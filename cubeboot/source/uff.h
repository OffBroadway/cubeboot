#include "config.h"

#ifdef USE_SMALL_FATFS
#define uf_mount pf_mount
#define uf_open pf_open
#define uf_read pf_read
#define uf_write pf_write
#define uf_lseek pf_lseek
#define uf_opendir pf_opendir
#define uf_readdir pf_readdir
#define uf_size pf_size
#else

#endif