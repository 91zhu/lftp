/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

class NetAccess : public FileAccess
{
protected:
   Resolver *resolver;

   sockaddr_u *peer;
   int	 peer_num;
   int	 peer_curr;
   void	 ClearPeer();
   void	 NextPeer();

   bool	 relookup_always;

   int	 max_retries;
   int	 retries;

   int	 idle;
   time_t idle_start;

   int	 timeout;
   int	 CheckTimeout();

   int	 reconnect_interval;

   int	 socket_buffer;
   int	 socket_maxseg;
   void	 SetSocketBuffer(int sock);
   void	 SetSocketMaxseg(int sock);
   static void SetKeepAlive(int sock);
   static void KeepAlive(int sock);
   static void SetSocketBuffer(int sock,int val);
   static void SetSocketMaxseg(int sock,int val);

   class RateLimit *rate_limit;

   static const char *SocketNumericAddress(const sockaddr_u *u);
   static int SocketPort(const sockaddr_u *u);
   static socklen_t SocketAddrLen(const sockaddr_u *u);
   static int SocketConnect(int fd,const sockaddr_u *u);

   char	 *proxy;
   char	 *proxy_port;
   char  *proxy_user;
   char  *proxy_pass;

   void SayConnectingTo();

   void SetProxy(const char *);

   int Resolve(const char *defp,const char *ser,const char *pr);

public:
   const char *GetProto() { return "net"; }

   void Init();

   NetAccess();
   NetAccess(const NetAccess *);
   ~NetAccess();

   void Reconfig(const char *name=0);

   void Connect(const char *,const char *);
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
      time_t t;

      void AdjustTime();
      void Reset();
      void Used(int);
   };

private:
   static int total_xfer_number;
   static bool total_reconfig_needed;
   static void ReconfigTotal();
   static BytesPool total;
   BytesPool one;

public:
   RateLimit();
   ~RateLimit();

   int BytesAllowed();
   void BytesUsed(int);

   void Reconfig(const char *name,const char *c);
};

#endif//NETACCESS_H
