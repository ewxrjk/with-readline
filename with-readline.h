/*
 * This file is part of with-readline.
 * Copyright (C) 2005 Richard Kettlewell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef WITH_READLINE_H
#define WITH_READLINE_H

#if HAVE_CONFIG_H
# include <config.h>
#endif
#if HAVE_PTY_H
# include <pty.h>
#endif
#if HAVE_UTIL_H
# include <util.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <readline/readline.h>
#include <readline/history.h>

void xfclose(FILE *fp);
int xprintf(const char *s, ...);
char *xstrdup(const char *s);
void xclose(int fd);

void fatal(int errno_value, const char *fmt, ...)
  attribute((noreturn))
  attribute((format (printf, 2, 3)));
extern void (*exitfn)(int) attribute((noreturn));

extern int debugging;

void make_terminal(int *ptmp, char **slavep);

#ifndef PTMX_PATH
# define PTMX_PATH "/dev/ptmx"
#endif

#ifndef WCOREDUMP
# define WCOREDUMP(W) ((W) & 0x80)
#endif

#endif /* WITH_READLINE_H */

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:GzcB+cdeLSuiMbrj99qYPw */
