// ABOUTME: MinGW/CLANGARM64 stub replacement for child.c — all child process functions are no-ops
// ABOUTME: Compiled instead of child.c on CLANGARM64; ConPTY integration deferred to a future phase

#include "child.h"
#include "term.h"
#include "winpriv.h"
#include <stdarg.h>

string child_dir = null;
bool logging = false;

void child_update_charset(void) {}

void child_create(char *argv[], struct winsize *winp)
{
  (void)argv; (void)winp;
}

void toggle_logging(void) {}

void child_proc(void) {}

void child_kill(bool point_blank)
{
  (void)point_blank;
  exit_mintty();
}

void child_write(const char *buf, uint len)
{
  (void)buf; (void)len;
}

void child_break(void) {}
void child_intr(void) {}

void child_printf(const char *fmt, ...)
{
  (void)fmt;
}

void child_send(const char *buf, uint len)
{
  (void)buf; (void)len;
}

void child_sendw(const wchar *buf, uint len)
{
  (void)buf; (void)len;
}

void child_resize(struct winsize *winp)
{
  (void)winp;
}

bool child_is_alive(void)  { return false; }
bool child_is_parent(void) { return false; }

char *procres(int pid, char *res)
{
  (void)pid; (void)res; return null;
}

wchar *grandchild_process_list(void) { return null; }
char  *child_tty(void)               { return null; }
uchar *child_termios_chars(void)     { return null; }
char  *foreground_prog(void)         { return null; }
char  *foreground_cwd(void)          { return null; }

void user_command(wstring commands, int n)
{
  (void)commands; (void)n;
}

wstring child_conv_path(wstring path, bool adjust_dir)
{
  (void)adjust_dir; return path;
}

void child_fork(int argc, char *argv[], int moni, bool config_size, bool in_cwd, bool in_tabs)
{
  (void)argc; (void)argv; (void)moni;
  (void)config_size; (void)in_cwd; (void)in_tabs;
}

void child_set_fork_dir(char *dir)
{
  (void)dir;
}

void setenvi(char *env, int val)
{
  static char valbuf[22];
  snprintf(valbuf, sizeof valbuf, "%d", val);
  _putenv_s(env, valbuf);
}

void child_launch(int n, int argc, char *argv[], int moni)
{
  (void)n; (void)argc; (void)argv; (void)moni;
}
