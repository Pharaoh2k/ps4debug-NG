// SPDX-License-Identifier: GPL-3.0-only

#include <ps4.h>
#include "kdbg.h"

void dbgmark(const char *s) { sys_console_cmd(SYS_CONSOLE_CMD_PRINT, (void *)s); }

extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void)   __attribute__((weak));
void run_init_array(void) {
  if (!__init_array_start) return;
  for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p)
    if (*p) (*p)();
}

void *__dso_handle = &__dso_handle;

int __cxa_atexit(void (*func)(void *), void *arg, void *dso) {
  (void)func; (void)arg; (void)dso;
  return 0;
}
void __cxa_finalize(void *dso) { (void)dso; }

void __cxa_pure_virtual(void)   { uprintf("[shim] __cxa_pure_virtual called!"); for (;;); }
void __cxa_deleted_virtual(void){ uprintf("[shim] __cxa_deleted_virtual called!"); for (;;); }

void abort(void) { uprintf("[shim] abort()!"); for (;;); }
void exit(int code)  { (void)code; uprintf("[shim] exit()!");  for (;;); }
void _exit(int code) { (void)code; for (;;); }

// Word-at-a-time scan: the AoB fast path in proc_scan_aob_handle skips to
// the next candidate with memchr, so this must not be a naive byte loop or
// the fast path can't pay off. Classic haszero SWAR over 8-byte words.
void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  const unsigned char ch = (unsigned char)c;

  while (n && ((uintptr_t)p & (sizeof(uint64_t) - 1))) {
    if (*p == ch) return (void *)p;
    ++p; --n;
  }
  const uint64_t ones  = 0x0101010101010101ULL;
  const uint64_t highs = 0x8080808080808080ULL;
  const uint64_t xmask = ones * ch;
  while (n >= sizeof(uint64_t)) {
    uint64_t w;
    memcpy(&w, p, sizeof(w)); // unaligned-safe; clang turns this into one mov
    w ^= xmask;
    if ((w - ones) & ~w & highs) break; // some byte of w == ch
    p += sizeof(uint64_t); n -= sizeof(uint64_t);
  }
  while (n--) { if (*p == ch) return (void *)p; ++p; }
  return (void *)0;
}
int bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }

long pread(int fd, void *buf, size_t n, long off) { (void)fd; (void)buf; (void)n; (void)off; return -1; }
char *getcwd(char *buf, size_t size) { if (buf && size) buf[0] = 0; return (char *)0; }
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
  (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off; return (void *)-1;
}
int munmap(void *addr, size_t len) { (void)addr; (void)len; return 0; }
int strerror_r(int errnum, char *buf, size_t buflen) { (void)errnum; if (buf && buflen) buf[0] = 0; return 0; }

static int _lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 0x20 : c; }
int strcasecmp(const char *a, const char *b) {
  while (*a && _lc((unsigned char)*a) == _lc((unsigned char)*b)) { a++; b++; }
  return _lc((unsigned char)*a) - _lc((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
  while (n && *a && _lc((unsigned char)*a) == _lc((unsigned char)*b)) { a++; b++; n--; }
  return n ? _lc((unsigned char)*a) - _lc((unsigned char)*b) : 0;
}
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
  char *a = (char *)base;
  for (size_t i = 1; i < n; i++)
    for (size_t j = i; j > 0 && cmp(a + (j - 1) * sz, a + j * sz) > 0; j--)
      for (size_t k = 0; k < sz; k++) { char t = a[(j - 1) * sz + k]; a[(j - 1) * sz + k] = a[j * sz + k]; a[j * sz + k] = t; }
}

void *aligned_alloc(size_t align, size_t size) { return memalign(align, size); }
int posix_memalign(void **out, size_t align, size_t size) {
  void *m = memalign(align, size);
  if (!m) return 12 ;
  *out = m;
  return 0;
}

void *__stderrp = (void *)0;
void *__stdoutp = (void *)0;
void *__stdinp  = (void *)0;
int  fflush(void *f) { (void)f; return 0; }

char *getenv(const char *name) { (void)name; return (char *)0; }
int  isatty(int fd) { (void)fd; return 0; }
int  isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int  isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
int  isalpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int  isalnum(int c)  { return isalpha(c) || (c >= '0' && c <= '9'); }
int  isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int  islower(int c)  { return c >= 'a' && c <= 'z'; }
int  isprint(int c)  { return c >= 0x20 && c < 0x7f; }
int  iscntrl(int c)  { return (c >= 0 && c < 0x20) || c == 0x7f; }
int  ispunct(int c)  { return isprint(c) && c != ' ' && !isalnum(c); }
int  isblank(int c)  { return c == ' ' || c == '\t'; }
int  isgraph(int c)  { return isprint(c) && c != ' '; }
int  toupper(int c)  { return islower(c) ? c - 0x20 : c; }
int  tolower(int c)  { return isupper(c) ? c + 0x20 : c; }
void __assert(const char *func, const char *file, int line, const char *expr) {
  uprintf("[shim] assert failed: %s @ %s:%d (%s)", expr ? expr : "?", file ? file : "?", line, func ? func : "?");
  for (;;);
}
void __assert_fail(const char *expr, const char *file, unsigned line, const char *func) {
  __assert(func, file, (int)line, expr);
}
void abort_message(const char *fmt, ...) { (void)fmt; uprintf("[shim] abort_message"); for (;;); }

int pthread_rwlock_rdlock(void *l)  { (void)l; return 0; }
int pthread_rwlock_wrlock(void *l)  { (void)l; return 0; }
int pthread_rwlock_unlock(void *l)  { (void)l; return 0; }
int pthread_mutex_init(void *m, const void *a) { (void)m;(void)a; return 0; }
int pthread_mutex_destroy(void *m)  { (void)m; return 0; }
int pthread_mutex_lock(void *m)     { (void)m; return 0; }
int pthread_mutex_trylock(void *m)  { (void)m; return 0; }
int pthread_mutex_unlock(void *m)   { (void)m; return 0; }
int pthread_mutexattr_init(void *a) { (void)a; return 0; }
int pthread_mutexattr_destroy(void *a) { (void)a; return 0; }
int pthread_mutexattr_settype(void *a, int t) { (void)a;(void)t; return 0; }
int pthread_cond_init(void *c, const void *a) { (void)c;(void)a; return 0; }
int pthread_cond_destroy(void *c)   { (void)c; return 0; }
int pthread_cond_signal(void *c)    { (void)c; return 0; }
int pthread_cond_broadcast(void *c) { (void)c; return 0; }
int pthread_cond_wait(void *c, void *m) { (void)c;(void)m; return 0; }

int pthread_equal(unsigned long a, unsigned long b) { return a == b; }
int pthread_once(int *ctl, void (*fn)(void)) { if (ctl && !*ctl) { *ctl = 1; if (fn) fn(); } return 0; }

static void *_tls_slots[64]; static int _tls_n = 0;
int pthread_key_create(unsigned *key, void (*dtor)(void *)) { (void)dtor; if (_tls_n >= 64) return 12; *key = (unsigned)_tls_n++; return 0; }
int pthread_key_delete(unsigned key) { (void)key; return 0; }
void *pthread_getspecific(unsigned key) { return (key < 64) ? _tls_slots[key] : (void *)0; }
int pthread_setspecific(unsigned key, const void *val) { if (key < 64) _tls_slots[key] = (void *)val; return 0; }

int sched_yield(void) { scePthreadYield(); return 0; }
int nanosleep(const void *req, void *rem) { (void)req; (void)rem; sceKernelUsleep(1000); return 0; }
int clock_gettime(int clk, void *ts) { (void)clk; if (ts) { ((long *)ts)[0] = 0; ((long *)ts)[1] = 0; } return 0; }
long _umtx_op(void *o, int op, unsigned long v, void *u1, void *u2) { (void)o;(void)op;(void)v;(void)u1;(void)u2; return -1; }
static unsigned long _arc_s = 0x2545F4914F6CDD1DULL;
unsigned int arc4random(void) { _arc_s ^= _arc_s << 13; _arc_s ^= _arc_s >> 7; _arc_s ^= _arc_s << 17; return (unsigned int)(_arc_s >> 32); }
void arc4random_buf(void *b, unsigned long n) { unsigned char *p = (unsigned char *)b; while (n--) *p++ = (unsigned char)arc4random(); }

int  __cxa_guard_acquire(unsigned long long *g) { return !*(char *)g; }
void __cxa_guard_release(unsigned long long *g) { *(char *)g = 1; }
void __cxa_guard_abort(unsigned long long *g)   { (void)g; }
