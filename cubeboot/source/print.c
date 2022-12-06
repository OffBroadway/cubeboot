#include "print.h"

#ifdef DOLPHIN_PRINT_ENABLE
int iprintf(const char *fmt, ...) {
    va_list args;
    unsigned long length;
    static char buf[256];
    uint32_t level;

    level = IRQ_Disable();
    va_start(args, fmt);
    length = vsprintf((char *)buf, (char *)fmt, args);

    write(2, buf, length);

	int index = 0;
    for (char *s = &buf[0]; *s != '\x00'; s++) {
        if (*s == '\n')
            *s = '\r';
        int err = WriteUARTN(s, 1);
        if (err != 0) {
            printf("UART ERR: %d\n", err);
        }
        index++;
    }

    va_end(args);
    IRQ_Restore(level);

	return length;
}
#elif defined(CONSOLE_ENABLE) || defined(GECKO_PRINT_ENABLE)
int iprintf(const char *fmt, ...) {
    va_list args;
    unsigned long length;
    static char buf[256];
    uint32_t level;

    level = IRQ_Disable();
    va_start(args, fmt);
    length = vsprintf((char *)buf, (char *)fmt, args);

    write(2, buf, length);

    va_end(args);
    IRQ_Restore(level);

	return length;
}
#else
#define iprintf(...)
#endif
