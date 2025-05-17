#include <stdint.h>
#include <gctypes.h>

int dvd_threaded_read(void* dst, unsigned int len, uint64_t offset, unsigned int fd);
int dvd_threaded_read_id();
unsigned int dvd_threaded_get_error(void);
void dvd_threaded_reset();
