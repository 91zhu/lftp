/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#include <config.h>

#include <errno.h>
#include <assert.h>
#include <math.h>
#include <sys/types.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#include <arpa/inet.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif
#include "NetAccess.h"
#include "log.h"
#include "url.h"
#include "LsCache.h"
#include "misc.h"
#include "Speedometer.h"

#define super FileAccess

void NetAccess::Init()
{
   resolver=0;
   idle_timer.SetResource("net:idle",0);
   timeout_timer.SetResource("net:timeout",0);
   max_retries=0;
   max_persist_retries=0;
   persist_retries=0;
   socket_buffer=0;
   socket_maxseg=0;

   peer=0;
   peer_num=0;
   peer_curr=0;

   reconnect_interval=30;  // retry with 30 second interval
   reconnect_interval_multiplier=1.2;
   reconnect_interval_max=300;

   proxy=proxy_proto=0;
   proxy_port=0;
   proxy_user=proxy_pass=0;

   rate_limit=0;

   connection_limit=0;	// no limit.
   connection_takeover=false;

   home_auto=0;

   Reconfig(0);
}

NetAccess::NetAccess()
{
   Init();
}
NetAccess::NetAccess(const NetAccess *o) : super(o)
{
   Init();
   xfree(home_auto);
   home_auto=xstrdup(o->home_auto);
}
NetAccess::~NetAccess()
{
   Delete(resolver);
   if(rate_limit)
      delete rate_limit;
   ClearPeer();

   xfree(proxy);
   xfree(proxy_port);
   xfree(proxy_user);
   xfree(proxy_pass);
   xfree(proxy_proto);
   xfree(home_auto);
}

void NetAccess::Reconfig(const char *name)
{
   super::Reconfig(name);

   const char *c=hostname;

   reconnect_interval = ResMgr::Query("net:reconnect-interval-base",c);
   reconnect_interval_multiplier = ResMgr::Query("net:reconnect-interval-multiplier",c);
   if(reconnect_interval_multiplier<1)
      reconnect_interval_multiplier=1;
   reconnect_interval_max = ResMgr::Query("net:reconnect-interval-max",c);
   if(reconnect_interval_max<reconnect_interval)
      reconnect_interval_max=reconnect_interval;
   max_retries = ResMgr::Query("net:max-retries",c);
   max_persist_retries = ResMgr::Query("net:persist-retries",c);
   socket_buffer = ResMgr::Query("net:socket-buffer",c);
   socket_maxseg = ResMgr::Query("net:socket-maxseg",c);
   connection_limit = ResMgr::Query("net:connection-limit",c);
   connection_takeover = ResMgr::QueryBool("net:connection-takeover",c);

   if(rate_limit)
      rate_limit->Reconfig(name,c);
}

static int one=1;
void NetAccess::KeepAlive(int sock)
{
   setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,(char*)&one,sizeof(one));
}
void NetAccess::MinimizeLatency(int sock)
{
   int tos;
#ifdef IP_TOS
   tos = IPTOS_LOWDELAY;
   setsockopt(sock, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int));
#endif
}
void NetAccess::MaximizeThroughput(int sock)
{
   int tos;
#ifdef IP_TOS
   tos = IPTOS_THROUGHPUT;
   setsockopt(sock, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int));
#endif
}
void NetAccess::ReuseAddress(int sock)
{
   setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one));
}
void NetAccess::SetSocketBuffer(int sock)
{
   if(socket_buffer==0)
      return;
   if(-1==setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      Log::global->Format(1,"setsockopt(SO_SNDBUF,%d): %s\n",socket_buffer,strerror(errno));
   if(-1==setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      Log::global->Format(1,"setsockopt(SO_RCVBUF,%d): %s\n",socket_buffer,strerror(errno));
}
void NetAccess::SetSocketMaxseg(int sock)
{
#ifndef SOL_TCP
# define SOL_TCP IPPROTO_TCP
#endif
#ifdef TCP_MAXSEG
   if(socket_maxseg==0)
      return;
   if(-1==setsockopt(sock,SOL_TCP,TCP_MAXSEG,(char*)&socket_maxseg,sizeof(socket_maxseg)))
      Log::global->Format(1,"setsockopt(TCP_MAXSEG,%d): %s\n",socket_maxseg,strerror(errno));
#endif
}

const char *NetAccess::SocketNumericAddress(const sockaddr_u *u)
{
#ifdef HAVE_GETNAMEINFO
   static char buf[NI_MAXHOST];
   if(getnameinfo(&u->sa,SocketAddrLen(u),buf,sizeof(buf),0,0,NI_NUMERICHOST)<0)
      return "????";
   return buf;
#else
   static char buf[256];
   if(u->sa.sa_family!=AF_INET)
      return "????";
   unsigned char *a=(unsigned char *)&u->in.sin_addr;
   sprintf(buf,"%u.%u.%u.%u",a[0],a[1],a[2],a[3]);
   return buf;
#endif
}
int NetAccess::SocketPort(const sockaddr_u *u)
{
   if(u->sa.sa_family==AF_INET)
      return ntohs(u->in.sin_port);
#if INET6
   if(u->sa.sa_family==AF_INET6)
      return ntohs(u->in6.sin6_port);
#endif
   return 0;
}

socklen_t NetAccess::SocketAddrLen(const sockaddr_u *u)
{
   if(u->sa.sa_family==AF_INET)
      return sizeof(u->in);
#if INET6
   if(u->sa.sa_family==AF_INET6)
      return sizeof(u->in6);
#endif
   return sizeof(*u);
}

#if HAVE_INET_ATON && !HAVE_DECL_INET_ATON
CDECL int inet_aton(const char *,struct in_addr *);
#endif

int NetAccess::SocketCreate(int af,int type,int proto)
{
   int s=socket(af,type,proto);
   if(s<0)
      return s;
   const char *b=0;
   sockaddr_u bind_addr;
   memset(&bind_addr,0,sizeof(bind_addr));
   bind_addr.in.sin_family=af;
   if(af==AF_INET)
   {
      b=ResMgr::Query("net:socket-bind-ipv4",hostname);
      if(!(b && b[0] && inet_aton(b,&bind_addr.in.sin_addr)))
	 b=0;
   }
#if INET6
   else if(af==AF_INET6)
   {
      b=ResMgr::Query("net:socket-bind-ipv6",hostname);
      if(!(b && b[0] && inet_pton(af,b,&bind_addr.in6.sin6_addr)))
	 b=0;
   }
#endif
   if(b)
   {
      int res=bind(s,&bind_addr.sa,sizeof(bind_addr.sa));
      if(res==-1)
	 Log::global->Format(0,"**** bind(socket, %s): %s\n",b,strerror(errno));
   }
   return s;
}
int NetAccess::SocketCreateTCP(int af)
{
   return SocketCreate(af,SOCK_STREAM,IPPROTO_TCP);
}
int NetAccess::SocketConnect(int fd,const sockaddr_u *u)
{
   // some systems have wrong connect() prototype, so we have to cast off const.
   // in any case, connect does not alter the address.
   int res=connect(fd,(sockaddr*)&u->sa,SocketAddrLen(u));
   if(res!=-1)
      UpdateNow(); // if non-blocking doesn't work
   return res;
}

#ifdef TIOCOUTQ
static bool TIOCOUTQ_returns_free_space;
static bool TIOCOUTQ_works;
static bool TIOCOUTQ_tested;
static void test_TIOCOUTQ()
{
   int sock=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
   if(sock==-1)
      return;
   TIOCOUTQ_tested=true;
   int avail=-1;
   socklen_t len=sizeof(avail);
   if(getsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&avail,&len)==-1)
      avail=-1;
   int buf=-1;
   if(ioctl(sock,TIOCOUTQ,&buf)==-1)
      buf=-1;
   if(buf>=0 && avail>0 && (buf==0 || buf==avail))
   {
      TIOCOUTQ_works=true;
      TIOCOUTQ_returns_free_space=(buf==avail);
   }
   close(sock);
}
#endif
int NetAccess::SocketBuffered(int sock)
{
#ifdef TIOCOUTQ
   if(!TIOCOUTQ_tested)
      test_TIOCOUTQ();
   if(!TIOCOUTQ_works)
      return 0;
   int buffer=0;
   if(TIOCOUTQ_returns_free_space)
   {
      socklen_t len=sizeof(buffer);
      if(getsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&buffer,&len)==-1)
	 return 0;
      int avail=buffer;
      if(ioctl(sock,TIOCOUTQ,&avail)==-1)
	 return 0;
      if(avail>buffer)
	 return 0; // something wrong
      buffer-=avail;
      buffer=buffer*3/4; // approx...
   }
   else
   {
      if(ioctl(sock,TIOCOUTQ,&buffer)==-1)
	 return 0;
   }
   return buffer;
#else
   return 0;
#endif
}
int NetAccess::CheckHangup(const struct pollfd *pfd,int num)
{
   for(int i=0; i<num; i++)
   {
#ifdef SO_ERROR
      char  str[256];
      int   s_errno=0;
      socklen_t len;

      errno=0;

// Where does the error number go - to errno or to the pointer?
// It seems that to errno, but if the pointer is NULL it dumps core.
// (solaris 2.5)
// It seems to be different on glibc 2.0 - check both errno and s_errno

      len=sizeof(s_errno);
      getsockopt(pfd[i].fd,SOL_SOCKET,SO_ERROR,(char*)&s_errno,&len);
      if(errno==ENOTSOCK)
	 return 0;
      if(errno!=0 || s_errno!=0)
      {
	 sprintf(str,_("Socket error (%s) - reconnecting"),
				    strerror(errno?errno:s_errno));
	 DebugPrint("**** ",str,0);
	 return 1;
      }
#endif /* SO_ERROR */
      if(pfd[i].revents&POLLERR)
      {
	 DebugPrint("**** ","POLLERR",0);
	 return 1;
      }
   } /* end for */
   return 0;
}
int NetAccess::Poll(int fd,int ev)
{
   struct pollfd pfd;
   pfd.fd=fd;
   pfd.events=ev;
   pfd.revents=0;
   int res=poll(&pfd,1,0);
   if(res<1)
      return 0;
   if(CheckHangup(&pfd,1))
      return -1;
   if(pfd.revents)
      timeout_timer.Reset();
   return pfd.revents;
}

void NetAccess::SayConnectingTo()
{
   assert(peer_curr<peer_num);
   const char *h=(proxy?proxy:hostname);
   char *str=string_alloca(256+strlen(h));
   sprintf(str,_("Connecting to %s%s (%s) port %u"),proxy?"proxy ":"",
      h,SocketNumericAddress(&peer[peer_curr]),SocketPort(&peer[peer_curr]));
   DebugPrint("---- ",str,1);
}

void NetAccess::SetProxy(const char *px)
{
   bool was_proxied=(proxy!=0);

   xfree(proxy); proxy=0;
   xfree(proxy_port); proxy_port=0;
   xfree(proxy_user); proxy_user=0;
   xfree(proxy_pass); proxy_pass=0;

   if(!px)
      px="";

   ParsedURL url(px);
   if(!url.host || url.host[0]==0)
   {
      if(was_proxied)
	 ClearPeer();
      return;
   }

   proxy=xstrdup(url.host);
   proxy_port=xstrdup(url.port);
   proxy_user=xstrdup(url.user);
   proxy_pass=xstrdup(url.pass);
   proxy_proto=xstrdup(url.proto);
   ClearPeer();
}

bool NetAccess::NoProxy(const char *hostname)
{
   // match hostname against no-proxy var.
   if(!hostname)
      return false;
   const char *no_proxy_c=ResMgr::Query("net:no-proxy",0);
   if(!no_proxy_c)
      return false;
   char *no_proxy=alloca_strdup(no_proxy_c);
   int h_len=strlen(hostname);
   for(char *p=strtok(no_proxy," ,"); p; p=strtok(0," ,"))
   {
      int p_len=strlen(p);
      if(p_len>h_len || p_len==0)
	 continue;
      if(!strcasecmp(hostname+h_len-p_len,p))
	 return true;
   }
   return false;
}

void NetAccess::HandleTimeout()
{
   DebugPrint("**** ",_("Timeout - reconnecting"),0);
   Disconnect();
   timeout_timer.Reset();
}

bool NetAccess::CheckTimeout()
{
   if(timeout_timer.Stopped())
   {
      HandleTimeout();
      return(true);
   }
   return(false);
}

void NetAccess::ClearPeer()
{
   xfree(peer);
   peer=0;
   peer_curr=peer_num=0;
}

void NetAccess::NextPeer()
{
   peer_curr++;
   if(peer_curr>=peer_num)
      peer_curr=0;
   else
      try_time=0;	// try next address immediately
}

void NetAccess::ResetLocationData()
{
   Disconnect();
   ClearPeer();
   super::ResetLocationData();
   timeout_timer.SetResource("net:timeout",hostname);
   idle_timer.SetResource("net:idle",hostname);
}

void NetAccess::Open(const char *fn,int mode,off_t offs)
{
   timeout_timer.Reset();
   super::Open(fn,mode,offs);
}

int NetAccess::Resolve(const char *defp,const char *ser,const char *pr)
{
   int m=STALL;

   if(!resolver)
   {
      xfree(peer);
      peer=0;
      peer_num=0;
      if(proxy)
	 resolver=new Resolver(proxy,proxy_port,defp);
      else
	 resolver=new Resolver(hostname,portname,defp,ser,pr);
      Roll(resolver);
      if(!resolver)
	 return MOVED;
      m=MOVED;
   }

   if(!resolver->Done())
      return m;

   if(resolver->Error())
   {
      SetError(LOOKUP_ERROR,resolver->ErrorMsg());
      return(MOVED);
   }

   xfree(peer);
   peer=(sockaddr_u*)xmalloc(resolver->GetResultSize());
   peer_num=resolver->GetResultNum();
   resolver->GetResult(peer);
   if(peer_curr>=peer_num)
      peer_curr=0;

   Delete(resolver);
   resolver=0;
   return MOVED;
}


// RateLimit class implementation.
int RateLimit::total_xfer_number;
RateLimit::BytesPool RateLimit::total[2];
bool RateLimit::total_reconfig_needed=true;

RateLimit::RateLimit(const char *c)
{
   if(total_xfer_number==0)
   {
      total[GET].Reset();
      total[PUT].Reset();
   }
   total_xfer_number++;
   Reconfig(0,c);
}
RateLimit::~RateLimit()
{
   total_xfer_number--;
}

#define LARGE 0x10000000
#define DEFAULT_MAX_COEFF 2
void RateLimit::BytesPool::AdjustTime()
{
   double dif=TimeDiff(SMTask::now,t);

   if(dif>0)
   {
      // prevent overflow
      if((LARGE-pool)/dif < rate)
	 pool = pool_max>0 ? pool_max : rate*DEFAULT_MAX_COEFF;
      else
	 pool += int(dif*rate+0.5);

      if(pool>pool_max && pool_max>0)
	 pool=pool_max;
      if(pool_max==0 && pool>rate*DEFAULT_MAX_COEFF)
	 pool=rate*DEFAULT_MAX_COEFF;

      t=SMTask::now;
   }
}

int RateLimit::BytesAllowed(dir_t dir)
{
   if(total_reconfig_needed)
      ReconfigTotal();

   if(one[dir].rate==0 && total[dir].rate==0) // unlimited
      return LARGE;

   one  [dir].AdjustTime();
   total[dir].AdjustTime();

   int ret=LARGE;
   if(total[dir].rate>0)
      ret=total[dir].pool/total_xfer_number;
   if(one[dir].rate>0 && ret>one[dir].pool)
      ret=one[dir].pool;
   return ret;
}

void RateLimit::BytesPool::Used(int bytes)
{
   if(pool<bytes)
      pool=0;
   else
      pool-=bytes;
}

void RateLimit::BytesUsed(int bytes,dir_t dir)
{
   total[dir].Used(bytes);
   one  [dir].Used(bytes);
}

void RateLimit::BytesPool::Reset()
{
   pool=rate;
   t=SMTask::now;
}
void RateLimit::Reconfig(const char *name,const char *c)
{
   if(name && strncmp(name,"net:limit-",10))
      return;
   ResMgr::Query("net:limit-rate",c).ToNumberPair(one[GET].rate,one[PUT].rate);
   ResMgr::Query("net:limit-max",c) .ToNumberPair(one[GET].pool_max,one[PUT].pool_max);
   one[GET].Reset(); // to cut bytes_pool.
   one[PUT].Reset();

   if(name && !strncmp(name,"net:limit-total-",16))
      total_reconfig_needed=true;
}
void RateLimit::ReconfigTotal()
{
   ResMgr::Query("net:limit-total-rate",0).ToNumberPair(total[GET].rate,total[PUT].rate);
   ResMgr::Query("net:limit-total-max",0) .ToNumberPair(total[GET].pool_max,total[PUT].pool_max);
   total[GET].Reset();
   total[PUT].Reset();
   total_reconfig_needed = false;
}

bool NetAccess::ReconnectAllowed()
{
   if(max_retries>0 && retries>=max_retries)
      return true; // it will fault later - no need to wait.
   if(connection_limit>0 && connection_limit<=CountConnections())
      return false;
   if(try_time==0)
      return true;
   if(time_t(now) >= try_time+long(reconnect_interval_current))
      return true;
   TimeoutS(long(reconnect_interval_current)-(time_t(now)-try_time));
   return false;
}

const char *NetAccess::DelayingMessage()
{
   static char buf[80];
   if(connection_limit>0 && connection_limit<=CountConnections())
      return _("Connection limit reached");
   long remains=long(reconnect_interval_current)-(time_t(now)-try_time);
   if(remains<=0)
      return "";
   sprintf(buf,"%s: %ld",_("Delaying before reconnect"),remains);
   current->TimeoutS(1);
   return buf;
}

bool NetAccess::NextTry()
{
   if(max_retries>0 && retries>=max_retries)
   {
      Fatal(_("max-retries exceeded"));
      return false;
   }
   if(retries==0)
      reconnect_interval_current=reconnect_interval;
   else if(reconnect_interval_multiplier>1)
   {
      reconnect_interval_current*=reconnect_interval_multiplier;
      if(reconnect_interval_current>reconnect_interval_max)
	 reconnect_interval_current=reconnect_interval_max;
   }
   try_time=now;
   retries++;
   return true;
}
void NetAccess::TrySuccess()
{
   retries=0;
   persist_retries=0;
   reconnect_interval_current=reconnect_interval;
}

void NetAccess::Close()
{
   if(mode!=CLOSED)
      idle_timer.Reset();

   TrySuccess();

   Delete(resolver);
   resolver=0;

   super::Close();
}

int NetAccess::CountConnections()
{
   int count=0;
   for(FileAccess *o=FirstSameSite(); o!=0; o=NextSameSite(o))
   {
      if(o->IsConnected())
	 count++;
   }
   return count;
}

void NetAccess::PropagateHomeAuto()
{
   if(!home_auto)
      return;
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      NetAccess *o=(NetAccess*)fo; // we are sure it is NetAccess.
      if(!o->home_auto)
      {
	 o->home_auto=xstrdup(home_auto);
	 if(!o->home)
	    o->set_home(home_auto);
      }
   }
}
const char *NetAccess::FindHomeAuto()
{
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      NetAccess *o=(NetAccess*)fo; // we are sure it is NetAccess.
      if(o->home_auto)
	 return o->home_auto;
   }
   return 0;
}


// GenericParseListInfo implementation
int GenericParseListInfo::Do()
{
#define need_size (need&FileInfo::SIZE)
#define need_time (need&FileInfo::DATE)

   FA::fileinfo *cur;
   FileInfo *file;
   int res;
   int m=STALL;

do_again:
   if(done)
      return m;

   if(!ubuf && !get_info)
   {
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      int err;
      if(use_cache && FileAccess::cache->Find(session,"",mode,&err,
				    &cache_buffer,&cache_buffer_size))
      {
	 if(err)
	 {
	    if(mode==FA::MP_LIST)
	    {
	       mode=FA::LONG_LIST;
	       goto do_again;
	    }
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open("",mode);
	 session->UseCache(use_cache);
	 ubuf=new IOBufferFileAccess(session);
	 ubuf->SetSpeedometer(new Speedometer());
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
	 Roll(session);
	 Roll(ubuf);
      }
      m=MOVED;
   }
   if(ubuf)
   {
      if(ubuf->Error())
      {
	 FileAccess::cache->Add(session,"",mode,session->GetErrorCode(),ubuf);
	 if(mode==FA::MP_LIST)
	 {
	    mode=FA::LONG_LIST;
	    delete ubuf;
	    ubuf=0;
	    m=MOVED;
	    goto do_again;
	 }
	 SetError(ubuf->ErrorText());
	 Delete(ubuf);
	 ubuf=0;
	 return MOVED;
      }

      if(!ubuf->Eof())
	 return m;

      FileAccess::cache->Add(session,"",mode,FA::OK,ubuf);

      // now we have all the index in ubuf; parse it.
      const char *b;
      int len;
      ubuf->Get(&b,&len);

      int old_mode=mode;

      FileSet *set=Parse(b,len);
      if(set)
      {
	 bool need_resort=false;
	 set->rewind();
	 for(file=set->curr(); file!=0; file=set->next())
	 {
	    // tilde is special.
	    if(file->name[0]=='~')
	    {
	       int len=strlen(file->name);
	       file->name=(char*)xrealloc(file->name,len+3);
	       memmove(file->name+2,file->name,len+1);
	       memcpy(file->name,"./",2);
	       need_resort=true;
	    }
	 }
	 if(need_resort && !result)
	    result=new FileSet; // Merge will sort the names
	 if(result)
	 {
	    result->Merge(set);
	    delete set;
	 }
	 else
	    result=set;
      }

      Delete(ubuf);
      ubuf=0;
      m=MOVED;

      // try another mode? Parse() can set mode to indicate it wants to try it.
      if(mode!=old_mode)
	 return m;

      if(!result)
	 result=new FileSet;

      result->ExcludeDots();
      result->Exclude(exclude_prefix,exclude);

      get_info_cnt=result->get_fnum();
      if(get_info_cnt==0)
      {
	 done=true;
	 return m;
      }

      get_info=(FA::fileinfo*)xmalloc(sizeof(*get_info)*get_info_cnt);
      cur=get_info;

      get_info_cnt=0;
      result->rewind();
      for(file=result->curr(); file!=0; file=result->next())
      {
	 cur->size=NO_SIZE;
	 cur->time=NO_DATE;
	 cur->get_size = need_size && !(file->defined & file->SIZE);
	 cur->get_time = need_time && (!(file->defined & file->DATE)
				 || (file->date.ts_prec>0 && can_get_prec_time));
	 cur->file=0;

	 if(file->defined & file->TYPE)
	 {
	    if(file->filetype==file->SYMLINK && follow_symlinks)
	    {
	       //file->filetype=file->NORMAL;
	       file->defined &= ~(file->SIZE|file->SYMLINK_DEF|file->MODE|file->DATE|file->TYPE);
	       cur->get_size=true;
	       cur->get_time=true;
	    }

	    if(file->filetype==file->SYMLINK)
	    {
	       // don't need these for symlinks
	       cur->get_size=false;
	       cur->get_time=false;
	    }
	    else if(file->filetype==file->DIRECTORY)
	    {
	       if(!get_time_for_dirs)
		  continue;
	       // don't need size for directories
	       cur->get_size=false;
	    }
	 }

	 if(cur->get_size || cur->get_time)
	 {
	    cur->file=file->name;
	    if(!cur->get_size)
	       cur->size=-1;
	    if(!cur->get_time)
	       cur->time=(time_t)-1;
	    cur++;
	    get_info_cnt++;
	 }
      }
      if(get_info_cnt==0)
      {
	 xfree(get_info);
	 get_info=0;
	 done=true;
	 return m;
      }
      session->GetInfoArray(get_info,get_info_cnt);
   }
   if(get_info)
   {
      res=session->Done();
      if(res==FA::DO_AGAIN)
	 return m;
      if(res==FA::IN_PROGRESS)
	 return m;
      session->Close();
      for(cur=get_info; get_info_cnt-->0; cur++)
      {
	 if(cur->time!=NO_DATE)
	    result->SetDate(cur->file,cur->time,0);
	 if(cur->size!=NO_SIZE)
	    result->SetSize(cur->file,cur->size);
      }
      xfree(get_info);
      get_info=0;
      done=true;
      m=MOVED;
   }
   return m;
}

GenericParseListInfo::GenericParseListInfo(FileAccess *s,const char *p)
   : ListInfo(s,p)
{
   get_info=0;
   get_info_cnt=0;

   ubuf=0;

   get_time_for_dirs=true;
   can_get_prec_time=true;

   mode=FA::MP_LIST;
}

GenericParseListInfo::~GenericParseListInfo()
{
   xfree(get_info);
   Delete(ubuf);
}

const char *GenericParseListInfo::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting directory contents (%lld) %s[%s]"),
		     (long long)session->GetPos(),
		     ubuf->GetRateStrS(),session->CurrentStatus());
      return s;
   }
   if(get_info)
   {
      sprintf(s,_("Getting files information (%d%%) [%s]"),
		     session->InfoArrayPercentDone(),
		     session->CurrentStatus());
      return s;
   }
   return "";
}
