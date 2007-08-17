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

#ifndef NETACCESS_H
#define NETACCESS_H

#include "FileAccess.h"
#include "Resolver.h"
#include "LsCache.h"

class NetAccess : public FileAccess
{
protected:
   Resolver *resolver;

   sockaddr_u *peer;
   int	 peer_num;
   int	 peer_curr;
   void	 ClearPeer();
   void	 NextPeer();

   int	 max_retries;
   int	 max_persist_retries;
   int	 persist_retries;

   Timer idle_timer;
   Timer timeout_timer;
   bool	 CheckTimeout();

   int	 reconnect_interval;
   float reconnect_interval_current;
   float reconnect_interval_multiplier;
   int   reconnect_interval_max;

   int	 connection_limit;
   bool	 connection_takeover;

   class RateLimit *rate_limit;

   int	 socket_buffer;
   int	 socket_maxseg;
   void	 SetSocketBuffer(int sock);
   void	 SetSocketMaxseg(int sock);

   static void KeepAlive(int sock);
   static void MinimizeLatency(int sock);
   static void MaximizeThroughput(int sock);
   static void ReuseAddress(int sock);
   static int SocketBuffered(int sock);
   static const char *SocketNumericAddress(const sockaddr_u *u);
   static int SocketPort(const sockaddr_u *u);
   static socklen_t SocketAddrLen(const sockaddr_u *u);
   static int SocketConnect(int fd,const sockaddr_u *u);
   int SocketCreate(int,int,int);
   int SocketCreateTCP(int);
   int Poll(int fd,int ev);
   int CheckHangup(const struct pollfd *pfd,int num);

   char	 *proxy;
   char	 *proxy_port;
   char  *proxy_user;
   char  *proxy_pass;
   char  *proxy_proto;

   char	 *home_auto;
   void	 PropagateHomeAuto();
   const char *FindHomeAuto();

   void SayConnectingTo();

   void SetProxy(const char *);
   static bool NoProxy(const char *);

   int Resolve(const char *defp,const char *ser,const char *pr);

   const char *DelayingMessage();
   bool ReconnectAllowed();
   bool CheckRetries();	// returns false if max-retries exceeded.
   bool NextTry();	// increments retries; does CheckRetries().
   void TrySuccess();	// reset retry counters.

   virtual void HandleTimeout();

public:
   void Init();

   NetAccess();
   NetAccess(const NetAccess *);
   ~NetAccess();

   void Reconfig(const char *name=0);

   void Open(const char *fn,int mode,off_t offs);
   void ResetLocationData();

   void Close();

   int CountConnections();

   static void ClassInit();
};

class RateLimit
{
public:
   class BytesPool
   {
      friend class RateLimit;

      int pool;
      int rate;
      int pool_max;
      Time t;

      void AdjustTime();
      void Reset();
      void Used(int);
   };

private:
   static int total_xfer_number;
   static bool total_reconfig_needed;
   static void ReconfigTotal();
   static BytesPool total[2];
   BytesPool one[2];

public:
   RateLimit(const char *closure);
   ~RateLimit();

   enum dir_t { GET=0, PUT=1 };

   int BytesAllowed(dir_t how);
   int BytesAllowedToGet() { return BytesAllowed(GET); }
   int BytesAllowedToPut() { return BytesAllowed(PUT); }
   void BytesUsed(int,dir_t);
   void BytesGot(int b) { BytesUsed(b,GET); }
   void BytesPut(int b) { BytesUsed(b,PUT); }

   void Reconfig(const char *name,const char *c);
};

class GenericParseListInfo : public ListInfo
{
protected:
   FA::fileinfo *get_info;
   int get_info_cnt;

   int mode;
   IOBuffer *ubuf;

   bool get_time_for_dirs;
   bool can_get_prec_time;

   virtual FileSet *Parse(const char *buf,int len)
      { return session->ParseLongList(buf,len); }

public:
   GenericParseListInfo(FileAccess *session,const char *path);
   virtual ~GenericParseListInfo();
   int Do();
   const char *Status();
};


#endif//NETACCESS_H
