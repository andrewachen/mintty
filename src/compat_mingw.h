// ABOUTME: MinGW/CLANGARM64 compatibility shims for POSIX types, constants, and functions
// ABOUTME: Only included when building with __MINGW32__ (via std.h); stubs out Cygwin/POSIX dependencies

#ifndef COMPAT_MINGW_H
#define COMPAT_MINGW_H

// Suppress deprecated MSVC-compat names from MinGW string.h (e.g. strset(char*,int))
// that conflict with mintty's own declarations.  Must be set before any system header.
#define _NO_OLDNAMES

#include <stddef.h>  // offsetof
#include <stdint.h>  // uint64_t
#include <stdio.h>   // snprintf
#include <time.h>    // time_t, localtime, strftime
#include <direct.h>  // _mkdir
#include <windows.h>
#include <io.h>      // _isatty, open, close, dup
#include <process.h> // getpid
#include <fcntl.h>   // O_WRONLY, O_CREAT, O_APPEND

// O_NOCTTY is a no-op on Windows (no controlling terminal concept)
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

// MinGW mkdir takes only 1 arg (no mode)
#define mkdir(path, mode) _mkdir(path)

// _NO_OLDNAMES also suppresses strdup — restore it via _strdup
#ifndef strdup
#define strdup _strdup
#endif

// --- pid_t ---
#ifndef _PID_T_
#define _PID_T_
typedef int pid_t;
#endif

// --- struct winsize ---
struct winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};

// --- CERASE / CDEL (erase/delete character, normally from termios.h) ---
#ifndef CERASE
#define CERASE 0x7f
#endif
#ifndef CDEL
#define CDEL   0x7f
#endif

// --- termios stubs (just enough to compile child.h) ---
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  cc_t     c_cc[20];
};

// --- Signal constants missing from mingw-w64 ---
#ifndef SIGHUP
#define SIGHUP    1
#endif
#ifndef SIGQUIT
#define SIGQUIT   3
#endif
#ifndef SIGKILL
#define SIGKILL   9
#endif
#ifndef SIGPIPE
#define SIGPIPE   13
#endif
#ifndef SIGALRM
#define SIGALRM   14
#endif
#ifndef SIGCHLD
#define SIGCHLD   17
#endif
#ifndef SIGTSTP
#define SIGTSTP   20
#endif
#ifndef SIGTTIN
#define SIGTTIN   21
#endif
#ifndef SIGTTOU
#define SIGTTOU   22
#endif

// --- waitpid flags/macros ---
#ifndef WNOHANG
#define WNOHANG   1
#endif
#ifndef WIFEXITED
#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(s)     ((s) & 0x7f)
#endif

// --- struct passwd ---
struct passwd {
  char *pw_name;
  char *pw_dir;
  char *pw_shell;
};

// --- struct utsname + uname() ---
struct utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
};

static inline int uname(struct utsname *u) {
  if (!u) return -1;
  strncpy(u->sysname, "Windows", 64);
  strncpy(u->machine, "aarch64", 64);
  OSVERSIONINFOW vi;
  memset(&vi, 0, sizeof vi);
  vi.dwOSVersionInfoSize = sizeof vi;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  GetVersionExW(&vi);
#pragma GCC diagnostic pop
  snprintf(u->release, 64, "%lu.%lu", vi.dwMajorVersion, vi.dwMinorVersion);
  snprintf(u->version, 64, "Build %lu", vi.dwBuildNumber);
  gethostname(u->nodename, 64);
  return 0;
}

// --- POSIX function stubs ---
static inline pid_t fork(void)      { return -1; }
static inline int setsid(void)      { return -1; }
static inline pid_t waitpid(pid_t pid, int *status, int options) {
  (void)pid; (void)status; (void)options; return -1;
}
static inline int kill(pid_t pid, int sig) {
  (void)pid; (void)sig; return -1;
}
static inline unsigned int getuid(void) { return 0; }
static inline struct passwd *getpwuid(unsigned int uid) {
  (void)uid; return NULL;
}
static inline struct passwd *getpwnam(const char *name) {
  (void)name; return NULL;
}
static inline char *getlogin(void) {
  return getenv("USERNAME");
}
static inline char *ptsname(int fd) {
  (void)fd; return NULL;
}
static inline int grantpt(int fd)   { (void)fd; return 0; }
static inline int unlockpt(int fd)  { (void)fd; return 0; }
static inline void login(void *ut)  { (void)ut; }
static inline char *strsignal(int sig) {
  (void)sig; return "signal";
}

// isatty wrapper — mingw uses _isatty
#ifndef isatty
#define isatty _isatty
#endif

// strcasestr is GNU extension; MinGW lacks it
static inline char *strcasestr(const char *haystack, const char *needle) {
  size_t nlen = strlen(needle);
  for (; *haystack; haystack++)
    if (_strnicmp(haystack, needle, nlen) == 0)
      return (char *)haystack;
  return NULL;
}

// unsetenv is POSIX; MinGW uses _putenv_s with empty value
static inline int unsetenv(const char *name) {
  return _putenv_s(name, "") == 0 ? 0 : -1;
}

// setenv is POSIX; MinGW provides _putenv_s
static inline int setenv(const char *name, const char *value, int overwrite) {
  if (!overwrite && getenv(name)) return 0;
  return _putenv_s(name, value) == 0 ? 0 : -1;
}

// gettimeofday is POSIX; use GetSystemTimeAsFileTime on Windows
// struct timeval may already be defined by winsock.h
#ifndef _TIMEVAL_DEFINED
struct timeval {
  long tv_sec;
  long tv_usec;
};
#endif
static inline int gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  if (!tv) return 0;
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULONGLONG t = (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  t -= 116444736000000000ULL;  // 100ns intervals from 1601 to 1970
  tv->tv_sec  = (long)(t / 10000000);
  tv->tv_usec = (long)((t % 10000000) / 10);
  return 0;
}

// MinGW string.h declares strset(char*,int) regardless of _NO_OLDNAMES.
// Rename mintty's strset/wstrset so both can coexist.
#define strset mintty_strset
#define wstrset mintty_wstrset

// --- u_int64_t (BSD-style name, not in MinGW) ---
typedef uint64_t u_int64_t;

// --- termios c_cc indices (used in termclip.c, termmouse.c) ---
#ifndef VINTR
#define VINTR  0
#define VQUIT  1
#define VERASE 3
#define VSUSP  9
#define VSWTC  10
#endif

// --- CTRL(c) control character macro (from BSD/POSIX termios) ---
#ifndef CTRL
#define CTRL(c) ((c) & 0x1f)
#endif

// --- fstat (used in winimg.c) — available in MinGW via sys/stat.h ---
#include <sys/stat.h>

// --- access() and mode constants (used by WSL code paths) ---
// _NO_OLDNAMES suppresses the POSIX alias; restore it via _access.
// Windows has no execute bit; X_OK maps to existence check.
#define access _access
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 0
#endif
#ifndef F_OK
#define F_OK 0
#endif

#endif /* COMPAT_MINGW_H */
