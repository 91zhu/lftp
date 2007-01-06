/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000-2006 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include "Fish.h"
#include "trio.h"
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include "ascii_ctype.h"
#include "LsCache.h"
#include "misc.h"
#include "log.h"
#include "ArgV.h"

#define super NetAccess

#define max_buf 0x10000

void Fish::GetBetterConnection(int level)
{
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Fish *o=(Fish*)fo; // we are sure it is Fish.

      if(!o->recv_buf)
	 continue;

      if(o->state!=CONNECTED || o->mode!=CLOSED)
      {
	 if(level<2)
	    continue;
	 if(!connection_takeover || (o->priority>=priority && !o->IsSuspended()))
	    continue;
	 o->Disconnect();
	 return;
      }

      if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	 continue;

      // borrow the connection
      MoveConnectionHere(o);
      return;
   }
}

int Fish::Do()
{
   int m=STALL;
   int fd;

   // check if idle time exceeded
   if(mode==CLOSED && send_buf && idle_timer.Stopped())
   {
      DebugPrint("---- ",_("Closing idle connection"),1);
      Disconnect();
      return m;
   }

   if(Error())
      return m;

   if(!hostname)
      return m;

   if(send_buf && send_buf->Error())
   {
      Disconnect();
      return MOVED;
   }
   m|=HandleReplies();

   if(Error())
      return m;

   if(send_buf)
      timeout_timer.Reset(send_buf->EventTime());
   if(recv_buf)
      timeout_timer.Reset(recv_buf->EventTime());

   if((state==FILE_RECV || state==FILE_SEND)
   && rate_limit==0)
      rate_limit=new RateLimit(hostname);

   const char *charset;
   switch(state)
   {
   case DISCONNECTED:
   {
      if(mode==CLOSED)
	 return m;
      if(mode==CONNECT_VERIFY)
	 return m;

      // walk through Fish classes and try to find identical idle session
      // first try "easy" cases of session take-over.
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0 || connection_limit>CountConnections()))
	    break;
	 GetBetterConnection(i);
	 if(state!=DISCONNECTED)
	    return MOVED;
      }

      if(!ReconnectAllowed())
	 return m;

      if(!NextTry())
	 return MOVED;

      const char *shell=Query("shell",hostname);
      char *init=alloca_strdup2("echo FISH:;",strlen(shell));
      strcat(init,shell);

      const char *prog=Query("connect-program",hostname);
      if(!prog || !prog[0])
	 prog="ssh -a -x";
      char *a=alloca_strdup(prog);
      ArgV *cmd=new ArgV;
      for(a=strtok(a," "); a; a=strtok(0," "))
	 cmd->Add(a);
      if(user)
      {
	 cmd->Add("-l");
	 cmd->Add(user);
      }
      if(portname)
      {
	 cmd->Add("-p");
	 cmd->Add(portname);
      }
      cmd->Add(hostname);
      cmd->Add(init);
      {
      char *cmd_str=cmd->Combine(0);
      Log::global->Format(9,"---- %s (%s)\n",_("Running connect program"),cmd_str);
      xfree(cmd_str);
      }
      ssh=new PtyShell(cmd);
      state=CONNECTING;
      timeout_timer.Reset();
      m=MOVED;
   }
   case CONNECTING:
      fd=ssh->getfd();
      if(fd==-1)
      {
	 if(ssh->error())
	 {
	    SetError(FATAL,ssh->error_text);
	    return MOVED;
	 }
	 TimeoutS(1);
	 return m;
      }
      ssh->Kill(SIGCONT);
      send_buf=new IOBufferFDStream(ssh,IOBuffer::PUT);
      ssh=0;
      recv_buf=new IOBufferFDStream(new FDStream(fd,"pseudo-tty"),IOBuffer::GET);
      set_real_cwd("~");
      state=CONNECTING_1;
      m=MOVED;

   case CONNECTING_1:
      if(!received_greeting)
	 return m;

      charset=ResMgr::Query("fish:charset",hostname);
      if(charset && *charset)
      {
	 send_buf->SetTranslation(charset,false);
	 recv_buf->SetTranslation(charset,true);
      }

      Send("#FISH\n"
	   "exec 2>&1;echo;start_fish_server;"
	   "TZ=GMT;export TZ;LC_ALL=C;export LC_ALL;"
	   "echo '### 200'\n");
      PushExpect(EXPECT_FISH);
      Send("#VER 0.0.2\n"
	   "echo '### 000'\n");
      PushExpect(EXPECT_VER);
      if(home_auto==0)
      {
	 Send("#PWD\n"
	      "pwd; echo '### 200'\n");
	 PushExpect(EXPECT_PWD);
      }
      state=CONNECTED;
      m=MOVED;

   case CONNECTED:
      if(mode==CLOSED)
	 return m;
      if(home.path==0 && !RespQueueIsEmpty())
	 goto usual_return;
      ExpandTildeInCWD();
      if(mode!=CHANGE_DIR && xstrcmp(cwd,real_cwd))
      {
	 if(path_queue_len==0 || strcmp(path_queue[path_queue_len-1],cwd))
	 {
	    Send("#CWD %s\n"
		 "cd %s; echo '### 000'\n",cwd.path,shell_encode(cwd));
	    PushExpect(EXPECT_CWD);
	    PushDirectory(cwd);
	 }
	 if(!RespQueueIsEmpty())
	    goto usual_return;
      }
      SendMethod();
      if(mode==LONG_LIST || mode==LIST || mode==QUOTE_CMD)
      {
	 state=FILE_RECV;
	 m=MOVED;
	 goto usual_return;
      }
      state=WAITING;
      m=MOVED;
   case WAITING:
      if(RespQueueSize()==1 && mode==RETRIEVE)
      {
	 state=FILE_RECV;
	 m=MOVED;
      }
      if(RespQueueSize()==1 && mode==STORE)
      {
	 state=FILE_SEND;
	 real_pos=0;
	 pos=0;
	 m=MOVED;
      }
      if(RespQueueSize()==0)
      {
	 if(mode==ARRAY_INFO && array_ptr<array_cnt)
	    SendArrayInfoRequests();
	 else
	    state=DONE;
	 m=MOVED;
      }
      goto usual_return;
   case FILE_RECV:
      if(recv_buf->Size()>=rate_limit->BytesAllowedToGet())
      {
	 recv_buf->Suspend();
	 Timeout(1000);
      }
      else if(recv_buf->Size()>=max_buf)
      {
	 recv_buf->Suspend();
	 m=MOVED;
      }
      else if(recv_buf->IsSuspended())
      {
	 recv_buf->Resume();
	 if(recv_buf->Size()>0 || (recv_buf->Size()==0 && recv_buf->Eof()))
	    m=MOVED;
      }
      break;
   case FILE_SEND:
   case DONE:
      goto usual_return;
   }
usual_return:
   if(m==MOVED)
      return MOVED;
   if(send_buf)
      timeout_timer.Reset(send_buf->EventTime());
   if(recv_buf)
      timeout_timer.Reset(recv_buf->EventTime());
   if(CheckTimeout())
      return MOVED;
// notimeout_return:
   if(m==MOVED)
      return MOVED;
   return m;
}

void Fish::MoveConnectionHere(Fish *o)
{
   send_buf=o->send_buf; o->send_buf=0;
   recv_buf=o->recv_buf; o->recv_buf=0;
   rate_limit=o->rate_limit; o->rate_limit=0;
   path_queue=o->path_queue; o->path_queue=0;
   path_queue_len=o->path_queue_len; o->path_queue_len=0;
   RespQueue=o->RespQueue; o->RespQueue=0;
   RQ_alloc=o->RQ_alloc; o->RQ_alloc=0;
   RQ_head=o->RQ_head; o->RQ_head=0;
   RQ_tail=o->RQ_tail; o->RQ_tail=0;
   timeout_timer.Reset(o->timeout_timer);
   set_real_cwd(o->real_cwd);
   o->set_real_cwd(0);
   state=CONNECTED;
   o->Disconnect();
   if(!home)
      set_home(home_auto);
   ResumeInternal();
}

void Fish::Disconnect()
{
   if(send_buf)
      DebugPrint("---- ",_("Disconnecting"),9);
   Delete(send_buf); send_buf=0;
   Delete(recv_buf); recv_buf=0;
   delete ssh; ssh=0;
   EmptyRespQueue();
   EmptyPathQueue();
   state=DISCONNECTED;
   if(mode==STORE)
      SetError(STORE_FAILED,0);
   received_greeting=false;
   password_sent=0;
   xfree(home_auto); home_auto=0;
   home_auto=xstrdup(FindHomeAuto());
}

void Fish::EmptyPathQueue()
{
   for(int i=0; i<path_queue_len; i++)
      xfree(path_queue[i]);
   path_queue_len=0;
}

void Fish::Init()
{
   state=DISCONNECTED;
   send_buf=0;
   recv_buf=0;
   ssh=0;
   max_send=0;
   line=0;
   message=0;
   RespQueue=0;
   RQ_alloc=0;
   RQ_head=RQ_tail=0;
   eof=false;
   path_queue=0;
   path_queue_len=0;
   received_greeting=false;
   password_sent=0;
}

Fish::Fish()
{
   Init();
   Reconfig(0);
}

Fish::~Fish()
{
   Disconnect();
   xfree(line);
   xfree(message);
   EmptyRespQueue();
   xfree(RespQueue);
   EmptyPathQueue();
   xfree(path_queue);
}

Fish::Fish(const Fish *o) : super(o)
{
   Init();
   Reconfig(0);
}

void Fish::Close()
{
   switch(state)
   {
   case(DISCONNECTED):
   case(WAITING):
   case(CONNECTED):
   case(DONE):
      break;
   case(FILE_SEND):
      if(!RespQueueIsEmpty())
	 Disconnect();
      break;
   case(FILE_RECV):
   case(CONNECTING):
   case(CONNECTING_1):
      Disconnect();
   }
//    if(!RespQueueIsEmpty())
//       Disconnect(); // play safe.
   CloseExpectQueue();
   state=(recv_buf?CONNECTED:DISCONNECTED);
   eof=false;
   encode_file=true;
   super::Close();
}

void Fish::Send(const char *format,...)
{
   va_list va;
   char *str;

   static int max_send=256;
   for(;;)
   {
      va_start(va,format);
      str=string_alloca(max_send);
      int res=vsnprintf(str,max_send,format,va);
      va_end(va);
      if(res>=0 && res<max_send)
      {
	 if(res<max_send/16)
	    max_send/=2;
	 break;
      }
      max_send*=2;
   }

   DebugPrint("---> ",str,5);
   send_buf->Put(str);
}

void Fish::SendArrayInfoRequests()
{
   for(int i=array_ptr; i<array_cnt; i++)
   {
      if(array_for_info[i].get_time || array_for_info[i].get_size)
      {
	 const char *e=shell_encode(array_for_info[i].file);
	 Send("#INFO %s\n"
	      "ls -lLd %s; echo '### 200'\n",array_for_info[i].file,e);
	 PushExpect(EXPECT_INFO);
      }
      else
      {
	 if(i==array_ptr)
	    array_ptr++;   // if it is first one, just skip it.
	 else
	    break;	   // otherwise, wait until it is first.
      }
   }
}

void Fish::SendMethod()
{
   const char *e=alloca_strdup(shell_encode(file));
   const char *e1=shell_encode(file1);
   switch((open_mode)mode)
   {
   case CHANGE_DIR:
      Send("#CWD %s\n"
	   "cd %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_CWD);
      PushDirectory(file);
      break;
   case LONG_LIST:
      if(!encode_file)
	 e=file;
      Send("#LIST %s\n"
	   "ls -la %s; echo '### 200'\n",e,e);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case LIST:
      if(!encode_file)
	 e=file;
      Send("#LIST %s\n"
	   "ls -a %s; echo '### 200'\n",e,e);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case RETRIEVE:
      if(pos>0)
      {
	 int bs=0x1000;
	 real_pos=pos-pos%bs;
	 Send("#RETRP %lld %s\n"
	      "ls -lLd %s; "
	      "echo '### 100'; "
	      "dd ibs=%d skip=%lld if=%s 2>/dev/null; "
	      "echo '### 200'\n",
	    (long long)real_pos,e,e,bs,(long long)real_pos/bs,e);
      }
      else
      {
	 Send("#RETR %s\n"
	   "ls -lLd %s; "
	   "echo '### 100'; cat %s; echo '### 200'\n",e,e,e);
	 real_pos=0;
      }
      PushExpect(EXPECT_RETR_INFO);
      PushExpect(EXPECT_RETR);
      break;
   case STORE:
      if(entity_size<0)
      {
	 SetError(NO_FILE,"Have to know file size before upload");
	 break;
      }
      if(entity_size>0)
      {
	 Send("#STOR %lld %s\n"
	      "rest=%lld;file=%s;:>$file;echo '### 001';"
	      "if echo 1|head -c 1 -q ->/dev/null 2>&1;then "
		  "head -c $rest -q -|(cat>$file;cat>/dev/null);"
	      "else while [ $rest -gt 0 ];do "
		  "bs=4096;cnt=`expr $rest / $bs`;"
		  "[ $cnt -eq 0 ] && { cnt=1;bs=$rest; }; "
		  "n=`dd ibs=$bs count=$cnt 2>/dev/null|tee -a $file|wc -c`;"
		  "[ \"$n\" -le 0 ] && exit;"
		  "rest=`expr $rest - $n`; "
	      "done;fi;echo '### 200'\n",
	    (long long)entity_size,e,(long long)entity_size,e);
#if 0
	 // dd pays attension to read boundaries and reads wrong number
	 // of bytes when ibs>1. Have to use the inefficient ibs=1.
	 Send("#STOR %lld %s\n"
	      ">%s;echo '### 001';"
	      "dd ibs=1 count=%lld 2>/dev/null"
	      "|(cat>%s;cat>/dev/null);echo '### 200'\n",
	      (long long)entity_size,e,
	      e,
	      (long long)entity_size,
	      e);
#endif
      }
      else
      {
	 Send("#STOR %lld %s\n"
	      ">%s;echo '### 001';echo '### 200'\n",
	      (long long)entity_size,e,e);
      }
      PushExpect(EXPECT_STOR_PRELIMINARY);
      PushExpect(EXPECT_STOR);
      real_pos=0;
      pos=0;
      break;
   case ARRAY_INFO:
      SendArrayInfoRequests();
      break;
   case REMOVE:
      Send("#DELE %s\n"
	   "rm -f %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case REMOVE_DIR:
      Send("#RMD %s\n"
	   "rmdir %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case MAKE_DIR:
      Send("#MKD %s\n"
	   "mkdir %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case RENAME:
      Send("#RENAME %s %s\n"
	   "mv %s %s; echo '### 000'\n",e,e1,e,e1);
      PushExpect(EXPECT_DEFAULT);
      break;
   case CHANGE_MODE:
      Send("#CHMOD %04o %s\n"
	   "chmod %04o %s; echo '### 000'\n",chmod_mode,e,chmod_mode,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case QUOTE_CMD:
      Send("#EXEC %s\n"
	   "%s; echo '### 200'\n",file,file);
      PushExpect(EXPECT_QUOTE);
      real_pos=0;
      break;
   case MP_LIST:
      SetError(NOT_SUPP);
      break;
   case CONNECT_VERIFY:
   case CLOSED:
      abort();
   }
}

int Fish::ReplyLogPriority(int code)
{
   if(code==-1)
      return 3;
   return 4;
}

int Fish::HandleReplies()
{
   int m=STALL;
   if(recv_buf==0 || state==FILE_RECV)
      return m;
   if(recv_buf->Size()<5)
   {
   hup:
      if(recv_buf->Error())
      {
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Eof())
      {
	 DebugPrint("**** ",_("Peer closed connection"),0);
	 if(!RespQueueIsEmpty() && RespQueue[RQ_head]==EXPECT_CWD && message)
	    SetError(NO_FILE,message);
	 Disconnect();
	 m=MOVED;
      }
      return m;
   }
   const char *b;
   int s;
   recv_buf->Get(&b,&s);
   const char *eol=(const char*)memchr(b,'\n',s);
   if(!eol)
   {
      if(state==CONNECTING_1)
      {
	 const char *p="password:";
	 const char *y="(yes/no)?";
	 int p_len=strlen(p);
	 int y_len=strlen(y);
	 if(s>0 && b[s-1]==' ')
	    s--;
	 if(s>=p_len && !strncasecmp(b+s-p_len,p,p_len)
	 || s>10 && !strncmp(b+s-2,"':",2))
	 {
	    if(!pass)
	    {
	       SetError(LOGIN_FAILED,_("Password required"));
	       return MOVED;
	    }
	    if(password_sent>1)
	    {
	       SetError(LOGIN_FAILED,_("Login incorrect"));
	       return MOVED;
	    }
	    recv_buf->Put("XXXX");
	    send_buf->Put(pass);
	    send_buf->Put("\n");
	    password_sent++;
	    return m;
	 }
	 if(s>=y_len && !strncasecmp(b+s-y_len,y,y_len))
	 {
	    recv_buf->Put("yes\n");
	    send_buf->Put("yes\n");
	    return m;
	 }
      }
      if(recv_buf->Eof() || recv_buf->Error())
	 goto hup;
      return m;
   }
   m=MOVED;
   s=eol-b+1;
   xfree(line);
   line=(char*)xmemdup(b,s);
   line[s-1]=0;
   recv_buf->Skip(s);

   int code=-1;
   if(s>7 && !memcmp(line,"### ",4)) {
      if(sscanf(line+4,"%3d",&code)!=1)
	 code=-1;
   }

   DebugPrint("<--- ",line,ReplyLogPriority(code));
   if(code==-1)
   {
      if(!received_greeting && !strcmp(line,"FISH:"))
      {
	 received_greeting=true;
	 return m;
      }
      if(message==0)
	 message=xstrdup(line);
      else
      {
	 message=(char*)xrealloc(message,xstrlen(message)+s+1);
	 strcat(message,"\n");
	 strcat(message,line);
      }
      return m;
   }

   if(RespQueueIsEmpty())
   {
      DebugPrint("**** ",_("extra server response"),3);
      xfree(message);
      message=0;
      return m;
   }
   expect_t &e=RespQueue[RQ_head];
   RQ_head++;

   bool keep_message=false;
   char *p;

   switch(e)
   {
   case EXPECT_FISH:
   case EXPECT_VER:
      /* nothing yet */
      break;;
   case EXPECT_PWD:
      if(!message)
	 break;
      home_auto=xstrdup(message);
      Log::global->Format(9,"---- home set to %s\n",home_auto);
      PropagateHomeAuto();
      if(!home)
	 set_home(home_auto);
      cache->SetDirectory(this, home, true);
      break;
   case EXPECT_CWD:
      p=PopDirectory();
      if(message==0)
      {
	 set_real_cwd(p);
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    cwd.Set(p);
	    eof=true;
	 }
	 cache->SetDirectory(this,p,true);
      }
      else
	 SetError(NO_FILE,message);
      xfree(p);
      break;
   case EXPECT_RETR_INFO:
      if(message && is_ascii_digit(message[0]) && !strchr(message,':'))
      {
	 long long size_ll;
	 if(1==sscanf(message,"%lld",&size_ll))
	 {
	    entity_size=size_ll;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
      }
      else if(message)
      {
	 FileInfo *fi=FileInfo::parse_ls_line(message,"GMT");
	 if(!fi)
	 {
	    SetError(NO_FILE,message);
	    return MOVED;
	 }
	 if(fi->defined&fi->SIZE)
	 {
	    entity_size=fi->size;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
	 if(fi->defined&fi->DATE)
	 {
	    entity_date=fi->date;
	    if(opt_date)
	       *opt_date=entity_date;
	 }
      }
      state=FILE_RECV;
      break;
   case EXPECT_INFO:
   {
      FileInfo *fi=FileInfo::parse_ls_line(message,"GMT");
      if(fi && fi->defined&fi->SIZE)
	 array_for_info[array_ptr].size=fi->size;
      else
	 array_for_info[array_ptr].size=NO_SIZE;
      if(fi && fi->defined&fi->DATE)
	 array_for_info[array_ptr].time=fi->date;
      else
	 array_for_info[array_ptr].time=NO_DATE;
      array_for_info[array_ptr].get_size=false;
      array_for_info[array_ptr].get_time=false;
      array_ptr++;
      break;
   }
   case EXPECT_RETR:
   case EXPECT_DIR:
   case EXPECT_QUOTE:
      eof=true;
      state=DONE;
      break;
   case EXPECT_DEFAULT:
      if(message)
	 SetError(NO_FILE,message);
      break;
   case EXPECT_STOR_PRELIMINARY:
      if(message)
      {
	 Disconnect();
	 SetError(NO_FILE,message);
      }
      break;
   case EXPECT_STOR:
      if(message)
      {
	 Disconnect();
	 SetError(NO_FILE,message);
      }
      break;
   case EXPECT_IGNORE:
      break;
   }

   if(!keep_message)
   {
      xfree(message);
      message=0;
   }

   return m;
}
void Fish::PushExpect(expect_t e)
{
   int newtail=RQ_tail+1;
   if(newtail>RQ_alloc)
   {
      if(RQ_head-0<newtail-RQ_alloc)
	 RespQueue=(expect_t*)
	    xrealloc(RespQueue,(RQ_alloc=newtail+16)*sizeof(*RespQueue));
      memmove(RespQueue,RespQueue+RQ_head,(RQ_tail-RQ_head)*sizeof(*RespQueue));
      RQ_tail=0+(RQ_tail-RQ_head);
      RQ_head=0;
      newtail=RQ_tail+1;
   }
   RespQueue[RQ_tail]=e;
   RQ_tail=newtail;
}
void Fish::CloseExpectQueue()
{
   for(int i=RQ_head; i<RQ_tail; i++)
   {
      switch(RespQueue[i])
      {
      case EXPECT_IGNORE:
      case EXPECT_FISH:
      case EXPECT_VER:
      case EXPECT_PWD:
      case EXPECT_CWD:
	 break;
      case EXPECT_RETR_INFO:
      case EXPECT_INFO:
      case EXPECT_RETR:
      case EXPECT_DIR:
      case EXPECT_QUOTE:
      case EXPECT_DEFAULT:
	 RespQueue[i]=EXPECT_IGNORE;
	 break;
      case EXPECT_STOR_PRELIMINARY:
      case EXPECT_STOR:
	 Disconnect();
	 break;
      }
   }
}

void Fish::PushDirectory(const char *p)
{
   path_queue=(char**)xrealloc(path_queue,++path_queue_len*sizeof(*path_queue));
   path_queue[path_queue_len-1]=xstrdup(p);
}
char *Fish::PopDirectory()
{
   assert(path_queue_len>0);
   char *p=path_queue[0];
   memmove(path_queue,path_queue+1,--path_queue_len*sizeof(*path_queue));
   return p; // caller should free it.
}

const char *memstr(const char *mem,size_t len,const char *str)
{
   size_t str_len=strlen(str);
   while(len>=str_len)
   {
      if(!memcmp(mem,str,str_len))
	 return mem;
      mem++;
      len--;
   }
   return 0;
}

int Fish::Read(void *buf,int size)
{
   if(Error())
      return error_code;
   if(mode==CLOSED)
      return 0;
   if(state==DONE)
      return 0;	  // eof
   if(state==FILE_RECV && real_pos>=0)
   {
      const char *buf1;
      int size1;
   get_again:
      if(recv_buf->Size()==0 && recv_buf->Error())
      {
	 Disconnect();
	 return DO_AGAIN;
      }
      recv_buf->Get(&buf1,&size1);
      if(buf1==0) // eof
      {
	 Disconnect();
	 return DO_AGAIN;
      }
      if(size1==0)
	 return DO_AGAIN;
      if(entity_size!=NO_SIZE && real_pos<entity_size)
      {
	 if(real_pos+size1>entity_size)
	    size1=entity_size-real_pos;
      }
      else
      {
	 const char *end=memstr(buf1,size1,"### ");
	 if(end)
	 {
	    size1=end-buf1;
	    if(size1==0)
	    {
	       state=WAITING;
	       if(HandleReplies()==MOVED)
		  current->Timeout(0);
	       return DO_AGAIN;
	    }
	 }
	 else
	 {
	    for(int j=0; j<3; j++)
	       if(size1>0 && buf1[size1-1]=='#')
		  size1--;
	    if(size1==0 && recv_buf->Eof())
	    {
	       Disconnect();
	       return DO_AGAIN;
	    }
	 }
      }

      int bytes_allowed=rate_limit->BytesAllowedToGet();
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
      if(size1==0)
	 return DO_AGAIN;
      if(norest_manual && real_pos==0 && pos>0)
	 return DO_AGAIN;
      if(real_pos<pos)
      {
	 off_t to_skip=pos-real_pos;
	 if(to_skip>size1)
	    to_skip=size1;
	 recv_buf->Skip(to_skip);
	 real_pos+=to_skip;
	 goto get_again;
      }
      if(size>size1)
	 size=size1;
      memcpy(buf,buf1,size);
      recv_buf->Skip(size);
      pos+=size;
      real_pos+=size;
      rate_limit->BytesGot(size);
      TrySuccess();
      return size;
   }
   return DO_AGAIN;
}

int Fish::Write(const void *buf,int size)
{
   if(mode!=STORE)
      return(0);

   Resume();
   Do();
   if(Error())
      return(error_code);

   if(state!=FILE_SEND || rate_limit==0)
      return DO_AGAIN;

   {
      int allowed=rate_limit->BytesAllowedToPut();
      if(allowed==0)
	 return DO_AGAIN;
      if(size+send_buf->Size()>allowed)
	 size=allowed-send_buf->Size();
   }
   if(size+send_buf->Size()>0x4000)
      size=0x4000-send_buf->Size();
   if(pos+size>entity_size)
   {
      size=entity_size-pos;
      // tried to write more than originally requested. Make it retry with Open:
      if(size==0)
	 return STORE_FAILED;
   }
   if(size<=0)
      return 0;
   send_buf->Put((char*)buf,size);
   TrySuccess();
   rate_limit->BytesPut(size);
   pos+=size;
   real_pos+=size;
   return(size);
}
int Fish::Buffered()
{
   if(send_buf==0)
      return 0;
   return send_buf->Size();
}
int Fish::StoreStatus()
{
   if(Error())
      return error_code;
   if(state!=FILE_SEND)
      return IN_PROGRESS;
   if(real_pos!=entity_size)
   {
      Disconnect();
      return IN_PROGRESS;
   }
   if(RespQueueSize()==0)
      return OK;
   return IN_PROGRESS;
}

int Fish::Done()
{
   if(mode==CLOSED)
      return OK;
   if(Error())
      return error_code;
   if(eof || state==DONE)
      return OK;
   if(mode==CONNECT_VERIFY)
      return OK;
   return IN_PROGRESS;
}

void Fish::SuspendInternal()
{
   if(recv_buf)
      recv_buf->SuspendSlave();
   if(send_buf)
      send_buf->SuspendSlave();
}
void Fish::ResumeInternal()
{
   if(recv_buf)
      recv_buf->ResumeSlave();
   if(send_buf)
      send_buf->ResumeSlave();
}

const char *Fish::CurrentStatus()
{
   switch(state)
   {
   case DISCONNECTED:
      if(!ReconnectAllowed())
	 return DelayingMessage();
      return _("Not connected");
   case CONNECTING:
      if(ssh && ssh->status)
	 return ssh->status;
   case CONNECTING_1:
      return _("Connecting...");
   case CONNECTED:
      return _("Connected");
   case WAITING:
      return _("Waiting for response...");
   case FILE_RECV:
      return _("Receiving data");
   case FILE_SEND:
      return _("Sending data");
   case DONE:
      return _("Done");
   }
   return "";
}

bool Fish::SameSiteAs(const FileAccess *fa) const
{
   if(!SameProtoAs(fa))
      return false;
   Fish *o=(Fish*)fa;
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool Fish::SameLocationAs(const FileAccess *fa)	const
{
   if(!SameSiteAs(fa))
      return false;
   Fish *o=(Fish*)fa;
   if(xstrcmp(cwd,o->cwd))
      return false;
   return true;
}

void Fish::Cleanup()
{
   if(hostname==0)
      return;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
      fo->CleanupThis();

   CleanupThis();
}
void Fish::CleanupThis()
{
   if(mode!=CLOSED)
      return;
   Disconnect();
}

void Fish::Reconfig(const char *name)
{
   super::Reconfig(name);
   if(!xstrcmp(name,"fish:charset") && recv_buf && send_buf)
   {
      if(!IsSuspended())
	 cache->TreeChanged(this,"/");
      const char *charset=ResMgr::Query("fish:charset",hostname);
      if(charset && *charset)
      {
	 send_buf->SetTranslation(charset,false);
	 recv_buf->SetTranslation(charset,true);
      }
   }
}

void Fish::ClassInit()
{
   // register the class
   Register("fish",Fish::New);
}
FileAccess *Fish::New() { return new Fish(); }

DirList *Fish::MakeDirList(ArgV *args)
{
   return new FishDirList(args,this);
}
#include "FileGlob.h"
Glob *Fish::MakeGlob(const char *pattern)
{
   return new GenericGlob(this,pattern);
}
ListInfo *Fish::MakeListInfo(const char *p)
{
   return new FishListInfo(this,p);
}

#undef super
#define super DirList
#include "ArgV.h"

int FishDirList::Do()
{
   if(done)
      return STALL;

   if(buf->Eof())
   {
      done=true;
      return MOVED;
   }

   if(!ubuf)
   {
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      int err;
      if(use_cache && FileAccess::cache->Find(session,pattern,FA::LONG_LIST,&err,
				    &cache_buffer,&cache_buffer_size))
      {
	 if(err)
	 {
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open(pattern,FA::LONG_LIST);
	 ((Fish*)session)->DontEncodeFile();
	 ubuf=new IOBufferFileAccess(session);
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      buf->PutEOF();
      FileAccess::cache->Add(session,pattern,FA::LONG_LIST,FA::OK,ubuf);
      return MOVED;
   }

   int m=STALL;

   if(len>0)
   {
      buf->Put(b,len);
      ubuf->Skip(len);
      m=MOVED;
   }

   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}

FishDirList::FishDirList(ArgV *a,FileAccess *fa)
   : DirList(a)
{
   session=fa;
   ubuf=0;
   pattern=args->Combine(1);
}

FishDirList::~FishDirList()
{
   Delete(ubuf);
   xfree(pattern);
}

const char *FishDirList::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
      return s;
   }
   return "";
}

void FishDirList::SuspendInternal()
{
   if(ubuf)
      ubuf->SuspendSlave();
}
void FishDirList::ResumeInternal()
{
   if(ubuf)
      ubuf->ResumeSlave();
}

static FileSet *ls_to_FileSet(const char *b,int len)
{
   FileSet *set=new FileSet;
   char *buf=string_alloca(len+1);
   memcpy(buf,b,len);
   buf[len]=0;
   for(char *line=strtok(buf,"\n"); line; line=strtok(0,"\n"))
   {
      int ll=strlen(line);
      if(ll && line[ll-1]=='\r')
	 line[--ll]=0;
      if(ll==0)
	 continue;

      FileInfo *f=FileInfo::parse_ls_line(line,"GMT");

      if(!f)
	 continue;

      set->Add(f);
   }
   return set;
}

FileSet *Fish::ParseLongList(const char *b,int len,int *err) const
{
   if(err)
      *err=0;
   return ls_to_FileSet(b,len);
}

// FishListInfo implementation
FileSet *FishListInfo::Parse(const char *b,int len)
{
   return ls_to_FileSet(b,len);
}


#include "modconfig.h"
#ifdef MODULE_PROTO_FISH
void module_init()
{
   Fish::ClassInit();
}
#endif
