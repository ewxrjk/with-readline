/*
 * This file is part of with-readline
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

void buffer_init(struct buffer *b) {
  b->base = b->start = b->end = b->top = 0;
}

void buffer_append(struct buffer *b, const void *ptr, size_t n) {
  char *nb;
  size_t ns, offset = b->start - b->base, len = b->end - b->start,
    spare = b->top - b->end;
  
  if(n > spare) {
    /* not enough space at the top */
    if(n <= spare + offset) {
      /* enough space in total */
      memmove(b->base, b->start, len);
      b->end = b->base + len;
      b->start = b->base;
    } else {
      /* not enough space in total */
      if(!(ns = b->top - b->base)) ns = 1;
      while(ns && ns < n + len)
	ns *= 2;
      if(!ns) fatal(0, "insufficient memory");
      if(!(nb = realloc(b->start, ns)))
	fatal(errno, "error calling realloc");
      memmove(nb, nb + offset, len);
      b->base = b->start = nb;
      b->end = nb + len;
      b->top = nb + ns;
    }
  }
  memcpy(b->end, ptr, n);
  b->end += n;
}

int buffer_write(struct buffer *b, int fd) {
  int n;

  n = write(fd, b->start, b->end - b->start);
  if(n < 0) return errno;
  b->start += n;
  if(b->start == b->end) b->start = b->end = b->base;
  return 0;
}

void buffer_clear(struct buffer *b) {
  b->start = b->end = b->base;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:5ysLr89FJgUVUyS/UcGz0g */
