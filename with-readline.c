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
static int sigpipe[2];                  /* signal notifications */

static struct termios original_termios; /* original keyboard settings */
static struct termios reading_termios;  /* in-use keyboard settings */

static struct buffer input;             /* keyboard input */
static struct buffer line;              /* latest line */

static char *histfile;                  /* path to history file */

static const struct option options[] = {
  { "application", required_argument, 0, 'a' },
  { "history", required_argument, 0, 'H' },
  { "help", no_argument, 0, 'h' },
  { "version", no_argument, 0, 'V' },
  { 0, 0, 0, 0 }
};

/* display usage message and terminate */
static void help(void) {
  xprintf("Usage:\n"
	  "  with-readline [OPTIONS] -- COMMAND ARGS...\n"
	  "Options:\n"
          "  --application APP, -a APP      Set application name\n"
          "  --history ENTRIES, -H ENTRIES  Maximum history to retain\n"
	  "  --help, -h                     Display usage message\n"
	  "  --version, -V                  Display version number\n");
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

static void unblock(int sig) {
  sigset_t ss;

  sigemptyset(&ss);
  sigaddset(&ss, sig);
  if(sigprocmask(SIG_UNBLOCK, &ss, 0) < 0)
    fatal(errno, "error calling sigprocmask");
}

static void resize(void) {
  struct winsize w;

  if(ioctl(0, TIOCGWINSZ, &w) < 0)
    fatal(errno, "error calling ioctl TIOCGWINSZ");
  if(ioctl(ptm, TIOCSWINSZ, &w) < 0)
    fatal(errno, "error calling ioctl TIOSGWINSZ");
  rl_resize_terminal();
}

/* run an iteration of the event loop */
static void eventloop(void) {
  fd_set fds;
  int max, n, err;
  unsigned char ch, sig;
  const char *ptr;
  char buf[4096];

  if(ptm == -1) return;
  
  FD_ZERO(&fds);
  max = 0;
#define addfd(FD) do {                          \
  FD_SET((FD), &fds);                           \
  if((FD) > max) max = (FD);                    \
} while(0);
  addfd(0);                             /* await input */
  addfd(ptm);                           /* to detect slaves closing */
  addfd(sigpipe[0]);
  n = select(max + 1, &fds, 0, 0, 0);
  if(n < 0) {
    if(errno == EINTR)
      return;
    fatal(errno, "error calling select");
  }
  if(FD_ISSET(0, &fds)) {
    /* Read a single character.  We could read many characters, parse out the
     * special characters, and dribble the remainder into readline, but we only
     * have to keep up with a human typist so the extra effort doesn't seem
     * worthwhile. */
    n = read(0, &ch, 1);
    if(n < 0) {
      if(errno == EINTR) return;
      fatal(errno, "error reading from standard input");
    }
    if(n == 0) {                        /* no more stdin */
      xclose(ptm);
      ptm = -1;
      return;
    }
    /* check for interrupting characters and send them straight on to the
     * command. */
    if(ch == original_termios.c_cc[VINTR]
       || ch == original_termios.c_cc[VQUIT]) {
      if((err = do_writen(ptm, &ch, 1)))
        fatal(err, "error writing to master");
      return;
    }
    /* store the character for later use by readline */
    buffer_append(&input, &ch, 1);
    return;
  }
  if(FD_ISSET(ptm, &fds)) {
    n = read(ptm, buf, sizeof buf);
    if(n < 0) {
      if(errno == EIO) {
        xclose(ptm);
        ptm = -1;
        return;
      }
      if(errno == EINTR) return;
      fatal(errno, "error reading master");
    } else if(!n) {
      xclose(ptm);
      ptm = -1;
      return;
    } else {
      if((err = do_writen(1, buf, n)))
        fatal(err, "error writing to master");
      /* figure out the output line so far.  If there is a newline in the
       * current input then it is the start of a new line; throw away the
       * old line and start from just after it. */
      for(ptr = buf + n; ptr > buf && ptr[-1] != '\n'; --ptr)
        ;
      if(ptr != buf) {
        buffer_clear(&line);
        n -= (ptr - buf);
      }
      buffer_append(&line, ptr, n);
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
      resize();
      break;
    case SIGCONT:
      if(tcsetattr(0, TCSANOW, &reading_termios) < 0)
        fatal(errno, "error calling tcsetattr");
      resize();
      break;
    default:                            /* some fatal signal */
      if(tcsetattr(0, TCSANOW, &original_termios) < 0)
        fatal(errno, "error calling tcsetattr");
      unblock(sig);
      signal(sig, SIG_DFL);
      kill(getpid(), sig);
      fatal(errno, "error calling kill");
    }
  }
}

static int getc_callback(FILE attribute((unused)) *fp) {
  /* wait until a character is available */
  while(ptm != -1 && input.start == input.end)
    eventloop();
  if(ptm == -1) return EOF;
  return *input.start++;
}

/* Install a signal handler.  If always=1 then always install the handler.  If
 * always=0 then only install if the handler is currently SIG_IGN. */
static void catch_signal(int sig, int always) {
  struct sigaction sa, oldsa;

  sa.sa_handler = sighandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if(!always)
    if(sigaction(sig, 0, &oldsa) < 0)
      fatal(errno, "error querying signal handler (%d, %s)",
            sig, strsignal(sig));
  if(always || oldsa.sa_handler != SIG_IGN)
    if(sigaction(sig, &sa, 0) < 0)
      fatal(errno, "error installing signal handler (%d, %s)",
            sig, strsignal(sig));
}

static long convertnum(const char *s, long min, long max) {
  char *e;
  long n;

  errno = 0;
  n = strtol(s, &e, 10);
  if(errno) fatal(errno, "cannot convert integer '%s'", optarg);
  if(*e) fatal(0, "not a valid integer '%s'", optarg);
  if(n > max || n < min) fatal(0, "integer %ld out of range [%ld,%ld]",
                               n, min, max);
  return n;
}

int main(int argc, char **argv) {
  int n, pts, p[2], err;
  char *ptspath, *prompt, *s;
  FILE *tty;
  struct winsize w;
  char buf[4096];
  pid_t pid, r;
  const char *app = 0;
  struct stat sb;
  struct group *g;
  mode_t modemask;
  const char *home, *histfilesize;
  long maxhistory = 0;

  /* This is supposed to be a list of signals which by default terminate the
   * process.  Excluded are those that make a coredump, on the assumption that
   * you usually want the coredump to reflect the point the signal arrived, not
   * the handler.
   */
  static const int fatal_signals[] = {
    SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGALRM, SIGUSR1, SIGUSR2,
#ifdef SIGPOLL
    SIGPOLL,
#endif
#ifdef SIGPROF
    SIGPROF,
#endif
#ifdef SIGVTALRM
    SIGVTALRM,
#endif
#ifdef SIGLOST
    SIGLOST,
#endif
    0
  };

  /* we might be setuid/setgid at this point */

  /* parse command line; initial '+' means not to reorder options */
  while((n = getopt_long(argc, argv, "+hVa:H:", options, 0)) >= 0) {
    switch(n) {
    case 'a': app = optarg; break;
    case 'H':
      errno = 0;
      maxhistory = convertnum(optarg, 0, INT_MAX);
      break;
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
    /* read in saved history */
    if(!(home = getenv("HOME")))
      fatal(0, "HOME is not set");
    histfile = xmalloc(strlen(home) + strlen(app) + 64);
    sprintf(histfile, "%s/.%s_history", home, app);
    if((err = read_history(histfile)) && errno != ENOENT)
      fatal(err, "error reading %s", histfile);
    if(maxhistory == 0) {
      /* determine default history file size the same way GNU Bash does */
      if((histfilesize = getenv("HISTFILESIZE")))
        maxhistory = convertnum(histfilesize, 0, INT_MAX);
      else
        maxhistory = 500;
    }
    stifle_history(maxhistory);
    /* write the history back out, thus making sure it exists (necessary for
     * append_history() to work */
    if((err = write_history(histfile)))
      fatal(errno, "error writing %s", histfile);
    rl_readline_name = app;
    /* we'll have our own signal handlers */
    rl_catch_signals = 0;
    rl_catch_sigwinch = 0;
    /* we'll handle signals by writing the signal number into a pipe, so they
     * can be easily picked up by the event loop */
    if(pipe(sigpipe) < 0) fatal(errno, "error creating pipe");
    unblock(SIGWINCH);
    catch_signal(SIGWINCH, 1);
    unblock(SIGCONT);
    catch_signal(SIGCONT, 1);
    /* we'll want to clean up on fatal signals.  We won't (normally) get SIGINT
     * from the keyboard, but it might nonetheless be sent via kill(2). */
    for(n = 0; fatal_signals[n]; ++n)
      catch_signal(fatal_signals[n], 0);
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
      xclose(p[0]);
      /* we always echo input to /dev/tty rather than whatever stdout or stderr
       * happen to be at the moment (it would be better to guarantee to use the
       * same terminal as stdin) */
      if(!(tty = fopen("/dev/tty", "r+")))
        fatal(errno, "error opening /dev/tty");
      rl_instream = stdin;              /* needed by rl_prep_terminal */
      rl_outstream = tty;
      rl_prep_terminal(1);              /* want key at a time mode always */
      /* disable INTR and QUIT, since we want to pass them through the pty. */
      if(tcgetattr(0, &reading_termios) < 0)
        fatal(errno, "error calling tcgetattr");
      reading_termios.c_cc[VINTR] = reading_termios.c_cc[VQUIT] = 0;
      if(tcsetattr(0, TCSANOW, &reading_termios) < 0)
        fatal(errno, "error calling tcsetattr");
      /* stop readline from fiddling with terminal settings.  Readline
       * documentation suggests we can set these to 0, but it is a lying toad:
       * this is not so (at least in 4.3).  */
      rl_prep_term_function = prep_nop;
      rl_deprep_term_function = deprep_nop;
      /* replace rl_getc with our own function for fine-grained control over
       * input */
      rl_getc_function = getc_callback;
      rl_initialize();
      while(ptm != -1) {
        eventloop();                    /* wait for something to happen */
        if(input.start != input.end) {
          /* there is input.  We copy the prompt since line might be modified
           * while still reading. */
          if(!(prompt = malloc(line.end - line.start + 1)))
            fatal(errno, "error calling malloc");
          memcpy(prompt, line.start, line.end - line.start);
          prompt[line.end - line.start] = 0;
          buffer_clear(&line);          /* zap the saved line */
          rl_already_prompted = 1;      /* command already printed prompt */
          s = readline(prompt);         /* get a line */
          free(prompt);
          if(!s) {
            /* send an EOF */
            if((err = do_writen(ptm, &original_termios.c_cc[VEOF], 1)))
              fatal(err, "error writing to pty master");
          } else {
            if(*s) {
              add_history(s);
              append_history(1, histfile);
              /* currently we ignore errors writing the history */
            }
            /* pass input to slave reader */
            if((err = do_write(ptm, s))
               || (err = do_write(ptm, "\r")))
              fatal(err, "error writing to pty master");
            free(s);
          }
          rl_replace_line("", 1);
        }
      }
      if(tcsetattr(0, TCSANOW, &original_termios) < 0)
        fatal(errno, "error calling tcsetattr");
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
      /* check that the terminal has sensible permissions */
      if(fstat(pts, &sb) < 0) fatal(errno, "error calling fstat on %s",
                                    ptspath);
      /* group tty write is ok - used by write(1) and similar programs
       * group anything else write is not safe however.
       * group read is bad - shoulnd't give those programs excess privilege
       * world read or write is very bad!
       */
      if((g = getgrnam("tty")) && sb.st_gid == g->gr_gid) modemask = 057;
      else modemask = 077;
      if(sb.st_mode & modemask)
        fatal(0, "%s has insecure mode %#lo",
              ptspath, (unsigned long)sb.st_mode);
      if(sb.st_uid != getuid())
        fatal(0, "%s has owner %lu, but we are running as UID %lu",
              ptspath, (unsigned long)sb.st_uid, (unsigned long)getuid());
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
