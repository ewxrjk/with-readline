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

AC_DEFUN([RJK_CHECK_LIB],[
  AC_CACHE_CHECK([for $2 in -l$1],[rjk_cv_lib_$1_$2],[
    save_LIBS="$LIBS"
    LIBS="${LIBS} -l$1"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([$3],[$2;])],
                   [rjk_cv_lib_$1_$2=yes],
                   [rjk_cv_lib_$1_$2=no])
    LIBS="$save_LIBS"
  ])
  if test $rjk_cv_lib_$1_$2 = yes; then
    $4
  else
    $5
  fi
])
# arch-tag:d09b2112a218009313949a279401a5b4
