#include <stdint.h>
#include <gctypes.h>

typedef bool (*dvd_should_cancel_callback)();

int dvd_threaded_read(void* dst, unsigned int len, uint64_t offset, unsigned int fd, dvd_should_cancel_callback should_cancel);
int dvd_threaded_read_id(dvd_should_cancel_callback should_cancel);
void dvd_threaded_audio_config(char use_streaming, char size);
unsigned int dvd_threaded_get_error();
void dvd_threaded_stop_motor();
void dvd_threaded_reset();
