/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef RESMGR_H
#define RESMGR_H

#include "trio.h"
#include <sys/types.h>
#include <time.h>
#include "xmalloc.h"
#include "TimeDate.h"

typedef const char *ResValValid(char **value);
typedef const char *ResClValid(char **closure);

class ResValue;
class ResDecl;

class ResMgr
{
   friend class ResDecl;
public:
   class Resource
   {
      friend class ResMgr;
      friend class ResDecl;

      const ResDecl *type;
      char *value;
      char *closure;

      Resource *next;

      bool ClosureMatch(const char *cl_data);

      Resource(Resource *next,const ResDecl *type,
	       char *closure,char *value)
      {
	 this->type=type;
	 this->value=value;
	 this->closure=closure;
	 this->next=next;
      }
      ~Resource()
      {
	 xfree(closure);
	 xfree(value);
      }
   };

private:
   static Resource *chain;
   static ResDecl *type_chain;

public:
   static const char *QueryNext(const char *name,const char **closure,Resource **ptr);
   static const char *SimpleQuery(const char *name,const char *closure);
   static ResValue Query(const char *name,const char *closure);
   static bool QueryBool(const char *name,const char *closure);

   enum CmpRes {
      EXACT_PREFIX=0x00,SUBSTR_PREFIX=0x01,
      EXACT_NAME  =0x00,SUBSTR_NAME  =0x10,
      DIFFERENT=-1
   };

   static int VarNameCmp(const char *name1,const char *name2);
   static const char *FindVar(const char *name,ResDecl **type);
   static const char *Set(const char *name,const char *closure,const char *value);

   static char *Format(bool with_defaults,bool only_defaults);
   static char **Generator(void);

   static const char *BoolValidate(char **value);
   static const char *TriBoolValidate(char **value);
   static const char *NumberValidate(char **value);
   static const char *UNumberValidate(char **value);
   static const char *FloatValidate(char **value);
   static const char *TimeIntervalValidate(char **value);
   static const char *RangeValidate(char **value);
   static const char *ERegExpValidate(char **value);
   static const char *IPv4AddrValidate(char **value);
   static const char *NoClosure(char **);
   static bool str2bool(const char *value);

   static void ClassInit();

   static int ResourceCompare(const Resource *a,const Resource *b);
   static int VResourceCompare(const void *a,const void *b);
};

class ResDecl
{
   friend class ResMgr;

   ResDecl *next;
   ResValValid *val_valid;
   ResClValid *closure_valid;
public:
   const char *name;
   char *defvalue;

   ResDecl(const char *name,const char *defvalue,
	   ResValValid *val_valid,ResClValid *closure_valid=0);
   ~ResDecl();

   ResValue Query(const char *closure);
   bool QueryBool(const char *closure);
};

class ResValue
{
   const char *s;
public:
   ResValue(const char *s_new)
      {
	 s=s_new;
      }
   bool to_bool()
      {
	 return ResMgr::str2bool(s);
      }
   operator int()
      {
	 return atoi(s);
      }
   operator double()
      {
	 return atof(s);
      }
   operator float()
      {
	 return atof(s);
      }
   operator const char*()
      {
	 return s;
      }
   bool is_nil() { return s==0; }
};

inline bool ResDecl::QueryBool(const char *closure)
{
   return Query(closure).to_bool();
}
inline bool ResMgr::QueryBool(const char *name,const char *closure)
{
   return Query(name,closure).to_bool();
}

class TimeInterval : public TimeDiff
{
   bool infty;
   const char *error_text;
public:
   TimeInterval(const char *);
   TimeInterval(time_t i) : TimeDiff(i,0) { infty=false; error_text=0; }
   TimeInterval(const TimeDiff &d) : TimeDiff(d) { infty=false; error_text=0; }
   bool Error() const { return error_text!=0; };
   const char *ErrorText() const { return error_text; }

   bool IsInfty() const { return infty; }
};

class Range
{
   int start;
   int end;
   bool full;
   const char *error_text;

public:
   Range(const char *s);
   bool Match(int n);
   bool IsFull() { return full; }
   int Random();
   bool Error() { return error_text!=0; };
   const char *ErrorText() { return error_text; }
};

#endif //RESMGR_H
