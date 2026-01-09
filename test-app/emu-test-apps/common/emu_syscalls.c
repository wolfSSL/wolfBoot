#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

extern uint32_t _ebss;
extern uint32_t _estack;
extern volatile uint32_t systick_ms;
void emu_uart_putc(char c);

static char *heap_end;

int _write(int file, const char *ptr, int len)
{
    int i;
    (void)file;
    for (i = 0; i < len; ++i) {
        emu_uart_putc(ptr[i]);
    }
    return len;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    if (st == 0) {
        errno = EINVAL;
        return -1;
    }
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

void *_sbrk(ptrdiff_t incr)
{
    char *prev;
    if (heap_end == 0) {
        heap_end = (char *)&_ebss;
    }
    prev = heap_end;
    if ((heap_end + incr) >= (char *)&_estack) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    return prev;
}

int _gettimeofday(struct timeval *tv, void *tzvp)
{
    (void)tzvp;
    if (tv == 0) {
        errno = EINVAL;
        return -1;
    }
    tv->tv_sec = (time_t)(systick_ms / 1000u);
    tv->tv_usec = (suseconds_t)((systick_ms % 1000u) * 1000u);
    return 0;
}

time_t time(time_t *t)
{
    time_t now = (time_t)(systick_ms / 1000u);
    if (t != 0) {
        *t = now;
    }
    return now;
}

void _exit(int status)
{
    (void)status;
    while (1) {
        __asm volatile("wfi");
    }
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}

void _init(void)
{
}

void _fini(void)
{
}
