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

#include <config.h>

#include "modconfig.h"

#include "trio.h"
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "CmdExec.h"
#include "GetJob.h"
#include "CatJob.h"
#include "LsCache.h"
#include "mgetJob.h"
#include "mkdirJob.h"
#include "rmJob.h"
#include "SysCmdJob.h"
#include "mvJob.h"
#include "pgetJob.h"
#include "SleepJob.h"
#include "FindJob.h"
#include "FindJobDu.h"
#include "ChmodJob.h"
#include "CopyJob.h"

#include "misc.h"
#include "alias.h"
#include "netrc.h"
#include "url.h"
#include "GetPass.h"
#include "SignalHook.h"
#include "FileFeeder.h"
#include "xalloca.h"
#include "bookmark.h"
#include "log.h"
#include "module.h"
#include "getopt.h"
#include "FileCopy.h"
#include "DummyProto.h"
#include "QueueFeeder.h"
#include "lftp_rl.h"
#include "FileSetOutput.h"

#include "confpaths.h"

#define MINUTE (60)
#define HOUR   (60*MINUTE)
#define DAY    (24*HOUR)

Bookmark lftp_bookmarks;
History	 cwd_history;

CMD(alias); CMD(anon);   CMD(cd);      CMD(debug);	CMD(du);
CMD(exit);  CMD(get);    CMD(help);    CMD(jobs);
CMD(kill);  CMD(lcd);    CMD(ls);      CMD(cls);
CMD(open);  CMD(pwd);    CMD(set);
CMD(shell); CMD(source); CMD(user);    CMD(rm);
CMD(wait);  CMD(subsh);  CMD(mirror);
CMD(mv);    CMD(cat);    CMD(cache);
CMD(mkdir); CMD(scache); CMD(mrm);
CMD(ver);   CMD(close);  CMD(bookmark);CMD(lftp);
CMD(echo);  CMD(suspend);CMD(sleep);
CMD(at);    CMD(find);   CMD(command); CMD(module);
CMD(lpwd);  CMD(glob);	 CMD(chmod);   CMD(queue);
CMD(repeat);CMD(get1);   CMD(history);

#ifdef MODULE_CMD_MIRROR
# define cmd_mirror 0
#endif
#ifdef MODULE_CMD_SLEEP
# define cmd_sleep  0
# define cmd_at     0
# define cmd_repeat 0
#endif

#define S "\001"

const struct CmdExec::cmd_rec CmdExec::static_cmd_table[]=
{
   {"!",       cmd_shell,  N_("!<shell-command>"),
	 N_("Launch shell or shell command\n")},
   {"(",       cmd_subsh,  N_("(commands)"),
	 N_("Group commands together to be executed as one command\n"
	 "You can launch such a group in background\n")},
   {"?",       cmd_help,   0,"help"},
   {"alias",   cmd_alias,  N_("alias [<name> [<value>]]"),
	 N_("Define or undefine alias <name>. If <value> omitted,\n"
	 "the alias is undefined, else is takes the value <value>.\n"
         "If no argument is given the current aliases are listed.\n")},
   {"anon",    cmd_anon,   "anon",
	 N_("anon - login anonymously (by default)\n")},
   {"at",      cmd_at},
   {"bookmark",cmd_bookmark,N_("bookmark [SUBCMD]"),
	 N_("bookmark command controls bookmarks\n\n"
	 "The following subcommands are recognized:\n"
	 "  add <name> [<loc>] - add current place or given location to bookmarks\n"
	 "                       and bind to given name\n"
	 "  del <name>         - remove bookmark with the name\n"
	 "  edit               - start editor on bookmarks file\n"
	 "  import <type>      - import foreign bookmarks\n"
	 "  list               - list bookmarks (default)\n")},
   {"bye",     cmd_exit,   0,"exit"},
   {"cache",   cmd_cache,  N_("cache [SUBCMD]"),
	 N_("cache command controls local memory cache\n\n"
	 "The following subcommands are recognized:\n"
	 "  stat        - print cache status (default)\n"
	 "  on|off      - turn on/off caching\n"
	 "  flush       - flush cache\n"
	 "  size <lim>  - set memory limit, -1 means unlimited\n"
	 "  expire <Nx> - set cache expiration time to N seconds (x=s)\n"
	 "                minutes (x=m) hours (x=h) or days (x=d)\n")},
   {"cat",     cmd_cat,    N_("cat [-b] <files>"),
	 N_("cat - output remote files to stdout (can be redirected)\n"
	 " -b  use binary mode (ascii is the default)\n")},
   {"cd",      cmd_cd,     N_("cd <rdir>"),
	 N_("Change current remote directory to <rdir>. The previous remote directory\n"
	 "is stored as `-'. You can do `cd -' to change the directory back.\n"
	 "The previous directory for each site is also stored on disk, so you can\n"
	 "do `open site; cd -' even after lftp restart.\n")},
   {"chmod",   cmd_chmod,   N_("chmod mode file..."), 0},
   {"close",   cmd_close,   "close [-a]",
	 N_("Close idle connections. By default only with current server.\n"
	 " -a  close idle connections with all servers\n")},
   {"cls",     cmd_cls,     N_("[re]cls [opts] [path/][pattern]"),
	 N_("List remote files. You can redirect output of this command to file\n"
	    "or via pipe to external command.\n"
	    "\n"
	    /* note: I've tried to keep options which are likely to be always
	     * turned on (via cmd:cls-default, etc) capital, to leave lowercase
	     * available for options more commonly used manually.  -s/-S is an
	     * exception; they both seem to be options used manually, so I made
	     * them align with GNU ls options. */
	    " -1                   - single-column output\n"
	    " -B, --basename       - show basename of files only\n"
	    " -F, --classify       - append indicator (one of /@) to entries\n"
	    " -l, --long           - use a long listing format\n"
	    " -q, --quiet          - don't show status\n"
	    " -s, --size           - print size of each file\n"
	    "     --filesize       - if printing size, only print size for files\n"
	    " -i, --nocase         - case-insensitive pattern matching\n"
	    " -I, --sortnocase     - sort names case-insensitively\n"
	    " -D, --dirsfirst      - list directories first\n"
	    "     --sort=OPT       - \"name\", \"size\"\n"
	    " -S                   - sort by file size\n"
	    "\n"
	    "By default, cls output is cached, to see new listing use `recls' or\n"
	    "`cache flush'.\n"
	    "\n"
	    "The variables cls-default and cls-completion-default can be used to\n"
	    "specify defaults for cls listings and completion listings, respectively.\n"
	    "For example, to make completion listings show file sizes, set\n"
	    "cls-completion-default to \"-s\".\n"
	    "\n"
	    /* FIXME: poorly worded. another explanation of --filesize: if a person
	     * wants to only see file sizes for files (not dirs) when he uses -s,
	     * add --filesize; it won't have any effect unless -s is also used, so
	     * it can be enabled all the time. (that's also poorly worded, and too
	     * long.) */
	    "Tips: Use --filesize and -D to pack the listing better.  If you don't\n"
	    "always want to see file sizes, --filesize in cls-default will affect the\n"
	    "-s flag on the commandline as well.  All flags work in\n"
	    "cls-completion-default.  -i in cls-completion-default makes filename\n"
	    "completion case-insensitive."
	    "\n"
	    "Usage note: directory names must have a trailing slash; \"cls pub/\" to\n"
	    "be recognized as a directory.\n"
	   )},
   {"connect", cmd_open,   0,"open"},
   {"command", cmd_command},
   {"debug",   cmd_debug,  N_("debug [<level>|off] [-o <file>]"),
	 N_("Set debug level to given value or turn debug off completely.\n"
	 " -o <file>  redirect debug output to the file.\n")},
   {"du",      cmd_du,  N_("du [options] <dirs>"),
	 N_("Summarize disk usage.\n"
	 " -a, --all             write counts for all files, not just directories\n"
	 " -b, --bytes           print size in bytes\n"
	 " -c, --total           produce a grand total\n"
	 " -d, --max-depth=N     print the total for a directory (or file, with --all)\n"
	 "                       only if it is N or fewer levels below the command\n"
	 "                       line argument;  --max-depth=0 is the same as\n"
	 "                       --summarize\n"
	 " -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n"
	 " -H, --si              likewise, but use powers of 1000 not 1024\n"
	 " -k, --kilobytes       like --block-size=1024\n"
	 " -m, --megabytes       like --block-size=1048576\n"
	 " -S, --separate-dirs   do not include size of subdirectories\n"
	 " -s, --summarize       display only a total for each argument\n"
	 "     --exclude=PAT     exclude files that match PAT\n")},
   {"echo",    cmd_echo,   0},
   {"exit",    cmd_exit,   N_("exit [<code>|bg]"),
	 N_("exit - exit from lftp or move to background if jobs are active\n\n"
	 "If no jobs active, the code is passed to operating system as lftp\n"
	 "termination status. If omitted, exit code of last command is used.\n"
	 "`bg' forces moving to background if cmd:move-background is false.\n")},
   {"fg",      cmd_wait,   0,"wait"},
   {"find",    cmd_find,0,
	 N_("Usage: find [OPTS] [directory]\n"
	 "Print contents of specified directory of current directory recursively.\n"
	 "Directories in the list are marked with trailing slash.\n"
	 "You can redirect output of this command.\n"
	 " -d, --maxdepth=LEVELS  Descend at most LEVELS of directories.\n")},
   {"get",     cmd_get,    N_("get [OPTS] <rfile> [-o <lfile>]"),
	 N_("Retrieve remote file <rfile> and store it to local file <lfile>.\n"
	 " -o <lfile> specifies local file name (default - basename of rfile)\n"
	 " -c  continue, reget\n"
	 " -E  delete remote files after successful transfer\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"get1",    cmd_get1,   0,0},
   {"glob",    cmd_glob,   N_("glob [OPTS] <cmd> <args>"),
	 N_(
	 "Expand wildcards and run specified command.\n"
	 "Options can be used to expand wildcards to list of files, directories,\n"
	 "or both types. Type selection is not very reliable and depends on server.\n"
	 "If entry type cannot be determined, it will be included in the list.\n"
	 " -f  plain files (default)\n"
	 " -d  directories\n"
	 " -a  all types\n")},
   {"help",    cmd_help,   N_("help [<cmd>]"),
	 N_("Print help for command <cmd>, or list of available commands\n")},
   {"history", cmd_history,N_("history -w file|-r file|-c|-l [cnt]"),
	 N_(" -w <file> Write history to file.\n"
	 " -r <file> Read history from file; appends to current history.\n"
	 " -c  Clear the history.\n"
	 " -l  List the history (default).\n"
	 "Optional argument cnt specifies the number of history lines to list,\n"
	 "or \"all\" to list all entries.\n")},
   {"jobs",    cmd_jobs,   "jobs [-v]",
	 N_("List running jobs. -v means verbose, several -v can be specified.\n")},
   {"kill",    cmd_kill,   N_("kill all|<job_no>"),
	 N_("Delete specified job with <job_no> or all jobs\n")},
   {"lcd",     cmd_lcd,    N_("lcd <ldir>"),
	 N_("Change current local directory to <ldir>. The previous local directory\n"
	 "is stored as `-'. You can do `lcd -' to change the directory back.\n")},
   {"lftp",    cmd_lftp,   N_("lftp [OPTS] <site>"),
	 N_("`lftp' is the first command executed by lftp after rc files\n"
	 " -f <file>           execute commands from the file and exit\n"
	 " -c <cmd>            execute the commands and exit\n"
	 " --help              print this help and exit\n"
	 " --version           print lftp version and exit\n"
	 "Other options are the same as in `open' command\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"lpwd",    cmd_lpwd},
   {"login",   cmd_user,   0,"user"},
   {"ls",      cmd_ls,	    N_("ls [<args>]"),
	 N_("List remote files. You can redirect output of this command to file\n"
	 "or via pipe to external command.\n"
	 "By default, ls output is cached, to see new listing use `rels' or\n"
	 "`cache flush'.\n"
	 "See also `help cls'.\n")},
   {"mget",    cmd_get,	   N_("mget [OPTS] <files>"),
	 N_("Gets selected files with expanded wildcards\n"
	 " -c  continue, reget\n"
	 " -d  create directories the same as in file names and get the\n"
	 "     files into them instead of current directory\n"
	 " -E  delete remote files after successful transfer\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"mirror",  cmd_mirror, N_("mirror [OPTS] [remote [local]]"),
	 N_("\nMirror specified remote directory to local directory\n\n"
	 " -c, --continue         continue a mirror job if possible\n"
	 " -e, --delete           delete files not present at remote site\n"
	 " -s, --allow-suid       set suid/sgid bits according to remote site\n"
	 " -n, --only-newer       download only newer files (-c won't work)\n"
	 " -r, --no-recursion     don't go to subdirectories\n"
	 " -p, --no-perms         don't set file permissions\n"
	 "     --no-umask         don't apply umask to file modes\n"
	 " -R, --reverse          reverse mirror (put files)\n"
	 " -L, --dereference      download symbolic links as files\n"
	 " -N, --newer-than FILE  download only files newer than the file\n"
	 " -P, --parallel[=N]     download N files in parallel\n"
	 " -i RX, --include RX    include matching files\n"
	 " -x RX, --exclude RX    exclude matching files\n"
	 "                        RX is extended regular expression\n"
	 " -t Nx, --time-prec Nx  set time precision to N seconds (x=s)\n"
	 "                        minutes (x=m) hours (x=h) or days (x=d)\n"
	 "                        default - mirror:time-precision setting\n"
	 " -T Nx, --loose-time-prec  set time precision for imprecise time\n"
	 "                        default - mirror:loose-time-precision\n"
	 " -v, --verbose[=N]      verbose operation\n"
	 "     --use-cache        use cached directory listings\n"
	 "\n"
	 "When using -R, the first directory is local and the second is remote.\n"
	 "If the second directory is omitted, basename of first directory is used.\n"
	 "If both directories are omitted, current local and remote directories are used.\n"
	 )},
   {"mkdir",   cmd_mkdir,  N_("mkdir [-p] <dirs>"),
	 N_("Make remote directories\n"
	 " -p  make all levels of path\n")},
   {"module",  cmd_module, N_("module name [args]"),
	 N_("Load module (shared object). The module should contain function\n"
	 "   void module_init(int argc,const char *const *argv)\n"
	 "If name contains a slash, then the module is searched in current\n"
	 "directory, otherwise in directories specified by setting module:path.\n")},
   {"more",    cmd_cat,    N_("more <files>"),
	 N_("Same as `cat <files> | more'. if PAGER is set, it is used as filter\n")},
   {"mput",    cmd_get,	   N_("mput [OPTS] <files>"),
	 N_("Upload files with wildcard expansion\n"
	 " -c  continue, reput\n"
	 " -d  create directories the same as in file names and put the\n"
	 "     files into them instead of current directory\n"
	 " -E  delete local files after successful transfer (dangerous)\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"mrm",     cmd_mrm,    N_("mrm <files>"),
	 N_("Removes specified files with wildcard expansion\n")},
   {"mv",      cmd_mv,	    N_("mv <file1> <file2>"),
	 N_("Rename <file1> to <file2>\n")},
   {"nlist",   cmd_ls,     N_("[re]nlist [<args>]"),
	 N_("List remote file names.\n"
	 "By default, nlist output is cached, to see new listing use `renlist' or\n"
	 "`cache flush'.\n")},
   {"open",    cmd_open,   N_("open [OPTS] <site>"),
	 N_("Select a server, URL or bookmark\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"pget",    cmd_get,    N_("pget [OPTS] <rfile> [-o <lfile>]"),
	 N_("Gets the specified file using several connections. This can speed up transfer,\n"
	 "but loads the net heavily impacting other users. Use only if you really\n"
	 "have to transfer the file ASAP, or some other user may go mad :)\n"
	 "\nOptions:\n"
	 " -n <maxconn>  set maximum number of connections (default 5)\n")},
   {"put",     cmd_get,    N_("put [OPTS] <lfile> [-o <rfile>]"),
	 N_("Upload <lfile> with remote name <rfile>.\n"
	 " -o <rfile> specifies remote file name (default - basename of lfile)\n"
	 " -c  continue, reput\n"
	 "     it requires permission to overwrite remote files\n"
	 " -E  delete local files after successful transfer (dangerous)\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"pwd",     cmd_pwd,    "pwd [-p]",
	 N_("Print current remote URL.\n"
	 " -p  show password\n")},
   {"queue",   cmd_queue,  N_("queue [OPTS] [<cmd>]"),
	 N_("\n"
	 "       queue [-n num] <command>\n\n"
	 "Add the command to queue for current site. Each site has its own command\n"
	 "queue. `-n' adds the command before the given item in the queue. It is\n"
	 "possible to queue up a running job by using command `queue wait <jobno>'.\n"
	 "\n"
	 "       queue --delete|-d [index or wildcard expression]\n\n"
	 "Delete one or more items from the queue. If no argument is given, the last\n"
	 "entry in the queue is deleted.\n"
	 "\n"
	 "       queue --move|-m <index or wildcard expression> [index]\n\n"
	 "Move the given items before the given queue index, or to the end if no\n"
	 "destination is given.\n"
	 )},
   {"quit",    cmd_exit,   0,"exit"},
   {"quote",   cmd_ls,	   N_("quote <cmd>"),
	 N_("Send the command uninterpreted. Use with caution - it can lead to\n"
	 "unknown remote state and thus will cause reconnect. You cannot\n"
	 "be sure that any change of remote state because of quoted command\n"
	 "is solid - it can be reset by reconnect at any time.\n")},
   {"recls",    cmd_cls,   0,
	 N_("recls [<args>]"
	 "Same as `cls', but don't look in cache\n")},
   {"reget",   cmd_get,    0,
	 N_("Usage: reget [OPTS] <rfile> [-o <lfile>]"
	 "Same as `get -c'\n")},
   {"rels",    cmd_ls,	    0,
	 N_("Usage: rels [<args>]"
	    "Same as `ls', but don't look in cache\n")},
   {"renlist", cmd_ls,	    0,
	 N_("Usage: renlist [<args>]"
	 "Same as `nlist', but don't look in cache\n")},
   {"repeat",  cmd_repeat, N_("repeat [delay] [command]"),
	 N_("Repeat specified command with a delay between iterations.\n"
	 "Default delay is one second, default command is empty.\n")},
   {"reput",   cmd_get,    0,
	 N_("Usage: reput <lfile> [-o <rfile>]"
	 "Same as `put -c'\n")},
   {"rm",      cmd_rm,	    N_("rm [-r] [-f] <files>"),
	 N_("Remove remote files\n"
	    " -r  recursive directory removal, be careful\n"
	    " -f  work quietly\n")},
   {"rmdir",   cmd_rm,	    N_("rmdir [-f] <dirs>"),
	 N_("Remove remote directories\n")},
   {"scache",  cmd_scache, N_("scache [<session_no>]"),
	 N_("List cached sessions or switch to specified session number\n")},
   {"set",     cmd_set,    N_("set [OPT] [<var> [<val>]]"),
	 N_("Set variable to given value. If the value is omitted, unset the variable.\n"
	 "Variable name has format ``name/closure'', where closure can specify\n"
	 "exact application of the setting. See lftp(1) for details.\n"
         "If set is called with no variable then only altered settings are listed.\n"
	 "It can be changed by options:\n"
   	 " -a  list all settings, including default values\n"
	 " -d  list only default values, not necessary current ones\n")},
   {"shell",   cmd_shell,  0,"!"},
   {"site",    cmd_ls,	   N_("site <site_cmd>"),
	 N_("Execute site command <site_cmd> and output the result\n"
	 "You can redirect its output\n")},
   {"sleep",   cmd_sleep, 0,
	 N_("Usage: sleep <time>[unit]\n"
	 "Sleep for given amount of time. The time argument can be optionally\n"
	 "followed by unit specifier: d - days, h - hours, m - minutes, s - seconds.\n"
	 "By default time is assumed to be seconds.\n")},
   {"source",  cmd_source, N_("source <file>"),
	 N_("Execute commands recorded in file <file>\n")},
   {"suspend", cmd_suspend},
   {"user",    cmd_user,   N_("user <user|URL> [<pass>]"),
	 N_("Use specified info for remote login. If you specify URL, the password\n"
	 "will be cached for future usage.\n")},
   {"version", cmd_ver,    "version",
	 N_("Shows lftp version\n")},
   {"wait",    cmd_wait,   N_("wait [<jobno>]"),
	 N_("Wait for specified job to terminate. If jobno is omitted, wait\n"
	 "for last backgrounded job.\n")},
   {"zcat",    cmd_cat,    N_("zcat <files>"),
	 N_("Same as cat, but filter each file through zcat\n")},
   {"zmore",   cmd_cat,    N_("zmore <files>"),
	 N_("Same as more, but filter each file through zcat\n")},
   {"bzcat",    cmd_cat,    0,
	 N_("Same as cat, but filter each file through bzcat\n")},
   {"bzmore",   cmd_cat,    0,
	 N_("Same as more, but filter each file through bzcat\n")},

   {NULL,NULL}
};

// returns:
//    0 - no match
//    1 - found, if *res==0 then ambiguous
static
int find_command(const char *unprec_name,const char * const *names,
	         const char **res)
{
   const char *match=0;
   for( ; *names; names++)
   {
      const char *s,*u;
      for(s=*names,u=unprec_name; *s && *u==*s; s++,u++)
	 ;
      if(*s && !*u)
      {
	 if(match)
	 {
	    *res=0;
	    return 1;
	 }
	 match=*names;
      }
      else if(!*s && !*u)
      {
	 *res=*names;
	 return 1;
      }
   }
   if(match)
   {
      *res=match;
      return 1;
   }
   *res=0;
   return 0;
}

Job *CmdExec::builtin_lcd()
{
   if(args->count()==1)
      args->Append("~");

   if(args->count()!=2)
   {
      eprintf(_("Usage: %s local-dir\n"),args->getarg(0));
      return 0;
   }
   const char *cd_to=args->getarg(1);

   if(!strcmp(cd_to,"-"))
   {
      if(old_lcwd)
	 cd_to=old_lcwd;
   }

   cd_to=expand_home_relative(cd_to);

   RestoreCWD();

   int res=chdir(cd_to);
   if(res==-1)
   {
      perror(cd_to);
      exit_code=1;
      return 0;
   }

   xfree(old_lcwd);
   old_lcwd=cwd;
   cwd=0;

   SaveCWD();  // this allocates cwd again

   if(interactive)
      eprintf(_("lcd ok, local cwd=%s\n"),cwd?cwd:"?");

   exit_code=0;
   return 0;
}

Job *CmdExec::builtin_cd()
{
   if(args->count()==1)
      args->Append("~");

   if(args->count()!=2)
   {
      // xgettext:c-format
      eprintf(_("Usage: cd remote-dir\n"));
      return 0;
   }

   const char *dir=args->getarg(1);

   if(url::is_url(dir))
      return builtin_open();

   if(!strcmp(dir,"-"))
   {
      dir=cwd_history.Lookup(session);
      if(!dir)
      {
	 eprintf(_("%s: no old directory for this site\n"),args->a0());
	 return 0;
      }
      args->setarg(1,dir); // for status line
   }

   xfree(old_cwd);
   old_cwd=xstrdup(session->GetCwd());

   if (!verify_path || background)
   {
      session->Chdir(dir,false);
      exit_code=0;
      return 0;
   }
   session->Chdir(dir);
   Roll(session);
   builtin=BUILTIN_CD;
   return this;
}

Job *CmdExec::builtin_exit()
{
   bool bg=false;
   int code=prev_exit_code;
   if(args->count()>=2)
   {
      const char *a=args->getarg(1);
      if(!strcmp(a,"bg"))
	 bg=true;
      else if(sscanf(a,"%i",&code)!=1)
      {
	 eprintf(_("Usage: %s [<exit_code>]\n"),args->a0());
	 return 0;
      }
   }
   // Note: one job is this CmdExec.
   if(!bg && top_level
   && !ResMgr::QueryBool("cmd:move-background",0) && NumberOfJobs()>1)
   {
      eprintf(_(
	 "There are running jobs and `cmd:move-background' is not set.\n"
	 "Use `exit bg' to force moving to background or `kill all' to terminate jobs.\n"
      ));
      return 0;
   }
   while(!Done())
      RemoveFeeder();
   exit_code=prev_exit_code=code;
   return 0;
}

CmdFeeder *lftp_feeder=0;
Job *CmdExec::builtin_lftp()
{
   int c;
   const char *cmd=0;
   char *acmd;
   static struct option lftp_options[]=
   {
      {"help",no_argument,0,'h'},
      {"version",no_argument,0,'v'},
      {0,0,0,0}
   };

   args->rewind();
   opterr=false;
   while((c=args->getopt_long("+f:c:vh",lftp_options,0))!=EOF)
   {
      switch(c)
      {
      case('h'):
	 cmd="help lftp;";
	 break;
      case('v'):
	 cmd="version;";
	 break;
      case('f'):
	 acmd=string_alloca(20+2*strlen(optarg));
	 strcpy(acmd,"source \"");
	 unquote(acmd+strlen(acmd),optarg);
	 strcat(acmd,"\";");
	 cmd=acmd;
	 break;
      case('c'):
	 acmd=string_alloca(4+strlen(optarg));
	 sprintf(acmd,"%s\n\n",optarg);
	 cmd=acmd;
	 break;
      }
   }
   opterr=true;

   if(cmd)
      PrependCmd(cmd);

   if(Done() && lftp_feeder)  // no feeder and no commands
   {
      SetCmdFeeder(lftp_feeder);
      lftp_feeder=0;
      SetInteractive(isatty(0));
      FeedCmd("||command exit\n");   // if the command fails, quit
   }

   if(!cmd)
   {
      /* if no lftp-specific options were found, call open */
      return builtin_open();
   }
   return 0;
}

Job *CmdExec::builtin_open()
{
   ReuseSavedSession();

   bool	 debug=false;
   char	 *port=NULL;
   char	 *host=NULL;
   char  *path=NULL;
   char	 *user=NULL;
   char	 *pass=NULL;
   int	 c;
   NetRC::Entry *nrc=0;
   char  *cmd_to_exec=0;
   char  *op=args->a0();
   bool insecure=false;
   bool no_bm=false;

   args->rewind();
   while((c=args->getopt("u:p:e:dB"))!=EOF)
   {
      switch(c)
      {
      case('p'):
	 port=optarg;
	 break;
      case('u'):
         user=optarg;
         pass=strchr(optarg,',');
	 if(pass==NULL)
	    pass=strchr(optarg,' ');
	 if(pass==NULL)
	    pass=strchr(optarg,':');
	 if(pass==NULL)
   	    break;
	 *pass=0;
	 pass++;
	 insecure=true;
         break;
      case('d'):
	 debug=true;
	 break;
      case('e'):
	 cmd_to_exec=optarg;
	 break;
      case('B'):
	 no_bm=true;
	 break;
      case('?'):
	 if(!strcmp(op,"lftp"))
	    eprintf(_("Try `%s --help' for more information\n"),op);
	 else
	    eprintf(_("Usage: %s [-e cmd] [-p port] [-u user[,pass]] <host|url>\n"),
	       op);
	 return 0;
      }
   }

   if(optind<args->count())
      host=args->getarg(optind++);

   ParsedURL *url=0;

   const char *bm=0;

   if(cmd_to_exec)
      PrependCmd(cmd_to_exec);

   if(!no_bm && host && (bm=lftp_bookmarks.Lookup(host))!=0)
   {
      char *cmd=string_alloca(5+3+3+xstrlen(user)*2+1+xstrlen(pass)*2+3+xstrlen(port)*2+strlen(bm)*2+6+1);
      strcpy(cmd,"open -B ");
      if(user)
      {
	 strcat(cmd,"-u \"");
	 unquote(cmd+strlen(cmd),user);
	 if(pass)
	 {
	    strcat(cmd,",");
	    unquote(cmd+strlen(cmd),pass);
	 }
	 strcat(cmd,"\" ");
      }
      if(port)
      {
	 strcat(cmd,"-p \"");
	 unquote(cmd+strlen(cmd),port);
	 strcat(cmd,"\" ");
      }

      strcat(cmd,bm);

      if(background)
	 strcat(cmd," &\n");
      strcat(cmd,";\n");

      PrependCmd(cmd);
   }
   else
   {
      if(host && host[0])
      {
	 url=new ParsedURL(host);

	 const ParsedURL &uc=*url;
	 if(uc.host && uc.host[0])
	 {
	    cwd_history.Set(session,session->GetCwd());

	    FileAccess *new_session=0;

	    const char *p=uc.proto;
	    if(!p)
	       p=ResMgr::Query("cmd:default-protocol",uc.host);
	    if(!p)
	       p="ftp";
	    new_session=FileAccess::New(p,uc.host);
	    if(!new_session)
	    {
	       eprintf("%s: %s%s\n",args->a0(),p,
			_(" - not supported protocol"));
	       return 0;
	    }

	    saved_session=session;
	    session=0;
	    ChangeSession(new_session);

	    if(uc.user && !user)
	       user=uc.user;
	    if(uc.pass && !pass)
	    {
	       pass=uc.pass;
	       insecure=true;
	    }
	    host=uc.host;
	    if(uc.port!=0 && port==0)
	       port=uc.port;
	    if(uc.path && !path)
	       path=uc.path;
	 }

	 if(!pass)
	 {
	    nrc=NetRC::LookupHost(host);
	    if(nrc)
	    {
	       if((!uc.proto && nrc->user && !user)
	       || (nrc->user && user && !strcmp(nrc->user,user) && !pass))
	       {
		  user=nrc->user;
		  if(nrc->pass)
		     pass=nrc->pass;
	       }
	    }
	 }
      }
      else if(host && !host[0])
      {
	 ChangeSession(new DummyProto);
      }
      if(host && host[0])
	 session->Connect(host,port);
      if(user)
      {
	 if(!pass)
	    pass=GetPass(_("Password: "));
	 if(!pass)
	    eprintf(_("%s: GetPass() failed -- assume anonymous login\n"),
	       args->getarg(0));
	 else
	 {
	    session->Login(user,0);
	    // assume the new password is the correct one.
	    session->SetPasswordGlobal(pass);
	    session->InsecurePassword(insecure && !no_bm);
	 }
      }
      if(host && host[0])
      {
	 if(verify_host && !background)
	 {
	    session->ConnectVerify();
	    builtin=BUILTIN_OPEN;
	 }
      }
      if(nrc)
	 delete nrc;
   } // !bookmark

   if(path)
   {
      const char *old=cwd_history.Lookup(session);
      if(old)
	 session->Chdir(old,false);

      char *s=string_alloca(strlen(path)*4+40);
      strcpy(s,"&& cd \"");
      unquote(s+strlen(s),path);
      strcat(s,"\"");
      if(background)
	 strcat(s,"&");
      strcat(s,"\n");
      PrependCmd(s);
   }

   if(debug)
      PrependCmd("debug\n");

   if(url)
      delete url;

   Reconfig(0);

   if(builtin==BUILTIN_OPEN)
      return this;

   ReuseSavedSession();

   exit_code=0;
   return 0;
}

Job *CmdExec::builtin_restart()
{
   builtin=BUILTIN_EXEC_RESTART;
   return this;
}

Job *CmdExec::builtin_glob()
{
   const char *op=args->a0();
   bool files_only=true;
   bool dirs_only=false;
   int opt;

   while((opt=args->getopt("+adf"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 files_only=dirs_only=false;
	 break;
      case('d'):
	 dirs_only=true;
	 files_only=false;
	 break;
      case('f'):
	 files_only=true;
	 dirs_only=false;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   while(args->getindex()>1)
      args->delarg(1);	   // remove options.
   if(args->count()<2)
   {
      eprintf(_("Usage: %s [OPTS] command args...\n"),op);
      return 0;
   }
   assert(args_glob==0 && glob==0);
   args_glob=new ArgV();
   args->rewind();
   args_glob->Append(args->getnext());
   const char *pat=args->getnext();
   if(!pat)
   {
      delete args_glob;
      args_glob=0;
      args->rewind();
      return cmd_command(this);
   }
   glob=new GlobURL(session,pat);
   if(dirs_only)
      glob->glob->DirectoriesOnly();
   if(files_only)
      glob->glob->FilesOnly();
   builtin=BUILTIN_GLOB;
   return this;
}

Job *CmdExec::builtin_queue()
{
   static struct option queue_options[]=
   {
      {"move",required_argument,0,'m'},
      {"delete",optional_argument,0,'d'},
      {0,0,0,0}
   };
   enum { ins, del, move } mode = ins;

   const char *arg = NULL;
   /* position to insert at (ins only) */
   int pos = -1; /* default to the end */

   args->rewind();
   int opt;

   exit_code=1; // more failure exits than success exits, so set success explicitely

   while((opt=args->getopt_long("+dm:n:w",queue_options,0))!=EOF)
   {
      switch(opt)
      {
      case 'n':
	 if(!isdigit((unsigned char)optarg[0]))
	 {
	    eprintf(_("%s: -n: Number expected. "), args->a0());
	    goto err;
	 }
	 /* make offsets match the jobs output (starting at 1) */
	 pos = atoi(optarg) - 1;
	 break;

      case 'm':
	 mode = move;
	 arg = optarg;
	 break;

      case 'd':
	 mode = del;
	 break;

      case '?':
	 err:
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }

   const int args_remaining = args->count() - args->getindex();
   switch(mode) {
      case ins: {
	 if(args_remaining==0)
	    goto err;

	 CmdExec *queue=GetQueue();

	 char *cmd=NULL;
	 if(args_remaining == 1)
		 cmd=args->Combine(args->getindex());
	 else
		 cmd=args->CombineQuoted(args->getindex());

	 queue->has_queue->QueueCmd(cmd, session->GetCwd(), cwd, pos);
	 xfree(cmd);

	 last_bg=queue->jobno;
	 exit_code=0;
      }
      break;

      case del: {
         /* Accept:
	  * queue -d (delete the last job)
	  * queue -d 1  (delete entry 1)
	  * queue -d "get" (delete all *get*)
	  *
	  * We want an optional argument, but don't use getopt ::, since
	  * that'll disallow the space between arguments, which we want. */
         arg = args->getarg(args->getindex());

	 CmdExec *queue=GetQueue(false);
	 if(!queue) {
	    eprintf(_("%s: No queue is active.\n"), args->a0());
	    break;
	 }

	 if(!arg)
	    queue->has_queue->DelJob(-1); /* delete the last job */
	 else if(isdigit(arg[0]))
	    queue->has_queue->DelJob(atoi(arg)-1);
	 else
	    queue->has_queue->DelJob(arg);

	 exit_code=0;
      }
      break;

      case move: {
         /* Accept:
	  * queue -m 1 2  (move entry 1 to position 2)
	  * queue -m "*get*" 1
	  * queue -m 3    (move entry 3 to the end) */
         char *a1 = args->getarg(args->getindex());
	 if(a1 && !isdigit(a1[0])) {
	    eprintf(_("%s: -m: Number expected as second argument. "), args->a0());
	    goto err;
	 }
	 /* default to moving to the end */
	 int to = a1? atoi(a1)-1:-1;

	 CmdExec *queue=GetQueue(false);
	 if(!queue) {
	    eprintf(_("%s: No queue is active.\n"), args->a0());
	    break;
	 }

	 if(isdigit(arg[0])) {
	    queue->has_queue->MoveJob(atoi(arg)-1, to);
	    break;
	 }

	 queue->has_queue->MoveJob(arg, to);
	 exit_code=0;
      }
      break;
   }

   return 0;
}

Job *CmdExec::builtin_queue_edit()
{
   CmdExec *queue=GetQueue();

   if(queue->feeder) {
      eprintf(_("Can only edit plain queues.\n"));
      return 0;
   }

   const char *home=getenv("HOME");
   if(home==0)
      home="";
   char *q_file = (char *) xmalloc(strlen(home)+32);
   /* if we have $HOME, use $HOME/.lftp/file; otherwise just file */
   sprintf(q_file, "%s%slftp-queue-%i", home, home? "/.lftp/":"", (int) getpid());

   int q_fd=open(q_file,O_RDWR|O_CREAT,0600);
   if(q_fd==-1) {
      eprintf(_("Couldn't create temporary file `%s': %s.\n"), q_file, strerror(errno));
      xfree(q_file);
      return 0;
   }

   if(!queue->WriteCmds(q_fd)) {
      eprintf(_("%s: error writing %s: %s\n"), args->a0(), q_file, strerror(errno));

      /* abort without clearing the queue */
      close(q_fd);
      xfree(q_file);

      return 0;
   }

   queue->EmptyCmds();

   close(q_fd);

   /* empty the queue; when this command exits, it'll re-source the queue.
    * this also effectively "suspends" this queue until it's done editing. */

   {
      char *editcmd = (char *) xmalloc(strlen(q_file)*3+128);
      sprintf(editcmd, "shell \"/bin/sh -c 'exec ${EDITOR:-vi} %s'\"; queue -s %s; shell rm '%s'\n", q_file, q_file, q_file);
      PrependCmd(editcmd);
      xfree(editcmd);
   }

   xfree(q_file);

   return 0;
}

// below are only non-builtin commands
#define args	  (parent->args)
#define exit_code (parent->exit_code)
#define output	  (parent->output)
#define session	  (parent->session)
#define Clone()	  session->Clone()
#define eprintf	  parent->eprintf

CMD(lcd)
{
   return parent->builtin_lcd();
}

CMD(ls)
{
   bool nlist=false;
   bool re=false;
   int mode=FA::LIST;
   const char *op=args->a0();
   if(strstr(op,"nlist"))
      nlist=true;
   if(!strncmp(op,"re",2))
      re=true;
   if(!strcmp(op,"quote") || !strcmp(op,"site"))
   {
      if(args->count()<=1)
      {
	 eprintf(_("Usage: %s <cmd>\n"),op);
	 return 0;
      }
      nlist=true;
      mode=FA::QUOTE_CMD;
      if(!strcmp(op,"site"))
	 args->insarg(1,"SITE");
   }

   char *a=args->Combine(nlist?1:0);

   if(!nlist && args->count()==1 && parent->var_ls && parent->var_ls[0])
      args->Append(parent->var_ls);

   FileCopyPeer *src_peer=0;
   if(!nlist)
      src_peer=new FileCopyPeerDirList(Clone(),args);
   else
      src_peer=new FileCopyPeerFA(Clone(),a,mode);

   if(re)
      src_peer->NoCache();
   src_peer->SetDate(NO_DATE);
   src_peer->SetSize(NO_SIZE);
   FileCopyPeer *dst_peer=new FileCopyPeerFDStream(output,FileCopyPeer::PUT);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,false);
   c->DontCopyDate();
   c->LineBuffered();
   c->Ascii();

   CopyJob *j=new CopyJob(c,a,op);
   if(!output || output->usesfd(1))
      j->NoStatusOnWrite();
   xfree(a);
   output=0;
   if(!nlist)
      args=0;  // `ls' consumes args itself.

   return j;
}

/* this seems to belong here more than in FileSetOutput.cc ... */
const char *FileSetOutput::parse_argv(ArgV *a)
{
   static struct option cls_options[] = {
      {"basename",no_argument,0,'B'},
      {"classify",no_argument,0,'F'},
      {"long",no_argument,0,'l'},
      {"quiet",no_argument,0,'q'},
      {"size", no_argument,0,'s'},	/* show size */
      {"filesize", no_argument,0,0},	/* for files only */
      {"nocase", no_argument,0,'i'},
      {"sortnocase", no_argument,0,'I'},
      {"dirsfirst", no_argument,0,'D'},

      {"sort", required_argument,0, 0},
      {0, no_argument, 0, 'S'}, // sort by size
      {0,0,0,0}
   };

   a->rewind();
   int opt, longopt;
   while((opt=a->getopt_long(":1BFilqsDIS", cls_options, &longopt))!=EOF)
   {
      switch(opt) {
      case 0:
	 if(!strcmp(cls_options[longopt].name, "sort")) {
	    if(!strcasecmp(optarg, "name")) sort = FileSet::BYNAME;
	    else if(!strcasecmp(optarg, "size")) sort = FileSet::BYSIZE;
	    else return _("invalid argument for `--sort'");
	 } else if(!strcmp(cls_options[longopt].name, "filesize")) {
	    size_filesonly = true;
	 }
	 break;
      case('1'):
	 single_column = true;
         break;
      case('B'):
	 basenames = true;
         break;
      case('l'):
	 long_list();
         break;
      case('i'):
	 patterns_casefold = true;
         break;
      case('F'):
         classify=true;
         break;
      case('q'):
	 quiet = true;
         break;
      case('s'):
	 mode |= FileSetOutput::SIZE;
         break;
      case('D'):
	 sort_dirs_first = true;
	 break;
      case('I'):
	 sort_casefold = true;
	 break;
      case('S'):
	 sort = FileSet::BYSIZE;
	 break;

      default:
	 /* silly getopt won't give us its error instead of printing it, oh well.
	  * we only need to never print errors for completion and validation; for
	  * cls itself we could do it, but that'd be a special case ... */
	 return _("invalid option");
      }
   }

   while(a->getindex()>1)
      a->delarg(1);
   a->rewind();

   return NULL;
}

CMD(cls)
{
   exit_code=0;

   const char *op=args->a0();
   bool re=false;

   if(!output)
      output=new FDStream(1,"<stdout>");

   FileSetOutput fso;
   fso.config(output);

   if(!strncmp(op,"re",2))
      re=true;

   if(const char *err = fso.parse_argv(args)) {
      if(strcmp(err, "ERR"))
	      eprintf(_("%s: %s.\n"), op, err);
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }

   ArgV arg("", ResMgr::Query("cmd:cls-default", 0));
   fso.parse_argv(&arg);

   char *a=args->Combine(0);

   FileCopyPeer *src_peer=0;
   src_peer=new FileCopyPeerCLS(Clone(),args,fso);
   args=0;

   if(re)
      src_peer->NoCache();
   src_peer->SetDate(NO_DATE);
   src_peer->SetSize(NO_SIZE);
   FileCopyPeer *dst_peer=new FileCopyPeerFDStream(output,FileCopyPeer::PUT);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,false);
   c->DontCopyDate();
   c->LineBuffered();
   c->Ascii();

   CopyJob *j=new CopyJob(c,a,op);
   if(fso.quiet)
      j->NoStatus();
   else if(output->usesfd(1))
      j->NoStatusOnWrite();

   xfree(a);
   output=0;

   return j;
}

CMD(cat)
{
   const char *op=args->a0();
   int opt;
   bool ascii=false;
   bool auto_ascii=true;

   while((opt=args->getopt("+bau"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 ascii=true;
	 auto_ascii=false;
	 break;
      case('b'):
	 ascii=false;
	 auto_ascii=false;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   while(args->getindex()>1)
      args->delarg(1);
   args->rewind();
   if(args->count()<=1)
   {
      eprintf(_("Usage: %s [OPTS] files...\n"),op);
      return 0;
   }
   CatJob *j=new CatJob(Clone(),output,args);
   if(!auto_ascii)
   {
      if(ascii)
	 j->Ascii();
      else
	 j->Binary();
   }
   output=0;
   args=0;
   return j;
}

CMD(get)
{
   int opt;
   bool cont=false;
   const char *opts="+cEuaO:";
   const char *op=args->a0();
   ArgV	 *get_args=new ArgV(op);
   int n_conn=0;
   bool del=false;
   bool ascii=false;
   bool glob=false;
   bool make_dirs=false;
   bool reverse=false;
   char *output_dir=0;

   args->rewind();
   if(!strncmp(op,"re",2))
   {
      cont=true;
      opts="+EuaO:";
   }
   if(!strcmp(op,"pget"))
   {
      opts="+n:uO:";
      n_conn=-1;
   }
   else if(!strcmp(op,"put") || !strcmp(op,"reput"))
   {
      reverse=true;
   }
   else if(!strcmp(op,"mget"))
   {
      glob=true;
      opts="cEadO:";
   }
   else if(!strcmp(op,"mput"))
   {
      glob=true;
      opts="cEadO:";
      reverse=true;
   }
   while((opt=args->getopt(opts))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('n'):
	 if(!isdigit((unsigned char)optarg[0]))
	 {
	    eprintf(_("%s: -n: Number expected. "),op);
	    goto err;
	 }
	 n_conn=atoi(optarg);
	 break;
      case('E'):
	 del=true;
	 break;
      case('a'):
	 ascii=true;
	 break;
      case('d'):
	 make_dirs=true;
	 break;
      case('O'):
	 output_dir=optarg;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 delete get_args;
	 return 0;
      }
   }
   if(glob)
   {
      if(args->getcurr()==0)
      {
      file_name_missed:
	 // xgettext:c-format
	 eprintf(_("File name missed. "));
	 goto err;
      }
      delete get_args;
      output_dir=xstrdup(output_dir);  // save it from deleting.
      // remove options
      while(args->getindex()>1)
	 args->delarg(1);
      mgetJob *j=new mgetJob(Clone(),args,cont,make_dirs);
      if(reverse)
	 j->Reverse();
      if(del)
	 j->DeleteFiles();
      if(ascii)
	 j->Ascii();
      if(output_dir)
	 j->OutputDir(output_dir);
      args=0;
      return j;
   }
   args->back();
   const char *a=args->getnext();
   if(a==0)
      goto file_name_missed;
   while(a)
   {
      const char *src=a;
      const char *dst=0;
      a=args->getnext();
      if(a && !strcmp(a,"-o"))
      {
	 dst=args->getnext();
	 a=args->getnext();
      }
      if(reverse)
	 src=expand_home_relative(src);
      dst=output_file_name(src,dst,!reverse,output_dir,false);
      get_args->Append(src);
      get_args->Append(dst);
   }

   if(n_conn==0)
   {
      GetJob *j=new GetJob(Clone(),get_args,cont);
      if(del)
	 j->DeleteFiles();
      if(ascii)
	 j->Ascii();
      if(reverse)
	 j->Reverse();
      return j;
   }
   else
   {
      pgetJob *j=new pgetJob(Clone(),get_args);
      if(n_conn!=-1)
	 j->SetMaxConn(n_conn);
      return j;
   }
}

CMD(shell)
{
   Job *j;
   if(args->count()<=1)
      j=new SysCmdJob(0);
   else
   {
      char *a=args->Combine(1);
      j=new SysCmdJob(a);
      xfree(a);
   }
   return j;
}

CMD(mrm)
{
   args->setarg(0,"glob");
   args->insarg(1,"rm");
   return parent->builtin_restart();
}
CMD(rm)
{
   int opt;
   bool recursive=false;
   bool silent=false;
   const char *opts="+rf";

   bool rmdir = false;
   if(!strcmp(args->a0(),"rmdir"))
   {
      rmdir = true;
      opts="+f";
   }

   while((opt=args->getopt(opts))!=EOF)
   {
      switch(opt)
      {
      case('r'):
	 recursive=true;
	 break;
      case('f'):
	 silent=true;
	 break;
      case('?'):
      print_usage:
	 eprintf(_("Usage: %s %s[-f] files...\n"),args->a0(), rmdir? "":"[-r] ");
	 return 0;
      }
   }
   args->back();
   char *curr=args->getnext();
   if(curr==0)
      goto print_usage;

   if(recursive)
   {
      FinderJob *j=new FinderJob_Cmd(Clone(),args,FinderJob_Cmd::RM);
      args=0;
      if(silent)
	 j->BeQuiet();
      return j;
   }

   rmJob *j=(rmdir?
	     new rmdirJob(Clone(),new ArgV(args->a0())):
	     new rmJob(Clone(),new ArgV(args->a0())));

   while(curr)
   {
      j->AddFile(curr);
      curr=args->getnext();
   }

   if(silent)
      j->BeQuiet();

   return j;
}
CMD(mkdir)
{
   Job *j=new mkdirJob(Clone(),args);
   args=0;
   return j;
}

#ifndef O_ASCII
# define O_ASCII 0
#endif

CMD(source)
{
   if(args->count()<2)
   {
      // xgettext:c-format
      eprintf(_("Usage: %s <file>\n"),args->a0());
      return 0;
   }
   parent->SetCmdFeeder(new FileFeeder(new FileStream(args->getarg(1),O_RDONLY|O_ASCII)));
   exit_code=0;
   return 0;
}

CMD(jobs)
{
   int opt;
   args->rewind();
   int v=1;
   while((opt=args->getopt("+v"))!=EOF)
   {
      switch(opt)
      {
      case('v'):
	 v++;
	 break;
      case('?'):
         // xgettext:c-format
	 eprintf(_("Usage: %s [-v] [-v] ...\n"),args->a0());
	 return 0;
      }
   }
   parent->ListJobs(v);
   exit_code=0;
   return 0;
}

CMD(cd)
{
   return parent->builtin_cd();
}

CMD(pwd)
{
   int opt;
   args->rewind();
   int flags=0;
   while((opt=args->getopt("p"))!=EOF)
   {
      switch(opt)
      {
      case('p'):
	 flags|=FA::WITH_PASSWORD;
	 break;
      case('?'):
         // xgettext:c-format
	 eprintf(_("Usage: %s [-p]\n"),args->a0());
	 return 0;
      }
   }
   const char *url_c=session->GetConnectURL(flags);
   char *url=alloca_strdup(url_c);
   int len=strlen(url_c);
   url[len++]='\n';  // replaces \0
   Job *j=CopyJob::NewEcho(url,len,output,args->a0());
   output=0;
   return j;
}

CMD(exit)
{
   return parent->builtin_exit();
}

CMD(debug)
{
   const char *op=args->a0();
   int	 new_dlevel=9;
   char	 *debug_file_name=0;
   int 	 fd=-1;
   bool  enabled=true;

   int opt;
   while((opt=args->getopt("o:"))!=EOF)
   {
      switch(opt)
      {
      case('o'):
	 debug_file_name=optarg;
	 if(fd!=-1)
	    close(fd);
	 fd=open(debug_file_name,O_WRONLY|O_CREAT|O_APPEND,0600);
	 if(fd==-1)
	 {
	    perror(debug_file_name);
	    return 0;
	 }
	 fcntl(fd,F_SETFL,O_NONBLOCK);
	 fcntl(fd,F_SETFD,FD_CLOEXEC);
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }

   if(fd==-1)
      Log::global->SetOutput(2,false);
   else
      Log::global->SetOutput(fd,true);

   char *a=args->getcurr();
   if(a)
   {
      if(!strcasecmp(a,"off"))
      {
	 enabled=false;
      }
      else
      {
	 new_dlevel=atoi(a);
	 if(new_dlevel<0)
	    new_dlevel=0;
	 enabled=true;
      }
   }

   if(enabled)
   {
      Log::global->Enable();
      Log::global->SetLevel(new_dlevel);
   }
   else
      Log::global->Disable();


#if 0
   if(interactive)
   {
      if(enabled)
	 printf(_("debug level is %d, output goes to %s\n"),new_dlevel,
		     debug_file_name?debug_file_name:"<stderr>");
      else
	 printf(_("debug is off\n"));
   }
#endif
   exit_code=0;
   return 0;
}

CMD(user)
{
   if(args->count()<2 || args->count()>3)
   {
      eprintf(_("Usage: %s userid [pass]\n"),args->getarg(0));
      return 0;
   }
   char *user=args->getarg(1);
   char	*pass=args->getarg(2);
   bool insecure=(pass!=0);

   ParsedURL u(user,true);
   if(u.proto && !u.user)
   {
      exit_code=0;
      return 0;
   }
   if(u.proto && u.user && u.pass)
   {
      pass=u.pass;
      insecure=true;
   }
   if(!pass)
      pass=GetPass(_("Password: "));
   if(!pass)
      return 0;

   if(u.proto && u.user)
   {
      FA *s=FA::New(&u,false);
      if(s)
      {
	 s->SetPasswordGlobal(pass);
	 s->InsecurePassword(insecure);
	 SessionPool::Reuse(s);
      }
      else
      {
	 eprintf("%s: %s%s\n",args->a0(),u.proto,
		  _(" - not supported protocol"));
	 return 0;
      }
   }
   else
   {
      session->Login(args->getarg(1),0);
      session->SetPasswordGlobal(pass);
      session->InsecurePassword(insecure);
   }
   exit_code=0;
   return 0;
}
CMD(anon)
{
   session->AnonymousLogin();
   exit_code=0;
   return 0;
}

CMD(lftp)
{
   return parent->builtin_lftp();
}

CMD(open)
{
   return parent->builtin_open();
}

CMD(kill)
{
   int n;
   const char *op=args->a0();
   if(args->count()<2)
   {
#if 0 // too dangerous to kill last job. Better require explicit number.
      n=parent->last_bg;
      if(n==-1)
      {
	 eprintf(_("%s: no current job\n"),op);
	 return 0;
      }
      printf("%s %d\n",op,n);
      if(Job::Running(n))
      {
	 parent->Kill(n);
	 exit_code=0;
      }
      else
	 eprintf(_("%s: %d - no such job\n"),op,n);
#else
      eprintf(_("Usage: %s <jobno> ... | all\n"),args->getarg(0));
#endif
      return 0;
   }
   if(!strcmp(args->getarg(1),"all"))
   {
      parent->KillAll();
      exit_code=0;
      return 0;
   }
   args->rewind();
   exit_code=0;
   for(;;)
   {
      char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!isdigit((unsigned char)arg[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),op,arg);
	 exit_code=1;
      	 continue;
      }
      n=atoi(arg);
      if(Job::Running(n))
	 parent->Kill(n);
      else
      {
	 eprintf(_("%s: %d - no such job\n"),op,n);
	 exit_code=1;
      }
   }
   return 0;
}

CMD(set)
{
   const char *op=args->a0();
   bool with_defaults=false;
   bool only_defaults=false;
   int c;

   args->rewind();
   while((c=args->getopt("+ad"))!=EOF)
   {
      switch(c)
      {
      case 'a':
	 with_defaults=true;
	 break;
      case 'd':
	 only_defaults=true;
	 break;
      default:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->back();
   char *a=args->getnext();

   if(a==0)
   {
      char *s=ResMgr::Format(with_defaults,only_defaults);
      Job *j=CopyJob::NewEcho(s,output,args->a0());
      xfree(s);
      output=0;
      return j;
   }

   char *sl=strchr(a,'/');
   char *closure=0;
   if(sl)
   {
      *sl=0;
      closure=sl+1;
   }

   ResDecl *type;
   // find type of given variable
   const char *msg=ResMgr::FindVar(a,&type);
   if(msg)
   {
      eprintf(_("%s: %s. Use `set -a' to look at all variables.\n"),a,msg);
      return 0;
   }

   args->getnext();
   char *val=(args->getcurr()==0?0:args->Combine(args->getindex()));
   msg=ResMgr::Set(a,closure,val);

   if(msg)
   {
      eprintf("%s: %s.\n",val,msg);
      xfree(val);
      return 0;
   }
   xfree(val);

   exit_code=0;
   return 0;
}

CMD(alias)
{
   if(args->count()<2)
   {
      char *list=Alias::Format();
      Job *j=CopyJob::NewEcho(list,output,args->a0());
      xfree(list);
      output=0;
      return j;
   }
   else if(args->count()==2)
   {
      Alias::Del(args->getarg(1));
   }
   else
   {
      char *val=args->Combine(2);
      Alias::Add(args->getarg(1),val);
      xfree(val);
   }
   exit_code=0;
   return 0;
}

CMD(wait)
{
   char *op=args->a0();
   if(args->count()>2)
   {
      eprintf(_("Usage: %s [<jobno>]\n"),op);
      return 0;
   }
   int n=-1;
   args->rewind();
   char *jn=args->getnext();
   if(jn)
   {
      if(!strcmp(jn,"all"))
      {
	 parent->WaitForAllChildren();
	 for(int i=0; i<parent->waiting_num; i++)
	    parent->waiting[i]->Fg();
	 exit_code=0;
	 return 0;
      }
      if(!isdigit((unsigned char)jn[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),op,jn);
	 return 0;
      }
      n=atoi(jn);
   }
   if(n==-1)
   {
      n=parent->last_bg;
      if(n==-1)
      {
	 eprintf(_("%s: no current job\n"),op);
	 return 0;
      }
      printf("%s %d\n",op,n);
   }
   Job *j=parent->FindJob(n);
   if(j==0)
   {
      eprintf(_("%s: %d - no such job\n"),op,n);
      return 0;
   }
   if(Job::FindWhoWaitsFor(j)!=0)
   {
      eprintf(_("%s: some other job waits for job %d\n"),op,n);
      return 0;
   }
   j->parent=0;
   j->Bg();
   return j;
}

CMD(subsh)
{
   CmdExec *e=new CmdExec(Clone());

   char *c=args->getarg(1);
   e->FeedCmd(c);
   e->FeedCmd("\n");
   e->cmdline=(char*)xmalloc(strlen(c)+3);
   sprintf(e->cmdline,"(%s)",c);
   return e;
}

CMD(mv)
{
   if(args->count()!=3)
   {
      // xgettext:c-format
      eprintf(_("Usage: mv <file1> <file2>\n"));
      return 0;
   }
   Job *j=new mvJob(Clone(),args->getarg(1),args->getarg(2));
   return j;
}

const char *const cache_subcmd[]={
   "status","flush","on","off","size","expire",
   NULL
};

CMD(cache)  // cache control
{
   args->rewind();
   const char *op=args->getnext();

   if(!op)
      op="status";
   else if(!find_command(op,cache_subcmd,&op))
   {
      // xgettext:c-format
      eprintf(_("Invalid command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }
   if(!op)
   {
      // xgettext:c-format
      eprintf(_("Ambiguous command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }

   exit_code=0;
   if(!op || !strcmp(op,"status"))
      LsCache::List();
   else if(!strcmp(op,"flush"))
      LsCache::Flush();
   else if(!strcmp(op,"on"))
      LsCache::On();
   else if(!strcmp(op,"off"))
      LsCache::Off();
   else if(!strcmp(op,"size"))
   {
      op=args->getnext();
      if(!op)
      {
	 eprintf(_("%s: Operand missed for size\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      long lim=-1;
      if(strcmp(op,"unlim") && sscanf(op,"%ld",&lim)!=1)
      {
	 eprintf(_("%s: Invalid number for size\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      LsCache::SetSizeLimit(lim);
   }
   else if(!strcmp(op,"expire"))
   {
      op=args->getnext();
      if(!op)
      {
	 eprintf(_("%s: Operand missed for `expire'\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      TimeInterval exp(op);
      if(exp.Error())
      {
	 eprintf("%s: %s: %s.\n",args->a0(),op,exp.ErrorText());
	 exit_code=1;
	 return 0;
      }
      LsCache::SetExpire(exp);
   }
   return 0;
}

CMD(scache)
{
   if(args->count()==1)
   {
      SessionPool::Print(stdout);
      exit_code=0;
   }
   else
   {
      char *a=args->getarg(1);
      if(!isdigit((unsigned char)a[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),args->a0(),a);
	 return 0;
      }
      FileAccess *new_session=SessionPool::GetSession(atoi(a));
      if(new_session==0)
      {
	 eprintf(_("%s: %s - no such cached session. Use `scache' to look at session list.\n"),args->a0(),a);
	 return 0;
      }
      parent->ChangeSession(new_session);
   }
   return 0;
}

void CmdExec::print_cmd_help(const char *cmd)
{
   const cmd_rec *c;
   int part=find_cmd(cmd,&c);

   if(part==1)
   {
      if(c->long_desc==0 && c->short_desc==0)
      {
	 printf(_("Sorry, no help for %s\n"),cmd);
	 return;
      }
      if(c->short_desc==0 && !strchr(c->long_desc,' '))
      {
	 printf(_("%s is a built-in alias for %s\n"),cmd,c->long_desc);
	 print_cmd_help(c->long_desc);
	 return;
      }
      if(c->short_desc)
	 printf(_("Usage: %s\n"),_(c->short_desc));
      if(c->long_desc)
	 printf("%s",_(c->long_desc));
      return;
   }
   const char *a=Alias::Find(cmd);
   if(a)
   {
      printf(_("%s is an alias to `%s'\n"),cmd,a);
      return;
   }
   if(part==0)
      printf(_("No such command `%s'. Use `help' to see available commands.\n"),cmd);
   else
      printf(_("Ambiguous command `%s'. Use `help' to see available commands.\n"),cmd);
}

void CmdExec::print_cmd_index()
{
   int i=0;
   const char *c1;
   const cmd_rec *cmd_table=dyn_cmd_table?dyn_cmd_table:static_cmd_table;
   while(cmd_table[i].name)
   {
      while(cmd_table[i].name && !cmd_table[i].short_desc)
	 i++;
      if(!cmd_table[i].name)
	 break;
      c1=cmd_table[i].short_desc;
      i++;
      while(cmd_table[i].name && !cmd_table[i].short_desc)
	 i++;
      if(cmd_table[i].name)
      {
	 printf("\t%-35s %s\n",gettext(c1),gettext(cmd_table[i].short_desc));
	 i++;
      }
      else
	 printf("\t%s\n",_(c1));
   }
}

CMD(help)
{
   if(args->count()>1)
   {
      args->rewind();
      for(;;)
      {
	 char *cmd=args->getnext();
	 if(cmd==0)
	    break;
	 parent->print_cmd_help(cmd);
      }
      return 0;
   }

   parent->print_cmd_index();

   return 0;
}

CMD(ver)
{
   printf(
      _("Lftp | Version %s | Copyright (c) 1996-2001 Alexander V. Lukyanov\n"),VERSION);
   printf(
      _("This is free software with ABSOLUTELY NO WARRANTY. See COPYING for details.\n"));
   printf(
      _("Send bug reports and questions to <%s>.\n"),"lftp@uniyar.ac.ru");
   exit_code=0;
   return 0;
}

CMD(close)
{
   const char *op=args->a0();
   bool all=false;
   int opt;
   while((opt=args->getopt("a"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 all=true;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   if(all)
      session->CleanupAll();
   else
      session->Cleanup();
   exit_code=0;
   return 0;
}

const char * const bookmark_subcmd[]=
   {"add","delete","list","edit","import",0};
static ResDecl res_save_passwords
   ("bmk:save-passwords","no",ResMgr::BoolValidate,0);

CMD(bookmark)
{

   args->rewind();
   const char *op=args->getnext();

   if(!op)
      op="list";
   else if(!find_command(op,bookmark_subcmd,&op))
   {
      // xgettext:c-format
      eprintf(_("Invalid command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }
   if(!op)
   {
      // xgettext:c-format
      eprintf(_("Ambiguous command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }

   if(!strcmp(op,"list"))
   {
      char *list=lftp_bookmarks.Format();
      Job *j=CopyJob::NewEcho(list,output,args->a0());
      xfree(list);
      output=0;
      return j;
   }
   else if(!strcmp(op,"add"))
   {
      const char *key=args->getnext();
      if(key==0 || key[0]==0)
	 eprintf(_("%s: bookmark name required\n"),args->a0());
      else
      {
	 const char *value=args->getnext();
	 int flags=0;
	 if(res_save_passwords.QueryBool(session->GetHostName()))
	    flags|=session->WITH_PASSWORD;
	 if(value==0)
	 {
	    value=session->GetConnectURL(flags);
	    char *a=string_alloca(strlen(value)*3+2);
	    // encode some more characters, special to CmdExec parser.
	    url::encode_string(value,a,"&;|\"'\\");
	    if(value[0] && value[strlen(value)-1]!='/')
	       strcat(a,"/");
	    value=a;
	 }
	 if(value==0 || value[0]==0)
	    value="\"\"";
	 if(strchr(key,' ') || strchr(key,'\t'))
	 {
	    eprintf(_("%s: spaces in bookmark name are not allowed\n"),args->a0());
	    return 0;
	 }
	 lftp_bookmarks.Add(key,value);
   	 exit_code=0;
      }
   }
   else if(!strcmp(op,"delete"))
   {
      const char *key=args->getnext();
      if(key==0 || key[0]==0)
	 eprintf(_("%s: bookmark name required\n"),args->a0());
      else if(lftp_bookmarks.Lookup(key)==0)
	 eprintf(_("%s: no such bookmark `%s'\n"),args->a0(),key);
      else
      {
	 lftp_bookmarks.Remove(key);
	 exit_code=0;
      }
   }
   else if(!strcmp(op,"edit"))
   {
      parent->PrependCmd("shell \"/bin/sh -c 'exec ${EDITOR:-vi} $HOME/.lftp/bookmarks'\"\n");
   }
   else if(!strcmp(op,"import"))
   {
      op=args->getnext();
      if(!op)
	 eprintf(_("%s: import type required (netscape,ncftp)\n"),args->a0());
      else
      {
	 const char *fmt="shell " PKGDATADIR "/import-%s\n";
	 char *cmd=string_alloca(strlen(op)+strlen(fmt)+1);
	 sprintf(cmd,fmt,op);
	 parent->PrependCmd(cmd);
	 exit_code=0;
      }
   }
   return 0;
}

CMD(echo)
{
   char *s=args->Combine(1);
   int len=strlen(s);
   if(args->count()>1 && !strcmp(args->getarg(1),"-n"))
   {
      if(len<=3)
      {
	 exit_code=0;
	 xfree(s);
	 return 0;
      }
      memmove(s,s+3,len-=3);
   }
   else
   {
      s[len++]='\n'; // replaces \0 char
   }
   Job *j=CopyJob::NewEcho(s,len,output,args->a0());
   xfree(s);
   output=0;
   return j;
}

CMD(suspend)
{
   kill(getpid(),SIGSTOP);
   return 0;
}

CMD(find)
{
   static struct option find_options[]=
   {
      {"maxdepth",required_argument,0,'d'},
      {0,0,0,0}
   };
   int opt;
   int maxdepth = -1;
   const char *op=args->a0();

   args->rewind();
   while((opt=args->getopt_long("+d:",find_options,0))!=EOF)
   {
      switch(opt)
      {
      case 'd':
	 if(!isdigit((unsigned char)*optarg))
	 {
	    eprintf(_("%s: %s - not a number\n"),op,optarg);
	    return 0;
	 }
	 maxdepth = atoi(optarg);
	 break;
      case '?':
	 eprintf(_("Usage: %s [-d #] dir\n"),op);
	 return 0;
      }
   }

   if(!args->getcurr())
      args->Append(".");
   FinderJob_List *j=new class FinderJob_List(Clone(),args,
      output?output:new FDStream(1,"<stdout>"));
   j->set_maxdepth(maxdepth);
   output=0;
   return j;
}

CMD(du)
{
   static struct option du_options[]=
   {
      {"all",no_argument,0,'a'},
      /* alias: both GNU-like max-depth and lftp-like maxdepth;
       * only document one of them. */
      {"bytes",no_argument,0,'b'},
      {"maxdepth",required_argument,0,'d'},
      {"total",no_argument,0,'c'},
      {"max-depth",required_argument,0,'d'},
      {"human-readable",no_argument,0,'h'},
      {"si",no_argument,0,'H'},
      {"kilobytes",required_argument,0,'k'},
      {"megabytes",required_argument,0,'m'},
      {"separate-dirs",no_argument,0,'S'},
      {"summarize",no_argument,0,'s'},
      {"exclude",required_argument,0,0},
      {0,0,0,0}
   };
   int maxdepth = -1;
   bool max_depth_specified = false;
   int blocksize = 1024;
   bool separate_dirs = false;
   bool summarize_only = false;
   bool print_totals=false;
   bool all_files=false;
   const char *exclude=0;

   exit_code=1;

   const char *op=args->a0();

   args->rewind();
   int opt, longopt;
   while((opt=args->getopt_long("+abcd:hHkmsS",du_options,&longopt))!=EOF)
   {
      switch(opt)
      {
      case 'a':
	 all_files=true;
	 break;
      case 'b':
	 blocksize = 1;
	 break;
      case 'c':
	 print_totals=true;
	 break;
      case 'd':
	 if(!isdigit((unsigned char)*optarg))
	 {
	    eprintf(_("%s: %s - not a number\n"),op,optarg);
	    return 0;
	 }
	 maxdepth = atoi(optarg);
	 max_depth_specified = true;
	 break;
      case 'h':
	 blocksize = -1024;
	 break;
      case 'H':
	 blocksize = -1000;
	 break;
      case 'k': /* the default; here for completeness */
	 blocksize = 1024;
	 break;
      case 'm':
	 blocksize = 1024*1024;
	 break;
      case 's':
	 summarize_only = true;
	 break;
      case 'S':
	 separate_dirs = true;
	 break;
      case 0:
	 if(!strcmp(du_options[longopt].name, "exclude")) {
	    exclude=optarg;
	    break;
	 }
	 /* fallthrough */
      case '?':
	 eprintf(_("Usage: %s [options] <dirs>\n"),op);
	 return 0;
      }
   }

   if (summarize_only && max_depth_specified && maxdepth == 0)
      eprintf(_("%s: warning: summarizing is the same as using --max-depth=0\n"), op);

   if (summarize_only && max_depth_specified && maxdepth != 0)
   {
      eprintf(_("%s: summarizing conflicts with --max-depth=%i\n"), op, maxdepth);
      return 0;
   }

   exit_code=0;

   if (summarize_only)
      maxdepth = 0;

   if(!args->getcurr())
      args->Append(".");
   FinderJob_Du *j=new class FinderJob_Du(Clone(),args,
      output?output:new FDStream(1,"<stdout>"));
   args=0;
   j->PrintDepth(maxdepth);
   j->SetBlockSize(blocksize);
   if(print_totals)
      j->PrintTotals();
   if(all_files)
      j->AllFiles();
   if(separate_dirs)
      j->SeparateDirs();
   /* if separate_dirs is on, then there's no point in traversing past
    * max_print_depth at all */
   if(separate_dirs && maxdepth != -1)
      j->set_maxdepth(maxdepth);
   if(exclude)
      j->SetExclude(exclude);

   output=0;
   return j;
}

CMD(command)
{
   if(args->count()<2)
   {
      eprintf(_("Usage: %s command args...\n"),args->a0());
      return 0;
   }
   args->delarg(0);
   return parent->builtin_restart();
}

CMD(module)
{
   char *op=args->a0();
   if(args->count()<2)
   {
      eprintf(_("Usage: %s module [args...]\n"),args->a0());
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }
   void *map=module_load(args->getarg(1),args->count()-1,args->GetV()+1);
   if(map==0)
   {
      eprintf("%s\n",module_error_message());
      return 0;
   }
   exit_code=0;
   return 0;
}

CMD(lpwd)
{
   if(!parent->cwd)
   {
      eprintf("%s: %s\n",args->a0(),_("cannot get current directory"));
      return 0;
   }
   printf("%s\n",parent->cwd);
   exit_code=0;
   return 0;
}

CMD(glob)
{
   return parent->builtin_glob();
}

CMD(chmod)
{
   if(args->count()<3)
   {
      eprintf(_("Usage: %s mode file...\n"),args->a0());
      return 0;
   }
   int m;
   if(sscanf(args->getarg(1),"%o",&m)!=1)
   {
      eprintf(_("%s: %s - not an octal number\n"),args->a0(),args->getarg(1));
      return 0;
   }
   args->delarg(1);
   Job *j=new ChmodJob(Clone(),m,args);
   args=0;
   return j;
}

CMD(queue)
{
   return parent->builtin_queue();
}

CMD(get1)
{
   static struct option get1_options[]=
   {
      {"ascii",no_argument,0,'a'},
      {"region",required_argument,0,'r'},
      {"continue",no_argument,0,'c'},
      {"output",required_argument,0,'o'},
      {0,0,0,0}
   };
   int opt;
   const char *src=0;
   const char *dst=0;
   bool cont=false;
   bool ascii=false;

   args->rewind();
   while((opt=args->getopt_long("arco:",get1_options,0))!=EOF)
   {
      switch(opt)
      {
      case 'c':
	 cont=true;
	 break;
      case 'a':
	 ascii=true;
	 break;
      case 'o':
	 dst=optarg;
	 break;
      case 'r':
	 // FIXME
	 break;
      case '?':
      usage:
	 eprintf(_("Usage: %s [OPTS] file\n"),args->a0());
	 return 0;
      }
   }
   src=args->getcurr();
   if(src==0)
      goto usage;
   if(args->getnext()!=0)
      goto usage;

   if(dst==0 || dst[0]==0)
   {
      dst=basename_ptr(src);
   }
   else
   {
      if(dst[strlen(dst)-1]=='/')
      {
	 const char *slash=strrchr(src,'/');
	 if(slash)
	    slash++;
	 else
	    slash=src;
	 char *dst1=string_alloca(strlen(dst)+strlen(slash)+1);
	 strcpy(dst1,dst);
	 strcat(dst1,slash);
	 dst=dst1;
      }
   }

   ParsedURL dst_url(dst,true);

   if(dst_url.proto==0)
   {
      dst=expand_home_relative(dst);
      // check if dst is a directory.
      struct stat st;
      if(stat(dst,&st)!=-1)
      {
	 if(S_ISDIR(st.st_mode))
	 {
	    const char *slash=strrchr(src,'/');
	    if(slash)
	       slash++;
	    else
	       slash=src;
	    char *dst1=string_alloca(strlen(dst)+strlen(slash)+2);
	    strcpy(dst1,dst);
	    strcat(dst1,"/");
	    strcat(dst1,slash);
	    dst=dst1;
	 }
      }
   }

   FileCopyPeer *src_peer=0;
   FileCopyPeer *dst_peer=0;

   src_peer=FileCopyPeerFA::New(Clone(),src,FA::RETRIEVE,true);

   if(dst_url.proto==0)
      dst_peer=FileCopyPeerFDStream::NewPut(dst,cont);
   else
      dst_peer=new FileCopyPeerFA(&dst_url,FA::STORE);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,cont);

   if(ascii)
      c->Ascii();

   return new CopyJob(c,src,args->a0());
}

CMD(history)
{
   enum { READ, WRITE, CLEAR, LIST } mode = LIST;
   const char *fn = NULL;
   static struct option history_options[]=
   {
      {"read",required_argument,0,'r'},
      {"write",required_argument,0,'w'},
      {"clear",no_argument,0,'c'},
      {"list",required_argument,0,'l'},
      {0,0,0,0}
   };

   exit_code=0;
   args->rewind();
   int opt;
   while((opt=args->getopt_long("+r:w:cl",history_options,0))!=EOF) {
      switch(opt) {
      case 'r':
	 mode = READ;
	 fn = optarg;
	 break;
      case 'w':
	 mode = WRITE;
	 fn = optarg;
	 break;
      case 'c':
	 mode = CLEAR;
	 break;
      case 'l':
	 mode = LIST;
	 break;
      case '?':
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }

   int cnt = 16;
   if(char *arg = args->getcurr()) {
      if(!strcasecmp(arg, "all"))
	 cnt = -1;
      else if(isdigit(arg[0]))
	 cnt = atoi(arg);
      else {
	 eprintf(_("%s: %s - not a number\n"), args->a0(), args->getcurr());
	 exit_code=1;
	 return 0;
      }
   }

   switch(mode) {
   case READ:
      if(int err = lftp_history_read(fn)) {
	 eprintf("%s: %s: %s\n", args->a0(), fn, strerror(err));
	 exit_code=1;
      }
      break;

   case WRITE:
      if(int err = lftp_history_write(fn)) {
	 eprintf("%s: %s: %s\n", args->a0(), fn, strerror(err));
	 exit_code=1;
      }
      break;

   case LIST:
      lftp_history_list(cnt);
      break;
   case CLEAR:
      lftp_history_clear();
      break;
   }

   return 0;
}
