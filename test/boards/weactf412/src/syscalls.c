/*
 * syscalls.c — minimal newlib/OS glue for the WeAct F412RE bare-metal build.
 *
 * The shared kernel's exit.c / os-main.cpp call os_terminate() and os_goodbye();
 * newlib may reference a handful of _xxx stubs. We keep these local and tiny
 * instead of pulling in the full semihosting POSIX I/O layer — this build sends
 * its output over USART2 and ARM semihosting directly.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/* Provided by the shared kernel (src/startup/reset-hardware.c). */
extern void
__reset_hardware (void);

/* ---- OS termination hooks (referenced by exit.c / os-main.cpp) ---------- */

void __attribute__ ((weak))
os_goodbye (void)
{
  /* nothing to announce */
}

void __attribute__ ((noreturn, weak))
os_terminate (int code)
{
  (void) code;
  /* No host to return to: halt the core. A debugger will stop here. */
  for (;;)
    {
      __asm__ volatile ("bkpt 0");
    }
}

/* ---- Minimal newlib stubs ---------------------------------------------- */

int __attribute__ ((weak))
_write (int fd, const char* buf, int len)
{
  (void) fd;
  (void) buf;
  return len; /* pretend success; real output goes via uart:: / trace:: */
}

int __attribute__ ((weak))
_read (int fd, char* buf, int len)
{
  (void) fd;
  (void) buf;
  (void) len;
  return 0; /* EOF */
}

int __attribute__ ((weak))
_close (int fd)
{
  (void) fd;
  return -1;
}

off_t __attribute__ ((weak))
_lseek (int fd, off_t pos, int whence)
{
  (void) fd;
  (void) pos;
  (void) whence;
  return 0;
}

int __attribute__ ((weak))
_fstat (int fd, struct stat* st)
{
  (void) fd;
  st->st_mode = S_IFCHR;
  return 0;
}

int __attribute__ ((weak))
_isatty (int fd)
{
  (void) fd;
  return 1;
}

int __attribute__ ((weak))
_kill (int pid, int sig)
{
  (void) pid;
  (void) sig;
  errno = EINVAL;
  return -1;
}

int __attribute__ ((weak))
_getpid (void)
{
  return 1;
}
