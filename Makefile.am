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

bin_PROGRAMS=with-readline

with_readline_SOURCES=with-readline.c pty-unix98.c pty-bsd.c util.c	\
with-readline.h getopt.h
with_readline_LDADD=$(LIBOBJS) $(LIBREADLINE)

man_MANS=with-readline.1

EXTRA_DIST=ChangeLog.d $(man_MANS)

install-exec-hook:
if SETUID
	chown 0:0 $(DESTDIR)$(bindir)/with-readline$(EXEEXT)
	chmod 4555 $(DESTDIR)$(bindir)/with-readline$(EXEEXT)
endif

# arch-tag:UzftHuacSCXqQdpEMXpLmQ
