/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FTPCLASS_H
#define FTPCLASS_H

#include <stdio.h>
#include <time.h>

#include "NetAccess.h"
/*#include "ResMgr.h"*/

class Ftp : public NetAccess
{
   static Ftp *ftp_chain;
   Ftp *ftp_next;

   enum automate_state
   {
      EOF_STATE,	   // control connection is open, idle state
      INITIAL_STATE,	   // all connections are closed
      CONNECTING_STATE,	   // we are connecting the control socket
      WAITING_STATE,	   // we're waiting for a response with data
      ACCEPTING_STATE,	   // we're waiting for an incoming data connection
      DATA_OPEN_STATE,	   // data connection is open, for read or write
      FATAL_STATE,	   // fatal error occured (e.g. command not implemented)
      NO_FILE_STATE,	   // file access is impossible - no such file
      NO_HOST_STATE,	   // host not found or not specified
      STORE_FAILED_STATE,  // STOR failed - you have to reput
      LOGIN_FAILED_STATE,  // login failed due to invalid password
      SYSTEM_ERROR_STATE,  // system error occured, errno saved to saved_errno
      LOOKUP_ERROR_STATE,  // unsuccessful host name lookup
/*      ERROR_STATE,	   // see error_code.*/
      CWD_CWD_WAITING_STATE,  // waiting until 'CWD $cwd' finishes
      USER_RESP_WAITING_STATE,// waiting until 'USER $user' finishes
      DATASOCKET_CONNECTING_STATE,   // waiting for data_sock to connect
      COPY_FAILED
   };

   enum response
   {
      // only first digit of these responses should be used (RFC1123)
      RESP_READY=220,
      RESP_PASS_REQ=331,
      RESP_LOGGED_IN=230,
      RESP_CWD_RMD_DELE_OK=250,
      RESP_TYPE_OK=200,
      RESP_PORT_OK=200,
      RESP_REST_OK=350,
      RESP_TRANSFER_OK=226,
      RESP_RESULT_HERE=213,
      RESP_PWD_MKD_OK=257,
      RESP_NOT_IMPLEMENTED=502,
      RESP_NOT_UNDERSTOOD=500,
      RESP_NO_FILE=550,
      RESP_PERM_DENIED=553,
      RESP_BROKEN_PIPE=426,
      RESP_LOGIN_FAILED=530
   };

   enum check_case_t
   {
      CHECK_NONE,	// no special check
      CHECK_IGNORE,	// ignore response
      CHECK_READY,	// check response after connect
      CHECK_REST,	// check response for REST
      CHECK_CWD,	// check response for CWD
      CHECK_CWD_CURR,	// check response for CWD into current directory
      CHECK_CWD_STALE,	// check response for CWD when it's not critical
      CHECK_ABOR,	// check response for ABOR
      CHECK_SIZE,	// check response for SIZE
      CHECK_SIZE_OPT,	// check response for SIZE and save size to *opt_size
      CHECK_MDTM,	// check response for MDTM
      CHECK_MDTM_OPT,	// check response for MDTM and save size to *opt_date
      CHECK_PASV,	// check response for PASV and save address
      CHECK_EPSV,	// check response for EPSV and save address
      CHECK_FILE_ACCESS,// generic check for file access
      CHECK_PWD,	// check response for PWD and save it to home
      CHECK_RNFR,	// check RNFR and issue RNTO
      CHECK_USER,	// check response for USER
      CHECK_USER_PROXY,	// check response for USER sent to proxy
      CHECK_PASS,	// check response for PASS
      CHECK_PASS_PROXY,	// check response for PASS sent to proxy
      CHECK_TRANSFER	// generic check for transfer
   };

   struct expected_response;
   friend struct Ftp::expected_response;
   struct expected_response
   {
      int   expect;
      int   fail_state;
      check_case_t check_case;
      bool  log_resp;
      char  *path;
   };
   expected_response *RespQueue;
   int	 RQ_alloc;   // memory allocated

   int	 RQ_head;
   int	 RQ_tail;

   int	 multiline_code;

   void	 LogResp(const char *line);

   void  AddResp(int exp,int fail,check_case_t ck=CHECK_NONE,bool log=false);
   void  SetRespPath(const char *p);
   int   CheckResp(int resp);
   void  PopResp();
   void	 EmptyRespQueue();
   void	 CloseRespQueue(); // treat responses on Close()
   int   RespQueueIsEmpty() { return RQ_head==RQ_tail; }
   int	 RespQueueSize() { return RQ_tail-RQ_head; }
   expected_response *FindLastCWD();

   int	 RestCheck(int,int);
   int   NoFileCheck(int,int);
   int	 TransferCheck(int,int);
   int	 LoginCheck(int,int);
   int	 NoPassReqCheck(int,int);
   int	 proxy_LoginCheck(int,int);
   int	 proxy_NoPassReqCheck(int,int);
   const char *ExtractPWD();
   int	 CatchDATE(int,int);
   int	 CatchDATE_opt(int,int);
   int	 CatchSIZE(int,int);
   int	 CatchSIZE_opt(int,int);
   int	 Handle_PASV();
   int	 Handle_EPSV();

   void	 InitFtp();

   int   control_sock;
   int   data_sock;
   int	 aborted_data_sock;

   /* type of transfer: TYPE_A or TYPE_I */
   int   type;

   /* address for data accepting */
   sockaddr_u   data_sa;

   char  *resp;
   int   resp_size;
   int	 resp_alloc;
   char  *line;

   time_t   nop_time;
   long	    nop_offset;
   int	    nop_count;

   char	 *result;
   int	 result_size;

   void	 DataAbort();
   void  DataClose();

   void  ControlClose();
   void  AbortedClose();

   void	 SwitchToState(automate_state);

   void  SendCmd(const char *cmd);
   void	 FlushSendQueue(bool all=false);

   void	 ReceiveResp();
	 // If a response is received, it checks it for accordance with
	 // response_queue and switch to a state if necessary

   int	 Poll(int fd,int ev) { return FileAccess::Poll(fd,ev); }
   int	 Poll();

   int	 AbsolutePath(const char *p);

   char	 *send_cmd_buffer;
   int   send_cmd_count;   // in buffer
   int	 send_cmd_alloc;   // total allocated
   char  *send_cmd_ptr;	   // start

   void EmptySendQueue();

   automate_state state;

   char	 *anon_user;
   char	 *anon_pass;

   static const char *DefaultAnonPass();

   int	 flags;

   int	 StateToError();

   int	 addr_received;	// state of PASV

   bool	 wait_flush:1;	// wait until all responces come
   bool	 ignore_pass:1;	// logged in just with user
   bool  verify_data_address:1;
   bool  verify_data_port:1;
   bool	 rest_list:1;

   void	 GetBetterConnection(int level);
   bool  SameConnection(const Ftp *o);

   int	 nop_interval;

   void set_idle_start()
      {
	 time(&idle_start);
	 if(control_sock!=-1 && idle>0)
	    block+=TimeOut(idle*1000);
      }

   char *skey_pass;
   bool allow_skey;
   bool force_skey;
   const char *make_skey_reply();

   bool data_address_ok(sockaddr_u *d=0,bool verify_this_data_port=true);

   enum copy_mode_t { COPY_NONE, COPY_SOURCE, COPY_DEST };
   copy_mode_t copy_mode;
   sockaddr_u copy_addr;
   bool copy_addr_valid;
   bool copy_passive;
   friend class FtpCopy;

   const char *encode_eprt(sockaddr_u *);

public:
   static void ClassInit();

   Ftp();
   Ftp(const Ftp *);
   ~Ftp();

   const char *GetProto() { return "ftp"; }

   FileAccess *Clone() { return new Ftp(this); }
   static FileAccess *New() { return new Ftp(); }

   bool	 SameLocationAs(FileAccess *);
   bool	 SameSiteAs(FileAccess *);

   bool	 IsConnected()
   {
      if(control_sock==-1)
	 return false;
      Do();
      return(control_sock!=-1 && state!=CONNECTING_STATE);
   }
   bool	 IsBetterThan(FileAccess *fa)
   {
      return(SameProtoAs(fa)
	     && this->IsConnected() && !((Ftp*)fa)->IsConnected());
   }

   void	 Connect(const char *h,const char *p);
   void	 ConnectVerify();

   int   Read(void *buf,int size);
   int   Write(const void *buf,int size);
   int   Buffered();
   void  Close();

	 // When you are putting a file, call SendEOT to terminate
	 // transfer and then call StoreStatus until OK or error.
   int	 SendEOT();
   int	 StoreStatus();

   int	 Do();
   void  Disconnect();

   void	 SetFlag(int flag,bool val);
   int	 GetFlag(int flag) { return flags&flag; }

   static time_t ConvertFtpDate(const char *);

   const char *CurrentStatus();

   int	 Done();

   enum flag_mask
   {
      SYNC_MODE=1,
      SYNC_WAIT=2,
      NOREST_MODE=4,
      IO_FLAG=8,
      DOSISH_PATH=16,
      PASSIVE_MODE=32,

      MODES_MASK=SYNC_MODE|PASSIVE_MODE|DOSISH_PATH
   };

   void CopyOptions(FileAccess *fa)
   {
      if(SameProtoAs(fa))
      {
	 flags=(flags&~MODES_MASK)|(((Ftp*)fa)->flags&MODES_MASK);
	 if(((Ftp*)fa)->try_time==0)
	    try_time=0;
      }
   }

   void Reconfig(const char *name=0);
   void Cleanup(bool all);

   ListInfo *MakeListInfo();
   Glob *MakeGlob(const char *pattern);
   DirList *MakeDirList(ArgV *args);
};

#endif /* FTPCLASS_H */
