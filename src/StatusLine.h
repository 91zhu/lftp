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

#ifndef STATUSLINE_H
#define STATUSLINE_H

#ifdef __GNUC__
# define PRINTF_LIKE(n,m) __attribute__((format(printf,n,m)))
#else
# define PRINTF_LIKE(n,m)
#endif

#include "SMTask.h"

class StatusLine : public SMTask
{
   int fd;
   char shown[0x800];
   bool	not_term;
   time_t update_time;
   char to_be_shown[0x800];
   bool update_delayed;
   void update(char *);

public:
   int GetWidth();
   void Show(const char *f,...) PRINTF_LIKE(2,3);
   void WriteLine(const char *f,...) PRINTF_LIKE(2,3);

   int getfd() { return fd; }

   StatusLine(int new_fd);
   ~StatusLine();

   int Do();
};

#endif // STATUSLINE_H
