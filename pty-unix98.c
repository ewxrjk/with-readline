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

#if PTY_UNIX98
void make_terminal(int *ptmp, char **slavep) {
  int ptm, pts;
  const char *slave;

  if((ptm = posix_openpt(O_RDWR|O_NOCTTY)) < 0)
    fatal(errno, "error calling posix_openpt");
  slave = ptsname(ptm);
  if(grantpt(ptm) < 0)
    fatal(errno, "error calling grantpt for %s", slave);
  if(unlockpt(ptm) < 0)
    fatal(errno, "error calling unlockpt for %s", slave);
  if((pts = open(slave, O_RDWR|O_NOCTTY, 0)) < 0)
    fatal(errno, "error opening %s", slave);
  *ptmp = ptm;
  xclose(pts);
  *slavep = xstrdup(slave);
}
#endif

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
