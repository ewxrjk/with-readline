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

#include "with-readline.h"

void (*exitfn)(int) attribute((noreturn)) = exit;

void fatal(int errno_value, const char *fmt, ...) {
  va_list ap;

  fprintf(stderr, "FATAL: ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  if(errno_value != 0)
    fprintf(stderr, ": %s (%d)", strerror(errno_value), errno_value);
  fputc('\n', stderr);
  exitfn(1);
}

void xfclose(FILE *fp) {
  if(fclose(fp) < 0) fatal(errno, "fclose");
}

int xprintf(const char *s, ...) {
  va_list ap;
  int n;

  va_start(ap, s);
  if((n = vfprintf(stdout, s, ap)) < 0) fatal(errno, "stdout");
  va_end(ap);
  return n;
}

char *xstrdup(const char *s) {
  char *t;

  if(!(t = malloc(strlen(s) + 1))) fatal(errno, "malloc");
  return strcpy(t, s);
}

void xclose(int fd) {
  if(close(fd) < 0) fatal(errno, "error calling close");
}

void *xrealloc(void *ptr, size_t n) {
  if(!n) {                              /* make ambiguous case unambiguous */
    free(ptr);
    return 0;
  }
  if(!(ptr = realloc(ptr, n)))
    fatal(errno, "error calling realloc");
  return ptr;
}

void *xmalloc(size_t n) {
  void *ptr;
  if(!n)                                /* make ambiguous case unambiguous */
    return 0;
  if(!(ptr = malloc(n)))
    fatal(errno, "error calling malloc");
  return ptr;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
