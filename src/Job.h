/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef JOB_H
#define JOB_H

#include <stdarg.h>
#include "trio.h"
#undef printf

#include "SMTask.h"
#include "StatusLine.h"
#include "fg.h"
#include "FileAccess.h"

class Job : public SMTask
{
   static void SortJobs();

   Job	 *next;
   static Job *chain;
   int	 waiting_alloc;

protected:
   bool fg;
   FgData *fg_data;

   virtual ~Job();

public:
   int	 jobno;
   Job	 *parent;

   Job	 **waiting;
   int	 waiting_num;

   void AddWaiting(Job *);
   void RemoveWaiting(Job *);
   void ReplaceWaiting(Job *from,Job *to);

   void SetParent(Job *j) { parent=j; }
   void SetParentFg(Job *j, bool f=true)
      {
	 SetParent(j);
	 if(j->fg && f)
	    Fg();
      }

   void	 AllocJobno();

   virtual void	  PrintStatus(int) {};
   virtual void	  ShowRunStatus(StatusLine *);
   virtual void	  SayFinal() {}; // final phrase of fg job
   virtual int	  Done()=0;
   virtual int	  ExitCode() { return 0; }
   virtual int	  Do()=0;
   virtual int	  AcceptSig(int);
   virtual void	  Bg();
   virtual void	  Fg();

   char	 *cmdline;
   virtual void ListJobs(int verbose_level,int indent=0);
   void PrintJobTitle(int indent=0,const char *suffix=0);
   void ListOneJob(int verbose,int indent=0,const char *suffix=0);
   void ListOneJobRecursively(int verbose,int indent);
   void ListDoneJobs();
   void BuryDoneJobs();
   Job *FindAnyChild();
   bool WaitsFor(Job *);
   static Job *FindWhoWaitsFor(Job *);
   int NumAwaitedJobs() { return waiting_num; }
   Job *FindDoneAwaitedJob();
   void WaitForAllChildren();

   static int NumberOfJobs();
   static Job *FindJob(int n);
   static bool Running(int n)
   {
      Job *j=FindJob(n);
      return j && !j->Done();
   }
   void Kill(int n);
   void SendSig(int n,int sig);
   void KillAll();

   void vfprintf(FILE *file,const char *fmt,va_list v);
   // CmdExec redefines this, and traps all messages of its children.
   virtual void top_vfprintf(FILE *file,const char *fmt,va_list v);

   // C-like functions calling vfprintf
   void eprintf(const char *fmt,...) PRINTF_LIKE(2,3);
   void fprintf(FILE *file,const char *fmt,...) PRINTF_LIKE(3,4);
   void printf(const char *fmt,...) PRINTF_LIKE(2,3);
   void perror(const char *);
   void puts(const char *);

   Job();

   virtual const char *GetConnectURL() { return 0; }
   virtual void lftpMovesToBackground() {}
   static  void lftpMovesToBackground_ToAll();
};

class SessionJob : public Job
{
protected:
   static void Reuse(FileAccess *s) { SessionPool::Reuse(s); }
   FileAccess *Clone();

   SessionJob(FileAccess *);
   ~SessionJob();

public:
   void PrintStatus(int v);
   FileAccess *session;
   const char *GetConnectURL()
      {
	 if(!session)
	    return 0;
	 return session->GetConnectURL();
      }
   void Fg();
   void Bg();
};

#endif /* JOB_H */
