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

static int ptm;                         /* master pty fd */

static const struct option options[] = {
  { "help", no_argument, 0, 'h' },
  { "version", no_argument, 0, 'V' },
  { 0, 0, 0, 0 }
};

/* display usage message and terminate */
static void help(void) {
  xprintf("Usage:\n"
	  "  with-readline [OPTIONS] -- COMMAND ARGS...\n"
	  "Options:\n"
	  "  --help, -h              Display usage message\n"
	  "  --version, -V           Display version number\n");
  xfclose(stdout);
  exit(0);
}

/* display version number and terminate */
static void version(void) {
  xprintf("with-readline version %s (c) 2005 Richard Kettlewell\n", VERSION);
  xprintf("%s readline version %s\n",
          rl_gnu_readline_p ? "GNU" : "non-GNU",
          rl_library_version);
  xfclose(stdout);
  exit(0);
}

/* write a string to ptm */
static int do_write(int fd, const char *s) {
  size_t l = strlen(s), m = 0i;
  int n;

  while(m < l) {
    n = write(fd, s + m, l - m);
    if(n < 0) {
      if(errno == EPIPE) return -1;
      if(errno == EINTR) continue;
      fatal(errno, "error writing to pty master");
    } else
      m += n;
  }
  return 0;
}

/* called with input lines (or eof indicator) */
static void read_line_callback(char *s) {
  if(s) {
    /* record line in history */
    add_history(s);
    /* pass input to slave reader */
    do_write(ptm, s);
    do_write(ptm, "\r");
  } else {
    /* close master so that slave reader gets EOF */
    xclose(ptm);
    ptm = -1;
  }
}

/* dispose of setgid/setuid bit */
static void surrender_privilege(void) {
  gid_t egid;
  uid_t euid;

  if(getgid() != (egid = getegid())) {
    if(setregid(getgid(), getgid()) < 0)
       fatal(errno, "error calling setregid");
    if(getgid() != getegid())
      fatal(0, "real and effective group IDs do not match");
    if(setgid(egid) >= 0)
      fatal(0, "failed to surrender priviliged group ID");
  }
  if(getuid() != (euid = geteuid())) {
    if(setreuid(getuid(), getuid()) < 0)
       fatal(errno, "error calling setreuid");
    if(getuid() != geteuid())
      fatal(0, "real and effective user IDs do not match");
    if(setuid(euid) >= 0)
      fatal(0, "failed to surrender priviliged user ID");
  }
}

int main(int argc, char **argv) {
  int n, pts, p[2];
  char *ptspath;
  FILE *tty;
  fd_set fds;
  struct winsize w;
  struct termios t;
  char buf[4096];
  pid_t pid, r;

  /* parse command line */
  while((n = getopt_long(argc, argv, "hV", options, 0)) >= 0) {
    switch(n) {
    case 'h': help();
    case 'V': version();
    default: fatal(0, "invalid option");
    }
  }
  if(optind == argc) fatal(0, "no command specified");
  /* if stdin is not a tty then just go straight to the command */
  if(isatty(0)) {
    /* create the terminal */
    make_terminal(&ptm, &ptspath);
    surrender_privilege();
    /* get old terminal settings */
    if(tcgetattr(0, &t) < 0)
      fatal(errno, "error calling tcgetattr");
    if(ioctl(0, TIOCGWINSZ, &w) < 0)
      fatal(errno, "error calling ioctl TIOCGWINSZ");
    if(pipe(p) < 0) fatal(errno, "error creating pipe");
    switch(pid = fork()) {
    case -1: fatal(errno, "error calling fork");

      /* parent */
    default:
      /* wait for child to open slave */
      xclose(p[1]);
      read(p[0], buf, 1);
      /* we always echo input to /dev/tty rather than whatever stdout or stderr
       * happen to be at the moment (it would be better to guarantee to use the
       * same terminal as stdin) */
      if(!(tty = fopen("/dev/tty", "r+")))
        fatal(errno, "error opening /dev/tty");
      rl_outstream = tty;
      /* we leave stdout to the command */
      xclose(1);
      rl_callback_handler_install("", read_line_callback);
      while(ptm != -1) {
        FD_ZERO(&fds);
        FD_SET(0, &fds);                /* await input */
        FD_SET(ptm, &fds);              /* to detect slaves closing */
        n = select(ptm + 1, &fds, 0, 0, 0);
        if(n < 0) {
          if(errno == EINTR)
            continue;
          fatal(errno, "error calling select");
        }
        if(FD_ISSET(0, &fds))
          rl_callback_read_char();      /* input is available */
        else if(FD_ISSET(ptm, &fds)) {
          n = read(ptm, buf, sizeof buf);
          if(n < 0) {
            if(errno == EIO) break;     /* no more slaves */
            if(errno == EINTR) continue;
            fatal(errno, "error reading master");
          } else if(!n)
            break;                      /* no more slaves */
          /* the bytes read will be whatever we sent down ptm lately, we just
           * discard them */
        }
      }
      rl_callback_handler_remove();
      /* wait for the child to terminate so we can return its exit status */
      while((r = waitpid(pid, &n, 0)) < 0 && errno == EINTR)
        ;
      if(r < 0) fatal(errno, "error calling waitpid");
      if(WIFEXITED(n))
        exit(WEXITSTATUS(n));
      if(WIFSIGNALED(n)) {
        fprintf(stderr, "%s: %s%s\n",
                argv[optind], strsignal(WTERMSIG(n)),
                WCOREDUMP(n) ? " (core dumped)" : "");
        exit(128 + WTERMSIG(n));
      }
      fatal(0, "cannot parse wait status %#x", (unsigned)n);

      /* child */
    case 0:
      exitfn = _exit;
      xclose(ptm);
      if((pts = open(ptspath, O_RDWR, 0)) < 0)
        fatal(errno, "opening %s", ptspath);
      /* signal to parent that we have opened the slave */
      xclose(p[0]);
      xclose(p[1]);
      if(pts != 0) {
        if(dup2(pts, 0) < 0) fatal(errno, "error calling dup2");
        xclose(pts);
      }
      if(ioctl(0, TIOCSWINSZ, &w) < 0)
        fatal(errno, "error calling ioctl TIOSGWINSZ");
      if(tcsetattr(0, TCSANOW, &t) < 0)
        fatal(errno, "error calling tcsetattr");
      /* fall through to execution */
      break;
    }
  } else
    surrender_privilege();
  execvp(argv[optind], &argv[optind]);
  fatal(errno, "error executing %s", argv[optind]);
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:C911jgjHJBo7m+ZftBXo6Q */
