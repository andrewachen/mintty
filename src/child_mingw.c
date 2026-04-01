// ABOUTME: MinGW/CLANGARM64 child process implementation using Windows ConPTY API
// ABOUTME: Compiled instead of child.c on CLANGARM64; spawns child via CreatePseudoConsole

#include "child.h"
#include "term.h"
#include "winpriv.h"
#include "std.h"
#include <stdarg.h>

// ConPTY API — available since Windows 10 1809, ARM64 always has it.
// PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE is only defined in headers when
// _WIN32_WINNT >= 0x0A00; declare everything manually if the header skipped it.
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE ((DWORD)0x00020016)
typedef VOID* HPCON;
HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE hInput, HANDLE hOutput,
                                   DWORD dwFlags, HPCON *phPC);
HRESULT WINAPI ResizePseudoConsole(HPCON hPC, COORD size);
void    WINAPI ClosePseudoConsole(HPCON hPC);
#endif
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif


string child_dir = null;
bool logging = false;

static HPCON  hpc        = NULL;
static HANDLE hpipe_in   = INVALID_HANDLE_VALUE;  // we write → child stdin
static HANDLE hpipe_out  = INVALID_HANDLE_VALUE;  // child stdout → reader thread
static HANDLE hproc      = NULL;
static HANDLE hio_thread = NULL;
static bool   child_killed = false;

// Background thread: blocks on ReadFile and posts output to the main window.
static DWORD WINAPI
io_reader(LPVOID _)
{
  (void)_;
  char buf[4096];
  DWORD nread;
  while (ReadFile(hpipe_out, buf, sizeof buf, &nread, NULL) && nread > 0) {
    char *copy = malloc(nread);
    if (!copy) break;  // OOM: let pipe-break sentinel signal exit
    memcpy(copy, buf, nread);
    if (!PostMessage(wnd, WM_CONPTY_DATA, (WPARAM)copy, (LPARAM)nread))
      free(copy);
  }
  // Pipe broken = child exited; wake main thread so it can call exit_mintty.
  PostMessage(wnd, WM_CONPTY_DATA, (WPARAM)NULL, 0);
  return 0;
}

// Write one argument into *pp using the standard Windows quoting rules:
// backslashes before a '"' are doubled; '"' is escaped as '\"';
// backslashes before end-of-string are doubled to avoid escaping the closing '"'.
// Only wraps in quotes when the arg contains spaces, tabs, or quotes.
static void
quote_arg(wchar_t **pp, const wchar_t *arg)
{
  bool needs_quotes = !*arg;  // empty arg must be quoted
  for (const wchar_t *p = arg; *p && !needs_quotes; p++)
    if (*p == L' ' || *p == L'\t' || *p == L'"')
      needs_quotes = true;

  if (!needs_quotes) {
    while (*arg) *(*pp)++ = *arg++;
    return;
  }

  *(*pp)++ = L'"';
  while (*arg) {
    int nbs = 0;
    while (*arg == L'\\') { nbs++; arg++; }

    if (!*arg) {
      // End of arg: double backslashes before the closing quote
      for (int i = 0; i < nbs * 2; i++) *(*pp)++ = L'\\';
      break;
    } else if (*arg == L'"') {
      // Literal quote: double the backslashes, then escape the quote
      for (int i = 0; i < nbs * 2 + 1; i++) *(*pp)++ = L'\\';
      *(*pp)++ = L'"';
      arg++;
    } else {
      // Normal char: emit accumulated backslashes literally
      for (int i = 0; i < nbs; i++) *(*pp)++ = L'\\';
      *(*pp)++ = *arg++;
    }
  }
  *(*pp)++ = L'"';
}

// Build a Windows command-line string from a UTF-8 argv array.
// Caller must free the returned buffer.
static wchar_t *
build_cmdline(char *argv[])
{
  // Upper-bound size: 2x chars per arg (worst-case backslash doubling) + quotes + space
  size_t total = 1;
  for (int i = 0; argv[i]; i++) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, NULL, 0);
    if (wlen < 1) wlen = 1;
    total += 2 * wlen + 3;
  }

  wchar_t *cmdline = malloc(total * sizeof(wchar_t));
  wchar_t *p = cmdline;
  for (int i = 0; argv[i]; i++) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, NULL, 0);
    wchar_t *arg = malloc(wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, arg, wlen);
    if (i > 0) *p++ = L' ';
    quote_arg(&p, arg);
    free(arg);
  }
  *p = L'\0';
  return cmdline;
}

void
child_create(char *argv[], struct winsize *winp)
{
  // Create pipe pairs for ConPTY I/O.
  HANDLE pin_r, pin_w, pout_r, pout_w;
  if (!CreatePipe(&pin_r, &pin_w, NULL, 0)) {
    MessageBoxA(NULL, "CreatePipe failed", "mintty", MB_OK | MB_ICONERROR);
    exit_mintty();
    return;
  }
  if (!CreatePipe(&pout_r, &pout_w, NULL, 0)) {
    CloseHandle(pin_r); CloseHandle(pin_w);
    MessageBoxA(NULL, "CreatePipe failed", "mintty", MB_OK | MB_ICONERROR);
    exit_mintty();
    return;
  }

  // Create the pseudo console.
  // COORD fields are SHORT (signed 16-bit); clamp to avoid truncation.
  COORD size = { (SHORT)min(winp->ws_col, 32767), (SHORT)min(winp->ws_row, 32767) };
  HRESULT hr = CreatePseudoConsole(size, pin_r, pout_w, 0, &hpc);
  CloseHandle(pin_r);   // now owned by ConPTY
  CloseHandle(pout_w);  // now owned by ConPTY
  if (FAILED(hr)) {
    CloseHandle(pin_w); CloseHandle(pout_r);
    MessageBoxA(NULL, "CreatePseudoConsole failed", "mintty", MB_OK | MB_ICONERROR);
    exit_mintty();
    return;
  }
  hpipe_in  = pin_w;
  hpipe_out = pout_r;

  // Build STARTUPINFOEX with ConPTY attribute.
  SIZE_T attr_sz = 0;
  InitializeProcThreadAttributeList(NULL, 1, 0, &attr_sz);
  LPPROC_THREAD_ATTRIBUTE_LIST attrlist = malloc(attr_sz);
  InitializeProcThreadAttributeList(attrlist, 1, 0, &attr_sz);
  UpdateProcThreadAttribute(attrlist, 0,
    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc), NULL, NULL);

  STARTUPINFOEXW si = {0};
  si.StartupInfo.cb = sizeof(si);
  si.lpAttributeList = attrlist;

  wchar_t *cmdline = build_cmdline(argv);

  // When cmd differs from argv[0] (e.g. login shell: cmd="wsl", argv[0]="-wsl"),
  // pass cmd as the executable via lpApplicationName so Windows doesn't try to
  // launch the display name.  This mirrors execvp(cmd, argv) semantics.
  wchar_t *exe = NULL;
  extern char *cmd;
  if (cmd && argv[0] && strcmp(cmd, argv[0]) != 0) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
    exe = malloc(wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, cmd, -1, exe, wlen);
  }

  PROCESS_INFORMATION pi = {0};
  BOOL ok = CreateProcessW(exe, cmdline, NULL, NULL, FALSE,
                           EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                           &si.StartupInfo, &pi);
  free(exe);
  free(cmdline);
  DeleteProcThreadAttributeList(attrlist);
  free(attrlist);

  if (!ok) {
    ClosePseudoConsole(hpc);    hpc       = NULL;
    CloseHandle(hpipe_in);     hpipe_in  = INVALID_HANDLE_VALUE;
    CloseHandle(hpipe_out);    hpipe_out = INVALID_HANDLE_VALUE;
    MessageBoxA(NULL, "CreateProcessW failed", "mintty", MB_OK | MB_ICONERROR);
    exit_mintty();
    return;
  }

  hproc = pi.hProcess;
  CloseHandle(pi.hThread);

  // Start reader thread: blocks on ReadFile, posts WM_CONPTY_DATA on output.
  hio_thread = CreateThread(NULL, 0, io_reader, NULL, 0, NULL);
  if (!hio_thread) {
    ClosePseudoConsole(hpc);    hpc       = NULL;
    CloseHandle(hpipe_in);     hpipe_in  = INVALID_HANDLE_VALUE;
    CloseHandle(hpipe_out);    hpipe_out = INVALID_HANDLE_VALUE;
    TerminateProcess(hproc, 1); CloseHandle(hproc); hproc = NULL;
    MessageBoxA(NULL, "CreateThread failed", "mintty", MB_OK | MB_ICONERROR);
    exit_mintty();
  }
}

void child_update_charset(void) {}

void toggle_logging(void) {}

void
child_proc(void)
{
  if (!hpc) return;
  // Wait up to 100ms for Windows messages (including WM_CONPTY_DATA posted by
  // the reader thread), mirroring the original select(win_fd, pty_fd, 100ms).
  MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
  // Message dispatching (WM_CONPTY_DATA → term_write) runs in the message loop.
  // We only check for silent process exit here.
  if (hproc && WaitForSingleObject(hproc, 0) == WAIT_OBJECT_0 && !child_killed)
    exit_mintty();
}

void
child_kill(bool point_blank)
{
  (void)point_blank;
  child_killed = true;
  if (hpc) { ClosePseudoConsole(hpc); hpc = NULL; }
  if (hpipe_in != INVALID_HANDLE_VALUE) {
    CloseHandle(hpipe_in); hpipe_in = INVALID_HANDLE_VALUE;
  }
  // Close output pipe so ReadFile in the reader thread unblocks and exits.
  if (hpipe_out != INVALID_HANDLE_VALUE) {
    CloseHandle(hpipe_out); hpipe_out = INVALID_HANDLE_VALUE;
  }
  if (hproc) { TerminateProcess(hproc, 1); CloseHandle(hproc); hproc = NULL; }
  if (hio_thread) {
    WaitForSingleObject(hio_thread, 1000);
    CloseHandle(hio_thread); hio_thread = NULL;
  }
  exit_mintty();
}

void
child_write(const char *buf, uint len)
{
  DWORD w;
  if (hpipe_in != INVALID_HANDLE_VALUE)
    WriteFile(hpipe_in, buf, (DWORD)len, &w, NULL);
}

void child_break(void) {}
void child_intr(void) {}

void
child_printf(const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  char *msg;
  if (vasprintf(&msg, fmt, va) >= 0) {
    term_write(msg, strlen(msg));
    free(msg);
  }
  va_end(va);
}

void
child_send(const char *buf, uint len)
{
  term_reset_screen();
  if (term.echoing)
    term_write(buf, len);
  child_write(buf, len);
}

void
child_sendw(const wchar *buf, uint len)
{
  // Convert UTF-16 to UTF-8 for the ConPTY pipe.
  int bytes = WideCharToMultiByte(CP_UTF8, 0, buf, len, NULL, 0, NULL, NULL);
  if (bytes > 0) {
    char *s = malloc(bytes);
    WideCharToMultiByte(CP_UTF8, 0, buf, len, s, bytes, NULL, NULL);
    child_send(s, bytes);
    free(s);
  }
}

void
child_resize(struct winsize *winp)
{
  if (hpc) {
    COORD c = { (SHORT)min(winp->ws_col, 32767), (SHORT)min(winp->ws_row, 32767) };
    ResizePseudoConsole(hpc, c);
  }
}

bool child_is_alive(void)  { return hproc && WaitForSingleObject(hproc, 0) == WAIT_TIMEOUT; }
bool child_is_parent(void) { return false; }

char *procres(int pid, char *res)
{
  (void)pid; (void)res; return null;
}

wchar *grandchild_process_list(void) { return null; }
char  *child_tty(void)               { return null; }
uchar *child_termios_chars(void)     { return null; }
char  *foreground_prog(void)         { return null; }
char *
foreground_cwd(void)
{
  if (child_dir && *child_dir)
    return strdup(child_dir);
  return null;
}

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
  strset(&child_dir, dir);
}

void setenvi(char *env, int val)
{
  static char valbuf[22];
  snprintf(valbuf, sizeof valbuf, "%d", val);
  _putenv_s(env, valbuf);
}

void child_close_log(void) {}

void child_launch(int n, int argc, char *argv[], int moni)
{
  (void)n; (void)argc; (void)argv; (void)moni;
}

// ReGIS graphics not supported on CLANGARM64 (regis.c uses GNU nested functions)
#include "regis.h"
void regis_draw(HDC dc, float scale, int w, int h, int mode,
                uchar * regis, bool first_draw, flush_fn flush)
{
  (void)dc; (void)scale; (void)w; (void)h; (void)mode;
  (void)regis; (void)first_draw; (void)flush;
}
