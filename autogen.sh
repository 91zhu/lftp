#!/bin/sh
# $Id$
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="lftp"

(test -f $srcdir/configure.in \
  && test -d $srcdir/src) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"

    exit 1
}

DIE=0

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`autoconf' installed to compile $PKG_NAME."
  echo "Download the appropriate package for your distribution,"
  echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/autoconf/"
  DIE=1
}

(grep "^AM_PROG_LIBTOOL" $srcdir/configure.in >/dev/null) && {
  (libtoolize --version) < /dev/null > /dev/null 2>&1 || {
    echo
    echo "**Error**: You must have \`libtool' installed to compile $PKG_NAME."
    echo "Get ftp://ftp.gnu.org/pub/gnu/libtool/libtool-1.4.tar.gz"
    echo "(or a newer version if it is available)"
    DIE=1
  }
}

grep "^AM_GNU_GETTEXT" $srcdir/configure.in >/dev/null && {
  grep "sed.*POTFILES" $srcdir/configure.in >/dev/null || \
  (gettextize --version) < /dev/null > /dev/null 2>&1 || {
    echo
    echo "**Error**: You must have \`gettext' installed to compile $PKG_NAME."
    echo "Get ftp://ftp.gnu.org/pub/gnu/gettext/gettext-0.10.40.tar.gz"
    echo "(or a newer version if it is available)"
    DIE=1
  }
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: You must have \`automake' installed to compile $PKG_NAME."
  echo "Get ftp://ftp.gnu.org/pub/gnu/automake/automake-1.3.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
  NO_AUTOMAKE=yes
}


# if no automake, don't bother testing for aclocal
test -n "$NO_AUTOMAKE" || (aclocal --version) < /dev/null > /dev/null 2>&1 || {
  echo
  echo "**Error**: Missing \`aclocal'.  The version of \`automake'"
  echo "installed doesn't appear recent enough."
  echo "Get ftp://ftp.gnu.org/pub/gnu/automake/automake-1.4.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
}
ver=`gettextize --version 2>&1 | sed -n 's/^.*GNU gettext.* \([0-9]*\.[0-9.]*\).*$/\1/p'`

case $ver in
  '') gettext_fail_text="Unknown gettext version.";;
  0.10.[4-9][0-9]* | 0.1[1-9]* | 0.[2-9]* | [1-9].*) ;;
  *) gettext_fail_text="Old gettext version $ver.";;
esac

if test "$gettext_fail_text" != ""; then
  echo "$gettext_fail_text."
  echo "Get ftp://ftp.gnu.org/pub/gnu/gettext/gettext-0.10.40.tar.gz"
  echo "(or a newer version if it is available)"
  DIE=1
fi

if test "$DIE" -eq 1; then
  exit 1
fi

#make -f Makefile.am srcdir=. acinclude.m4

if test -z "$*"; then
  echo "**Warning**: I am going to run \`configure' with no arguments."
  echo "If you wish to pass any to it, please specify them on the"
  echo \`$0\'" command line."
  echo
fi

case $CC in
xlc )
  am_opt=--include-deps;;
esac

for coin in `find $srcdir -name configure.in -print`
do
  dr=`dirname $coin`
  if test -f $dr/NO-AUTO-GEN; then
    echo skipping $dr -- flagged as no auto-gen
  else
    echo processing $dr
    macrodirs=`sed -n -e 's,AM_ACLOCAL_INCLUDE(\(.*\)),\1,gp' < $coin`
    ( cd $dr
      aclocalinclude="$ACLOCAL_FLAGS"
      test -d m4 && aclocalinclude="$aclocalinclude -I m4"
      for k in $macrodirs; do
  	if test -d $k; then
          aclocalinclude="$aclocalinclude -I $k"
  	##else
	##  echo "**Warning**: No such directory \`$k'.  Ignored."
        fi
      done
      if grep "^AM_GNU_GETTEXT" configure.in >/dev/null; then
	if grep "sed.*POTFILES" configure.in >/dev/null; then
	  : do nothing -- we still have an old unmodified configure.in
	else
	  echo "Creating $dr/aclocal.m4 ..."
	  test -r $dr/aclocal.m4 || touch $dr/aclocal.m4
	  echo "Running gettextize...  Ignore non-fatal messages."
	  echo "no" | gettextize --force --copy
	  patch -p0 < gettext.diff || exit 1
	  mv -f po/ChangeLog~ po/ChangeLog
	  echo "Making $dr/aclocal.m4 writable ..."
	  test -r $dr/aclocal.m4 && chmod u+w $dr/aclocal.m4
        fi
      fi
      if grep "^AM_PROG_LIBTOOL" configure.in >/dev/null; then
	echo "Running libtoolize..."
	libtoolize --force --copy
      fi
      echo "Running aclocal $aclocalinclude ..."
      aclocal $aclocalinclude
      if grep "^A[MC]_CONFIG_HEADER" configure.in >/dev/null; then
	echo "Running autoheader..."
	autoheader
      fi
      if [ -r Makefile.am ]; then
        echo "Running automake --gnu $am_opt ..."
        automake --add-missing --gnu $am_opt
      fi
      echo "Running autoconf ..."
      autoconf
    )
  fi
done

conf_flags="--enable-maintainer-mode --enable-compile-warnings --cache-file=config.cache" #--enable-iso-c
if [ `uname` = SunOS ]; then
   conf_flags="$conf_flags --with-included-gettext"
fi

if test x$NOCONFIGURE = x; then
  echo Running $srcdir/configure $conf_flags "$@" ...
  $srcdir/configure $conf_flags "$@" \
  && echo Now type \`make\' to compile $PKG_NAME
else
  echo Skipping configure process.
fi
