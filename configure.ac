# Process this file with autoconf to produce a configure script.
#
# This file is part of with-readline.
# Copyright (C) 2005 Richard Kettlewell
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA
#

AC_INIT(with-readline, 0.unreleased, richard+withreadline@sfere.greenend.org.uk)
AC_CONFIG_AUX_DIR([config.aux])
AM_INIT_AUTOMAKE(with-readline, 0.unreleased)
AC_CONFIG_SRCDIR([with-readline.c])
AM_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_SET_MAKE

missing_libraries=""
missing_headers=""
missing_functions=""

AC_DEFINE(_GNU_SOURCE, 1, [required for e.g. strsignal])

# Checks for libraries.
# We save up a list of missing libraries that we can't do without
# and report them all at once.
AC_CHECK_LIB([readline], [readline],
            [AC_SUBST(LIBREADLINE,[-lreadline])],
            [missing_libraries="$missing_libraries libreadline"])
AC_CHECK_LIB([util], [openpty])

if test ! -z "$missing_libraries"; then
  AC_MSG_ERROR([missing libraries:$missing_libraries])
fi

# Checks for header files.
AC_CHECK_HEADERS([readline/readline.h], [:],[
  missing_headers="$missing_headers $ac_header"
])
AC_CHECK_HEADERS([pty.h util.h])
if test ! -z "$missing_headers"; then
  AC_MSG_ERROR([missing headers:$missing_headers])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_C_INLINE

# We can substitute for getopt_long
AC_CHECK_FUNCS([getopt_long],[:],[
  # versions shipped as source trigger compiler warnings
  gcc_werror=""
  AC_LIBOBJ(getopt)
  AC_LIBOBJ(getopt1)
])
AC_CHECK_FUNCS([grantpt unlockpt ptsname openpty])

AC_CACHE_CHECK([how to acquire a pseudo-terminal],[rjk_cv_pty_how],[
  if test $ac_cv_func_grantpt = yes && test $ac_cv_func_unlockpt = yes && test $ac_cv_func_ptsname = yes; then
    rjk_cv_pty_how=unix98
  elif test $rjk_gv_openpty = yes; then
    rjk_cv_pty_how=bsd
  else
    rjk_cv_pty_how=unknown
  fi
])
case $rjk_cv_pty_how in
unix98 )
  AC_DEFINE([PTY_UNIX98],[1],[define to use Unix98 pseudo-terminals])
  ;;
bsd )
  AC_DEFINE([PTY_BSD],[1],[define to use BSD pseudo-terminals])
  ;;
unknown )
  AC_MSG_ERROR([no idea how to acquire a pty on this platform])
  ;;
esac
AM_CONDITIONAL([SETUID],[test $rjk_cv_pty_how = bsd])

if test "x$GCC" = xyes; then
  # a reasonable default set of warnings
  CC="${CC} -Wall -W -Wpointer-arith -Wbad-function-cast \
	-Wwrite-strings -Wmissing-prototypes \
	-Wmissing-declarations -Wnested-externs $gcc_werror"

  # for older GCCs that don't know not to warn for system headers
  AC_CACHE_CHECK([checking whether -Wredundant-decls is OK],
		 rjk_cv_redundant_decls,
                 oldCC="${CC}"
		 CC="${CC} -Wredundant-decls"
		 [AC_TRY_COMPILE([#define _GNU_SOURCE 1
#include <stdio.h>
#include <unistd.h>
],
				[],
				[rjk_cv_redundant_decls=yes],
				[rjk_cv_redundant_decls=no])
		 CC="${oldCC}"])
  if test $rjk_cv_redundant_decls = yes; then
    CC="${CC} -Wredundant-decls"
  fi

  AC_CACHE_CHECK([checking whether -Wshadow is OK],
		 rjk_cv_shadow,
                 oldCC="${CC}"
		 CC="${CC} -Wshadow"
		 [AC_TRY_COMPILE([
#include <unistd.h>
#include <vorbis/vorbisfile.h>
],
				[],
				[rjk_cv_shadow=yes],
				[rjk_cv_shadow=no])
		 CC="${oldCC}"])
  if test $rjk_cv_shadow = yes; then
    CC="${CC} -Wshadow"
  fi

fi

AH_BOTTOM([#ifdef __GNUC__
# define attribute(x) __attribute__(x)
#else
# define attribute(x)
#endif])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
# arch-tag:D1v5cbWiPq7NN+7Ud//oNQ
