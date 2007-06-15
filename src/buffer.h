/*
 * lftp and utils
 *
 * Copyright (c) 1998-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef BUFFER_H
#define BUFFER_H

#include "SMTask.h"
#include "Filter.h"
#include "Timer.h"
#include "fg.h"
#include "xstring.h"

#include <stdarg.h>

#ifdef HAVE_ICONV
CDECL_BEGIN
#include <iconv.h>
CDECL_END
#endif

#define GET_BUFSIZE 0x10000
#define PUT_LL_MIN  0x2000

class Speedometer;

class Buffer
{
protected:
   char *error_text;
   int  saved_errno;
   bool error_fatal;

   char *buffer;
   int buffer_allocated;
   int in_buffer;
   int buffer_ptr;
   bool eof;	  // no reads possible (except from mem buffer)
   bool broken;	  // no writes possible

   bool save;  // save skipped data
   int save_max;

   off_t pos;

   // low-level for derived classes
   virtual int Put_LL(const char *buf,int size) { return 0; }
   virtual int PutEOF_LL() { return 0; }

   Speedometer *rate;
   void RateAdd(int n);

   void Allocate(int size);

   void SaveMaxCheck(int addsize);

public:
   bool Error() const { return error_text!=0; }
   bool ErrorFatal() const { return error_fatal; }
   void SetError(const char *e,bool fatal=false);
   void SetError2(const char *e1,const char *e2,bool fatal=false);
   void SetErrorCached(const char *e);
   const char *ErrorText() const { return error_text; }
   int Size() const { return in_buffer; }
   bool Eof() const { return eof; }
   bool Broken() const { return broken; }

   void Get(const char **buf,int *size);
   void Skip(int len); // Get(); consume; Skip()
   void UnSkip(int len); // this only works if there were no Put's.
   void Put(const char *buf,int size);
   void Put(const char *buf) { Put(buf,strlen(buf)); }
   void Format(const char *f,...) PRINTF_LIKE(2,3);
   void vFormat(const char *f, va_list v);
   void PutEOF() { eof=true; PutEOF_LL(); }

   unsigned long long UnpackUINT64BE(int offset=0);
   unsigned UnpackUINT32BE(int offset=0);
   unsigned UnpackUINT16BE(int offset=0);
   unsigned UnpackUINT8(int offset=0);
   void PackUINT64BE(unsigned long long data);
   void PackUINT32BE(unsigned data);
   void PackUINT16BE(unsigned data);
   void PackUINT8(unsigned data);

   long long UnpackINT64BE(int offset=0);
   int UnpackINT32BE(int offset=0);
   int UnpackINT16BE(int offset=0);
   int UnpackINT8(int offset=0);
   void PackINT64BE(long long data);
   void PackINT32BE(int data);
   void PackINT16BE(int data);
   void PackINT8(int data);

   // useful for cache.
   void Save(int m) { save=true; save_max=m; }
   bool IsSaving() const { return save; }
   void GetSaved(const char **buf,int *size) const;
   void SaveRollback(int p);

   void SetPos(off_t p) { pos=p; }
   off_t GetPos() const { return pos; }

   void SetSpeedometer(Speedometer *s) { rate=s; }
   const char *GetRateStrS();

   void Empty();

   Buffer();
   virtual ~Buffer();
};

class DirectedBuffer : public Buffer
{
public:
   enum dir_t { GET, PUT };

protected:
#ifdef HAVE_ICONV
   iconv_t backend_translate;
#endif
   Buffer *untranslated;
   dir_t mode;
   void EmbraceNewData(int len);

public:
   DirectedBuffer(dir_t m) { mode=m; 
#ifdef HAVE_ICONV
	   backend_translate=0; 
#endif
	   untranslated=0;
   }
   ~DirectedBuffer();
   void SetTranslation(const char *be_encoding,bool translit=true);
   virtual void PutTranslated(const char *buf,int size);
   void PutTranslated(const char *buf) { PutTranslated(buf,strlen(buf)); }
   void ResetTranslation();
   void Put(const char *buf,int size)
      {
	 if(mode==PUT)
	    PutTranslated(buf,size);
	 else
	    Buffer::Put(buf,size);
      }
   void Put(const char *buf) { Put(buf,strlen(buf)); }
   dir_t GetDirection() { return mode; }
};

class IOBuffer : public DirectedBuffer, public SMTask
{
protected:
   Time event_time; // used to detect timeouts

public:
   IOBuffer(dir_t m) : DirectedBuffer(m), event_time(now) {}
   virtual const Time& EventTime()
      {
	 if(IsSuspended())
	    return now;
	 return event_time;
      }
   virtual bool Done()
      {
	 return(broken || Error() || (eof && (mode==GET || in_buffer==0)));
      }
   virtual int Get_LL(int size) { return 0; }
   virtual int Do();

   virtual FgData *GetFgData(bool) { return 0; }
   virtual const char *Status() { return ""; }
   virtual int Buffered() { return Size(); }
};

class IOBufferStacked : public IOBuffer
{
   IOBuffer *down;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

public:
   IOBufferStacked(IOBuffer *b) : IOBuffer(b->GetDirection()) { down=b; }
   ~IOBufferStacked() { Delete(down); }
   const Time& EventTime() { return down->EventTime(); }
   int Do();
   bool Done();
};

class IOBufferFDStream : public IOBuffer
{
   FDStream *stream;
   class Timer *put_ll_timer;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

protected:
   ~IOBufferFDStream() { delete stream; delete put_ll_timer; }

public:
   IOBufferFDStream(FDStream *o,dir_t m,Timer *t=0)
      : IOBuffer(m) { stream=o; put_ll_timer=t; }
   bool Done();
   FgData *GetFgData(bool fg);
   const char *Status() { return stream->status; }
};

class FileAccess;

class IOBufferFileAccess : public IOBuffer
{
   FileAccess *session;

   int Get_LL(int size);

protected:
   ~IOBufferFileAccess();

public:
   IOBufferFileAccess(FileAccess *i) : IOBuffer(GET) { session=i; }

   void SuspendInternal();
   void ResumeInternal();
   const char *Status();
};

#endif // BUFFER_H
