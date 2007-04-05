/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FILESET_H
#define FILESET_H

#include <sys/types.h>
#include "xmalloc.h"

#undef TYPE

class TimeInterval;
class Range;

#define NO_SIZE	     ((off_t)-1L)
#define NO_SIZE_YET  ((off_t)-2L)
#define NO_DATE	     ((time_t)-1L)
#define NO_DATE_YET  ((time_t)-2L)

struct FileTimestamp
{
   time_t ts;
   int ts_prec;
   FileTimestamp() : ts(NO_DATE_YET), ts_prec(0) {}
   void set(time_t ts1,int ts1_prec) { ts=ts1; ts_prec=ts1_prec; }
   bool is_set() { return ts!=NO_DATE && ts!=NO_DATE_YET; }
   operator time_t() const { return ts; }
   time_t operator=(time_t t) { set(t,0); return t; }
};

class FileInfo
{
public:
   char	    *name;
   mode_t   mode;
   FileTimestamp date;
   off_t    size;
   void	    *data;
   const char *user, *group;
   int      nlinks;

   enum	 type
   {
      UNKNOWN=0,
      DIRECTORY,
      SYMLINK,
      NORMAL
   };

   type	 filetype;
   char	 *symlink;

   int	 defined;
   enum defined_bits
   {
      NAME=001,MODE=002,DATE=004,TYPE=010,SYMLINK_DEF=020,
      SIZE=0100,USER=0200,GROUP=0400,NLINKS=01000,

      IGNORE_SIZE_IF_OLDER=02000, // for ignore mask
      IGNORE_DATE_IF_OLDER=04000, // for ignore mask

      ALL_INFO=NAME|MODE|DATE|TYPE|SYMLINK_DEF|SIZE|USER|GROUP|NLINKS
   };

   int rank;
   char *longname;

   ~FileInfo();
   void Init();
   FileInfo() { Init(); }
   FileInfo(const FileInfo &fi);
   FileInfo(const char *n) { Init(); SetName(n); }

   void SetName(const char *n);
   void SetUser(const char *n);
   void SetGroup(const char *n);
   void LocalFile(const char *name, bool follow_symlinks);
   static FileInfo *parse_ls_line(const char *line,const char *tz);

   void SetMode(mode_t m) { mode=m; defined|=MODE; }
   void SetDate(time_t t,int prec) { date.set(t,prec); defined|=DATE; }
   void SetType(type t) { filetype=t; defined|=TYPE; }
   void SetSymlink(const char *s) { xfree(symlink); symlink=xstrdup(s);
      filetype=SYMLINK; defined|=TYPE|SYMLINK_DEF; }
   void	SetSize(off_t s) { size=s; defined|=SIZE; }
   void	SetNlink(int n) { nlinks=n; defined|=NLINKS; }

   void	 Merge(const FileInfo&);

   bool	 SameAs(const FileInfo *,int ignore);
   bool	 OlderThan(time_t t);
   bool	 NewerThan(time_t t);
   bool  SizeOutside(const Range *r);

   void	 SetAssociatedData(void *d,int len)
      {
	 xfree(data);
	 data=xmemdup(d,len);
      }
   void  *GetAssociatedData() { return data; }

   void SetRank(int r) { rank=r; }
   int GetRank() const { return rank; }
   void MakeLongName();
   void SetLongName(const char *s) { xfree(longname); longname=xstrdup(s); }
   const char *GetLongName() { if(!longname) MakeLongName(); return longname; }

   operator const char *() { return name; }
};

class PatternSet;

class FileSet
{
public:
   enum sort_e { BYNAME, BYSIZE, DIRSFIRST, BYRANK, BYDATE };

private:
   FileInfo **files;

   /* Alternate pointers when sort != NAME: */
   FileInfo **files_sort;

   int	 fnum;
   int	 allocated;

   int	 ind;

   void	 Sub(int);
   bool sorted;

   void add_before(int pos,FileInfo *fi);

public:
   FileSet()
   {
      files=files_sort=0;
      sorted=false;
      fnum=0;
      allocated=0;
      ind=0;
   }
   FileSet(const FileSet *s);
   ~FileSet();
   void Empty();

   int	 get_fnum() const { return fnum; }

   void	 Add(FileInfo *);
   void	 Merge(const FileSet *);
   void	 SubtractSame(const FileSet *,int ignore);
   void	 SubtractAny(const FileSet *);
   void  SubtractOlderThan(time_t t);
   void  SubtractNewerThan(time_t t);
   void  SubtractSizeOutside(const Range *r);
   void  SubtractDirs();
   void  SubtractNotDirs();
   void  SubtractNotIn(const FileSet *);
   void  SubtractSameType(const FileSet *);
   void  SubtractCurr();
   void  Sort(sort_e newsort, bool casefold=false, bool reverse=false);
   void  Unsort();
   void	 SortByPatternList(const char *list_c);

   void	 Exclude(const char *prefix,PatternSet *x);
   void	 ExcludeDots();
   void	 ExcludeUnaccessible();

   void	 rewind() { ind=0; }
   FileInfo *curr();
   FileInfo *next();

   void	 LocalRemove(const char *dir);
   void	 LocalUtime(const char *dir,bool only_dirs=false);
   void	 LocalChmod(const char *dir,mode_t mask=0);
   void	 LocalChown(const char *dir);

   void Count(int *d,int *f,int *s,int *o);

   int FindGEIndByName(const char *name) const;
   FileInfo *FindByName(const char *name) const;

   void  SetSize(const char *name,off_t size)
   {
      FileInfo *f=FindByName(name);
      if(f)
	 f->SetSize(size);
   }
   void  SetDate(const char *name,time_t date,int prec)
   {
      FileInfo *f=FindByName(name);
      if(f)
	 f->SetDate(date,prec);
   }

   /* add a path to all files */
   void PrependPath(const char *path);

   /* get all defined_bits used by this fileset */
   int Have() const;

   FileInfo * operator[](int i) const;

   int EstimateMemory() const;
};

#endif // FILESET_H
