#include <stdint.h>

struct gcm_disk_info {
	char game_code[4];
	char maker_code[2];
	char disk_id;
	char version;
	char audio_streaming;
	char stream_buffer_size;
	char unused_1[18];
	uint32_t magic;	/* 0xc2339f3d */
} __attribute__ ((__packed__));

struct gcm_disk_layout {
	uint32_t dol_offset;
	uint32_t fst_offset;
	uint32_t fst_size;
	uint32_t fst_max_size;
	uint32_t user_offset;
	uint32_t user_size;
	uint32_t disk_size;
	char unused_3[4];
} __attribute__ ((__packed__));

/* "boot.bin" */
struct gcm_disk_header {
	struct gcm_disk_info info;
	char game_name[992];
	uint32_t debug_monitor_offset;
	uint32_t debug_monitor_address;
	char unused_2[24];
	struct gcm_disk_layout layout;
} __attribute__ ((__packed__));
