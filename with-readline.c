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
static int readline_callback_installed; /* callback installed? */
static int sigpipe[2];                  /* signal notifications */
static int readline_getc_result = EOF;  /* saved character */

static const struct option options[] = {
  { "application", required_argument, 0, 'a' },
  { "help", no_argument, 0, 'h' },
  { "version", no_argument, 0, 'V' },
  { 0, 0, 0, 0 }
};

/* display usage message and terminate */
static void help(void) {
  xprintf("Usage:\n"
	  "  with-readline [OPTIONS] -- COMMAND ARGS...\n"
	  "Options:\n"
          "  --application APP, -a APP   Set application name\n"
	  "  --help, -h                  Display usage message\n"
	  "  --version, -V               Display version number\n");
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

/* write a string to fd */
static int do_writen(int fd, const char *s, size_t l) {
  size_t m = 0;
  int n;

  while(m < l) {
    n = write(fd, s + m, l - m);
    if(n < 0) {
      if(errno == EINTR) continue;
      return errno;
    } else
      m += n;
  }
  return 0;
}

/* write a string to fd */
static int do_write(int fd, const char *s) {
  return do_writen(fd, s, strlen(s));
}

/* called with input lines (or eof indicator) */
static void read_line_callback(char *s) {
  int err;

  if(s) {
    /* record line in history if nonempty */
    if(*s) add_history(s);
    /* pass input to slave reader */
    if((err = do_write(ptm, s))
       || (err = do_write(ptm, "\r")))
      fatal(err, "error writing to pty master");
    rl_callback_handler_remove();
    readline_callback_installed = 0;
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

static void prep_nop(int attribute((unused)) meta) {
}

static void deprep_nop() {
}

static void sighandler(int sig) {
  unsigned char s = sig;
  int save = errno;

  write(sigpipe[1], &s, 1);
  errno = save;
}

static int getc_callback(FILE attribute((unused)) *fp) {
  int ch;

  if((ch = readline_getc_result) == EOF)
    fatal(0, "read character callback called in wrong state");
  readline_getc_result = EOF;
  return ch;
}

int main(int argc, char **argv) {
  int n, pts, p[2], err, max;
  char *ptspath;
  FILE *tty;
  fd_set fds;
  struct winsize w;
  struct termios original_termios, t;
  char buf[4096], *line = 0;
  pid_t pid, r;
  size_t lspace = 0, llen = 0;
  struct sigaction sa;
  unsigned char sig, ch;
  const char *ptr, *app =0;

  /* we might be setuid/setgid at this point */

  /* parse command line */
  while((n = getopt_long(argc, argv, "hVa:", options, 0)) >= 0) {
    switch(n) {
    case 'a': app = optarg; break;
    case 'h': help();
    case 'V': version();
    default: fatal(0, "invalid option");
    }
  }
  if(optind == argc) fatal(0, "no command specified");
  /* if stdin is not a tty then just go straight to the command */
  if(isatty(0)) {
    /* Create the terminal
     *
     * Why use a pseudo-terminal and not a pipe?  Some programs vary their
     * behaviour depending on whether their standard input is a terminal or
     * not, and when you're addressing a program from the keyboard you probably
     * wanted the terminal behaviour.
     */
    make_terminal(&ptm, &ptspath);
    surrender_privilege();
    /* set app name for Readline */
    if(!app) {
      if((app = strrchr(argv[optind], '/'))) ++app;
      else app = argv[optind];
    }
    rl_readline_name = app;
    /* we'll have our own SIGWINCH handler */
    rl_catch_sigwinch = 0;
    /* we'll handle signals by writing the signal number into a pipe, so they
     * can be easily picked up by the event loop */
    if(pipe(sigpipe) < 0) fatal(errno, "error creating pipe");
    sa.sa_handler = sighandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGWINCH, &sa, 0) < 0)
      fatal(errno, "error installing SIGWINCH handler");
    if(sigaction(SIGCONT, &sa, 0) < 0)
      fatal(errno, "error installing SIGCONT handler");
    /* get old terminal settings; later on we'll apply these to the subsiduary
     * terminal */
    if(tcgetattr(0, &original_termios) < 0)
      fatal(errno, "error calling tcgetattr");
    if(ioctl(0, TIOCGWINSZ, &w) < 0)
      fatal(errno, "error calling ioctl TIOCGWINSZ");
    /* the child will tell the parent that it has completed initialiazation by
     * closing the this pipe.  The idea is to ensure if we read the master and
     * get eof, this is because the last slave was closed, not because it
     * hasn't been opened yet. */
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
      rl_instream = stdin;              /* needed by rl_prep_terminal */
      rl_outstream = tty;
      rl_prep_terminal(1);              /* want key at a time mode always */
      /* disable INTR and QUIT, since we want to pass them through the pty. */
      if(tcgetattr(0, &t) < 0)
        fatal(errno, "error calling tcgetattr");
      t.c_cc[VINTR] = t.c_cc[VQUIT] = 0;
      if(tcsetattr(0, TCSANOW, &t) < 0)
        fatal(errno, "error calling tcsetattr");
      /* XXX we don't handle SUSP yet. */
      /* stop rl_callback_handler_install/remove from fiddling with terminal
       * settings.  Readline documentation suggests we can set these to 0, but
       * it is a lying toad: this is not so (at least in 4.3).  */
      rl_prep_term_function = prep_nop;
      rl_deprep_term_function = deprep_nop;
      /* replace rl_getc with our own function for fine-grained control over
       * input */
      rl_getc_function = getc_callback;
      while(ptm != -1) {
        FD_ZERO(&fds);
        max = 0;
#define addfd(FD) do {                          \
  FD_SET((FD), &fds);                           \
  if((FD) > max) max = (FD);                    \
} while(0);
        addfd(0);                       /* await input */
        addfd(ptm);                     /* to detect slaves closing */
        addfd(sigpipe[0]);
        n = select(max + 1, &fds, 0, 0, 0);
        if(n < 0) {
          if(errno == EINTR)
            continue;
          fatal(errno, "error calling select");
        }
        if(FD_ISSET(0, &fds)) {
          /* Read a single character.  We could read many characters, parse out
           * the special characters, and dribble the remainder into readline,
           * but we only have to keep up with a human typist so the extra
           * effort doesn't seem worthwhile. */
          n = read(0, &ch, 1);
          if(n < 0) {
            if(errno == EINTR) continue;
            fatal(errno, "error reading from standard input");
          }
          if(n == 0) {                  /* no more stdin */
            xclose(ptm);
            ptm = -1;
            break;
          }
          /* check for interrupting characters and send them straight on to the
           * command. */
          if(ch == original_termios.c_cc[VINTR]
             || ch == original_termios.c_cc[VQUIT]) {
            switch(err = do_writen(ptm, &ch, 1)) {
            case 0: break;
            default: fatal(err, "error writing to master");
            }
            continue;
          }
          /* pass the character to readline */
          readline_getc_result = ch;
          if(!readline_callback_installed) {
            rl_already_prompted = 1;
            rl_callback_handler_install(llen ? line : "", read_line_callback);
            readline_callback_installed = 1;
            llen = 0;
          }
          rl_callback_read_char();      /* input is available */
          if(ptm == -1) break;          /* might get closed */
        }
        if(FD_ISSET(ptm, &fds)) {
          n = read(ptm, buf, sizeof buf);
          if(n < 0) {
            if(errno == EIO) break;     /* no more slaves */
            if(errno == EINTR) continue;
            fatal(errno, "error reading master");
          } else if(!n)
            break;                      /* no more slaves */
          else {
            err = do_writen(1, buf, n);
            switch(err) {
            case 0: break;
            default: fatal(err, "error writing to master");
            }
            /* figure out the output line so far */
            for(ptr = buf + n; ptr > buf && ptr[-1] != '\n'; --ptr)
              ;
            if(ptr != buf) llen = 0;    /* new line */
            n -= (ptr - buf);
            while(lspace < llen + n + 1)
              if(!(lspace = lspace ? 2 * lspace : 1))
                fatal(0, "insufficient memory");
            line = xrealloc(line, lspace);
            memcpy(line + llen, ptr, n);
            llen += n;
            line[llen] = 0;
          }
          /* the bytes read will be whatever we sent down ptm lately, we just
           * discard them */
        }
        if(FD_ISSET(sigpipe[0], &fds)) {
          n = read(sigpipe[0], &sig, 1);
          if(n < 0) {
            if(errno != EINTR) fatal(errno, "error reading from signal pipe");
          } else if(!n) fatal(0, "signal pipe unexpectedly reached EOF");
          switch(sig) {
          case SIGWINCH:
            /* propagate window size changes */
            if(ioctl(0, TIOCGWINSZ, &w) < 0)
              fatal(errno, "error calling ioctl TIOCGWINSZ");
            if(ioctl(ptm, TIOCSWINSZ, &w) < 0)
              fatal(errno, "error calling ioctl TIOSGWINSZ");
            rl_resize_terminal();
            break;
          case SIGCONT:
            /* consider also window size change */
            if(tcsetattr(0, TCSANOW, &t) < 0)
              fatal(errno, "error calling tcsetattr");
            break;
          }
        }
      }
      if(readline_callback_installed) {
        rl_callback_handler_remove();
        readline_callback_installed = 0;
      }
      rl_deprep_terminal();
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
      if(setsid() < 0)
        fatal(errno, "error calling setsid");
      if((pts = open(ptspath, O_RDWR, 0)) < 0)
        fatal(errno, "opening %s", ptspath);
#ifdef TIOCSCTTY
      if(ioctl(pts, TIOCSCTTY) < 0)
        fatal(errno, "error calling ioctl TIOCSCTTY");
#endif
      /* signal to parent that we have opened the slave */
      xclose(p[0]);
      xclose(p[1]);
      /* close stuff we don't need */
      xclose(sigpipe[0]);
      xclose(sigpipe[1]);
      if(pts != 0 && dup2(pts, 0) < 0) fatal(errno, "error calling dup2");
      if(pts != 1 && dup2(pts, 1) < 0) fatal(errno, "error calling dup2");
      if(pts != 2 && dup2(pts, 2) < 0) fatal(errno, "error calling dup2");
      if(pts > 2) xclose(pts);
      if(ioctl(0, TIOCSWINSZ, &w) < 0)
        fatal(errno, "error calling ioctl TIOSGWINSZ");
      original_termios.c_lflag &= ~ECHO;
      if(tcsetattr(0, TCSANOW, &original_termios) < 0)
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
