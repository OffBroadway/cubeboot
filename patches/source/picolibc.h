#include <stddef.h>
#include <stdarg.h>

int vsnprintf(char* s, size_t n, const char* format, va_list arg);

void *memmove(void *dst, const void *src, size_t length);
void *memcpy(void* dst, const void* src, size_t count);
char *strcpy (char *dst0, const char *src0);
size_t strlen (const char *str);
