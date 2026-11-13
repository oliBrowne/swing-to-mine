/* The few C library hooks the model test needs on bare metal (newlib-nano).
 * No files, no console: output goes into RAM variables read over the debugger. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>

extern uint8_t _heap_start, _heap_end;     /* from stm32f407vg.ld */
static uint8_t *heap_top = &_heap_start;

void *_sbrk(ptrdiff_t incr)
{
    if (heap_top + incr > &_heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = heap_top;
    heap_top += incr;
    return prev;
}

int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
void _exit(int status) { (void)status; while (1) { } }

/* ExecuTorch's default platform layer asks for the time; it only matters for logs. */
int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; }
    return 0;
}
