/*
 * lftp and utils
 *
 * Copyright (c) 1998-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "buffer.h"
#include "xmalloc.h"
#include "FileAccess.h"
#include "misc.h"
#include "trio.h"
#include "Speedometer.h"
#include "log.h"

#define BUFFER_INC	   (8*1024) // should be power of 2

void Buffer::Get(const char **buf,int *size)
{
   if(in_buffer==0)
   {
      *size=0;
      if(eof)
	 *buf=0;
      else
	 *buf="";
      return;
   }
   *buf=buffer+buffer_ptr;
   *size=in_buffer;
}

void Buffer::GetSaved(const char **buf,int *size) const
{
   if(!save)
   {
      *size=0;
      *buf=0;
      return;
   }
   *buf=buffer;
   *size=buffer_ptr+in_buffer;
}

void Buffer::SaveRollback(int p)
{
   if(buffer_ptr<p)
      save=false;
   if(!save)
   {
      buffer_ptr=0;
      in_buffer=0;
   }
   else
   {
      buffer_ptr=p;
      in_buffer=0;
   }
}

void Buffer::Allocate(int size)
{
   if(in_buffer==0 && !save)
      buffer_ptr=0;

   int in_buffer_real=in_buffer;
   if(save)
      in_buffer_real+=buffer_ptr;

   if(buffer_allocated<in_buffer_real+size)
   {
      buffer_allocated=(in_buffer_real+size+(BUFFER_INC-1)) & ~(BUFFER_INC-1);
      buffer=(char*)xrealloc(buffer,buffer_allocated);
   }
   // could be round-robin, but this is easier
   if(!save && buffer_ptr+in_buffer+size>buffer_allocated)
   {
      memmove(buffer,buffer+buffer_ptr,in_buffer);
      buffer_ptr=0;
   }
}

void Buffer::SaveMaxCheck(int size)
{
   if(save && buffer_ptr+size>save_max)
      save=false;
}

void Buffer::Put(const char *buf,int size)
{
   SaveMaxCheck(size);

   if(in_buffer==0 && !save)
   {
      buffer_ptr=0;

      if(size>=PUT_LL_MIN)
      {
	 int res=Put_LL(buf,size);
	 if(res>=0)
	 {
	    buf+=res;
	    size-=res;
	    pos+=res;
	 }
      }
   }

   if(size==0)
      return;

   Allocate(size);

   memcpy(buffer+buffer_ptr+in_buffer,buf,size);
   in_buffer+=size;
   pos+=size;
}

void Buffer::Format(const char *f,...)
{
   va_list v;
   va_start(v,f);
   vFormat(f, v);
   va_end(v);
}

void Buffer::vFormat(const char *f, va_list v)
{
   int size=64;
   for(;;)
   {
      Allocate(size);
      va_list tmp;
      VA_COPY(tmp,v);
      int res=vsnprintf(buffer+buffer_ptr+in_buffer, size, f, v);
      va_end(tmp);
      if(res>=0 && res<size)
      {
	 in_buffer+=res;
	 return;
      }
      if(res>size)   // some vsnprintf's return desired buffer size.
	 size=res+1;
      else
	 size*=2;
   }
}

void Buffer::Skip(int len)
{
   if(len>in_buffer)
      len=in_buffer;
   in_buffer-=len;
   buffer_ptr+=len;
   pos+=len;
}
void Buffer::UnSkip(int len)
{
   if(len>buffer_ptr)
      len=buffer_ptr;
   in_buffer+=len;
   buffer_ptr-=len;
   pos-=len;
}

void Buffer::Empty()
{
   in_buffer=0;
   buffer_ptr=0;
   if(save_max>0)
      save=true;
}

int Buffer::Do()
{
   return STALL;
}

Buffer::Buffer()
{
   error_text=0;
   saved_errno=0;
   error_fatal=false;
   buffer=0;
   buffer_allocated=0;
   in_buffer=0;
   buffer_ptr=0;
   eof=false;
   broken=false;
   save=false;
   save_max=0;
   pos=0;
   rate=0;
}
Buffer::~Buffer()
{
   xfree(error_text);
   xfree(buffer);
   delete rate;
}

const char *Buffer::GetRateStrS()
{
   if(!rate || !rate->Valid())
      return "";
   return rate->GetStrS();
}
void Buffer::RateAdd(int n)
{
   if(!rate)
      return;
   rate->Add(n);
}

void Buffer::SetError(const char *e,bool fatal)
{
   xfree(error_text);
   error_text=xstrdup(e);
   error_fatal=fatal;
}
#if 0 // unused
void Buffer::SetError2(const char *e1,const char *e2)
{
   xfree(error_text);
   int len1=strlen(e1);
   int len2=strlen(e2);
   error_text=(char*)xmalloc(len1+len2+1);
   strcpy(error_text,e1);
   strcat(error_text,e2);
}
#endif

// IOBufferFDStream implementation
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#undef super
#define super IOBuffer
int IOBufferFDStream::Do()
{
   if(Done() || Error())
      return STALL;
   int res=0;
   switch(mode)
   {
   case PUT:
      if(in_buffer==0)
	 return STALL;
      if(put_ll_timer && !eof && in_buffer<PUT_LL_MIN
      && !put_ll_timer->Stopped())
	 return STALL;
      res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 RateAdd(res);
	 in_buffer-=res;
	 buffer_ptr+=res;
	 event_time=now;
	 return MOVED;
      }
      break;

   case GET:
      res=Get_LL(GET_BUFSIZE);
      if(res>0)
      {
	 RateAdd(res);
	 in_buffer+=res;
	 SaveMaxCheck(0);
	 event_time=now;
	 return MOVED;
      }
      if(eof)
      {
	 event_time=now;
	 return MOVED;
      }
      break;
   }
   if(res<0)
   {
      event_time=now;
      return MOVED;
   }
   int fd=stream->getfd();
   if(fd>=0)
      Block(fd,mode==PUT?POLLOUT:POLLIN);
   else
      TimeoutS(1);
   return STALL;
}
int IOBufferFDStream::Put_LL(const char *buf,int size)
{
   if(stream->broken())
   {
      broken=true;
      return -1;
   }

   int res=0;

   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
	 goto stream_err;
      event_time=now;
      return 0;
   }

   res=write(fd,buf,size);
   if(res==-1)
   {
      if(NonFatalError(errno))
	 return 0;
      if(errno==EPIPE)
      {
	 broken=true;
	 return -1;
      }
      saved_errno=errno;
      stream->MakeErrorText();
      goto stream_err;
   }
   if(put_ll_timer)
      put_ll_timer->Reset();
   return res;

stream_err:
   SetError(stream->error_text,!TemporaryNetworkError(saved_errno));
   return -1;
}

int IOBufferFDStream::Get_LL(int size)
{
   int res=0;

   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
	 goto stream_err;
      return 0;
   }

   Allocate(size);

   res=read(fd,buffer+buffer_ptr+in_buffer,size);
   if(res==-1)
   {
      if(NonFatalError(errno))
	 return 0;
      saved_errno=errno;
      stream->MakeErrorText();
      goto stream_err;
   }

   if(res==0)
      eof=true;
   return res;

stream_err:
   SetError(stream->error_text,!TemporaryNetworkError(saved_errno));
   return -1;
}

FgData *IOBufferFDStream::GetFgData(bool fg)
{
   if(stream->getfd()!=-1)
      return new FgData(stream->GetProcGroup(),fg);
   return 0;
}

bool IOBufferFDStream::Done()
{
   if(put_ll_timer)
      put_ll_timer->Stop();
   if(super::Done())
      return stream->Done(); // stream->Done indicates if sub-process finished
   return false;
}

// FileInputBuffer implementation
#undef super
#define super IOBuffer
IOBufferFileAccess::~IOBufferFileAccess()
{
   session->Resume();
   session->Close();
}
int IOBufferFileAccess::Do()
{
   if(Done() || Error())
      return STALL;

   int res=Get_LL(GET_BUFSIZE);
   if(res>0)
   {
      RateAdd(res);
      in_buffer+=res;
      SaveMaxCheck(0);
      event_time=now;
      return MOVED;
   }
   if(res<0)
   {
      event_time=now;
      return MOVED;
   }
   if(eof)
   {
      event_time=now;
      return MOVED;
   }
   return STALL;
}
int IOBufferFileAccess::Get_LL(int size)
{
   int res=0;

   Allocate(size);

   res=session->Read(buffer+buffer_ptr+in_buffer,size);
   if(res<0)
   {
      if(res==FA::DO_AGAIN)
	 return 0;
      SetError(session->StrError(res));
      return -1;
   }
   if(res==0)
      eof=true;
   return res;
}

void IOBufferFileAccess::Suspend()
{
   session->Suspend();
   super::Suspend();
}
void IOBufferFileAccess::Resume()
{
   super::Resume();
   session->Resume();
}
const char *IOBufferFileAccess::Status()
{
   return session->CurrentStatus();
}

unsigned long long Buffer::UnpackUINT64BE(int offset)
{
   if(in_buffer-offset<8)
      return 0;
   unsigned long long res=UnpackUINT32BE(offset);
   res=(res<<32)|UnpackUINT32BE(offset+4);
   return res;
}
long long Buffer::UnpackINT64BE(int offset)
{
   unsigned long long n=UnpackUINT64BE(offset);
   if(n&0x8000000000000000ULL)
      return -(long long)(n^0xFFFFFFFFFFFFFFFFULL)-1;
   return (long long)n;
}
unsigned Buffer::UnpackUINT32BE(int offset)
{
   if(in_buffer-offset<4)
      return 0;
   unsigned char *b=(unsigned char*)buffer+buffer_ptr+offset;
   return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
}
int Buffer::UnpackINT32BE(int offset)
{
   unsigned n=UnpackUINT32BE(offset);
   if(n&0x80000000U)
      return -(int)(n^0xFFFFFFFFU)-1;
   return (int)n;
}
unsigned Buffer::UnpackUINT16BE(int offset)
{
   if(in_buffer-offset<2)
      return 0;
   unsigned char *b=(unsigned char*)buffer+buffer_ptr+offset;
   return (b[0]<<8)|b[1];
}
unsigned Buffer::UnpackUINT8(int offset)
{
   if(in_buffer-offset<1)
      return 0;
   unsigned char *b=(unsigned char*)buffer+buffer_ptr+offset;
   return b[0];
}
void Buffer::PackUINT64BE(unsigned long long data)
{
   Log::global->Format(11,"PackUINT64BE(0x%016llX)\n",data);
   Allocate(8);
   PackUINT32BE((unsigned)(data>>32));
   PackUINT32BE((unsigned)(data&0xFFFFFFFFU));
}
void Buffer::PackINT64BE(long long data)
{
   unsigned long long n;
   if(data<0)
      n=(unsigned long long)(-data)^0xFFFFFFFFFFFFFFFFULL+1;
   else
      n=(unsigned long long)data;
   PackUINT64BE(n);
}
void Buffer::PackUINT32BE(unsigned data)
{
   Log::global->Format(11,"PackUINT32BE(0x%08X)\n",data);
   Allocate(4);
   char *b=buffer+buffer_ptr+in_buffer;
   b[0]=(data>>24)&255;
   b[1]=(data>>16)&255;
   b[2]=(data>>8)&255;
   b[3]=(data)&255;
   in_buffer+=4;
}
void Buffer::PackINT32BE(int data)
{
   unsigned n;
   if(data<0)
      n=(unsigned)(-data)^0xFFFFFFFFU+1;
   else
      n=(unsigned)data;
   PackUINT32BE(n);
}
void Buffer::PackUINT16BE(unsigned data)
{
   Allocate(2);
   char *b=buffer+buffer_ptr+in_buffer;
   b[0]=(data>>8)&255;
   b[1]=(data)&255;
   in_buffer+=2;
}
void Buffer::PackUINT8(unsigned data)
{
   Log::global->Format(11,"PackUINT8(0x%02X)\n",data);
   Allocate(1);
   char *b=buffer+buffer_ptr+in_buffer;
   b[0]=(data)&255;
   in_buffer+=1;
}
