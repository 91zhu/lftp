dnl Check for libreadline of proper version
AC_DEFUN(READLINE_CHECK,
[AC_MSG_CHECKING(for readline)
AC_CACHE_VAL(lftp_cv_precompiled_readline,
[
   old_LIBS="$LIBS"
   LIBS="-lreadline $READLINE_SUPPLIB $LIBS"
   AC_TRY_LINK([extern int (*rl_getc_function)();],
      [rl_getc_function=0;],
      lftp_cv_precompiled_readline=yes,
      lftp_cv_precompiled_readline=no)
   LIBS="$old_LIBS"
])
if test $lftp_cv_precompiled_readline = yes; then
   READLINE_DIR=''
   AC_MSG_RESULT(yes)
   READLINE='-lreadline'
   READLINE_DEPEND=''
   COMPILE_READLINE=no
else
   READLINE_DIR=readline-4.0
   AC_MSG_RESULT(no - will compile)
   LINK_SRC="$LINK_SRC $READLINE_DIR"
   LINK_DST="$LINK_DST include/readline"
   READLINE='$(top_builddir)/$(READLINE_DIR)/libreadline.a'
   READLINE_DEPEND='$(top_builddir)/$(READLINE_DIR)/libreadline.a'
   COMPILE_READLINE=yes
fi
AC_CONFIG_SUBDIRS($READLINE_DIR)
AC_SUBST(COMPILE_READLINE)
AC_SUBST(READLINE_DIR)
AC_SUBST(READLINE_DEPEND)
AC_SUBST(READLINE)
])

dnl check if c++ compiler can use dynamic initializers for static variables
AC_DEFUN(CXX_DYNAMIC_INITIALIZERS,
[
   AC_LANG_SAVE
   AC_LANG_CPLUSPLUS
   AC_MSG_CHECKING(if c++ compiler can handle dynamic initializers)
   AC_TRY_RUN(
   [
      int f() { return 1; }
      int a=f();
      int main()
      {
	 exit(1-a);
      }
   ],
   [cxx_dynamic_init=yes],
   [cxx_dynamic_init=no],
   [cxx_dynamic_init=yes])
   AC_MSG_RESULT($cxx_dynamic_init)
   if test x$cxx_dynamic_init = xno; then
      AC_MSG_ERROR(C++ compiler cannot handle dynamic initializers of static objects)
   fi
   AC_LANG_RESTORE
])

dnl Check for size of addr length argument
AC_DEFUN(TYPE_SOCKLEN_T,
[
   AC_MSG_CHECKING(for socklen_t)
   AC_CACHE_VAL(lftp_cv_socklen_t,
   [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      old_CXXFLAGS="$CXXFLAGS"
      lftp_cv_socklen_t=no
      AC_TRY_COMPILE([
	 #include <sys/types.h>
	 #include <sys/socket.h>
      ],
      [
	 socklen_t len;
	 getpeername(0,0,&len);
      ],
      [
	 lftp_cv_socklen_t=yes
      ])
      AC_LANG_RESTORE
      CXXFLAGS="$old_CXXFLAGS"
   ])
   AC_MSG_RESULT($lftp_cv_socklen_t)
   if test $lftp_cv_socklen_t = no; then
      AC_DEFINE(socklen_t, int)
   fi
])

AC_DEFUN(LFTP_FUNC_POLL,
[
   AC_MSG_CHECKING(for working poll)
   AC_CACHE_VAL(lftp_cv_func_poll_works,
   [
      AC_TRY_RUN([
	       #include <sys/types.h>
	       #ifdef HAVE_SYS_POLL_H
	       #include <sys/poll.h>
	       #else
	       #include <poll.h>
	       #endif

	       int main()
	       {
		  struct pollfd pfd={5,POLLOUT}; /* fd 5 is config.log */
		  exit(!(poll(0,0,0)==0 && poll(&pfd,1,0)==1));
	       }
	    ],
	    [lftp_cv_func_poll_works=yes;],
	    [lftp_cv_func_poll_works=no;],
	    [lftp_cv_func_poll_works=no;])
   ])
   AC_MSG_RESULT($lftp_cv_func_poll_works)
   if test $lftp_cv_func_poll_works = yes; then
      AC_DEFINE(HAVE_POLL)
   fi
])

AC_DEFUN(LFTP_PROG_CXXLINK,
[
   AC_MSG_CHECKING(how to link simple c++ programs)
   if test "$GCC" = yes -a "$GXX" = yes; then
      old_CXX="$CXX"
      CXX="$CC"
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([],[char *a=new char[10]; exit(0);],
	 [],[CXX="$old_CXX";])
      AC_LANG_RESTORE
   fi
   AC_MSG_RESULT(using $CXX)
])

AC_DEFUN(LFTP_FUNC_SSCANF_CONST,
[
   AC_MSG_CHECKING(whether sscanf works on const strings)
   AC_CACHE_VAL(lftp_cv_func_sscanf_const_works,
   [
      AC_TRY_RUN([   #include <stdio.h>
		     int main() {
			int a,b;
			return !(sscanf("123x","%d%n",&a,&b)>0);
		  }],
	    [lftp_cv_func_sscanf_const_works=yes;],
	    [lftp_cv_func_sscanf_const_works=no;],
	    [lftp_cv_func_sscanf_const_works=yes;])
   ])
   AC_MSG_RESULT($lftp_cv_func_sscanf_const_works)
   if test $lftp_cv_func_sscanf_const_works = no; then
      if test "$GCC" = yes; then
	 CFLAGS="$CFLAGS -fwritable-strings"
      else
	 AC_MSG_WARN(sscanf does not work on const strings and not using gcc)
      fi
      if test "$GXX" = yes; then
	 CXXFLAGS="$CXXFLAGS -fwritable-strings"
      else
	 AC_MSG_WARN(sscanf does not work on const strings and not using g++)
      fi
   fi
])

dnl Do nothing if the compiler accepts the inline keyword.
dnl Otherwise define c_inline to __inline__ or __inline if one of those work,
dnl otherwise define c_inline to be empty.
AC_DEFUN(LFTP_C_INLINE,
[AC_CACHE_CHECK([for inline], ac_cv_c_inline,
[ac_cv_c_inline=no
for ac_kw in inline __inline__ __inline; do
  AC_TRY_COMPILE(, [} $ac_kw foo() {], [ac_cv_c_inline=$ac_kw; break])
done
])
case "$ac_cv_c_inline" in
  inline | yes) ;;
  no) AC_DEFINE(c_inline, ) ;;
  *)  AC_DEFINE_UNQUOTED(c_inline, $ac_cv_c_inline) ;;
esac
])
