/*
 * gcm.h
 *
 * GameCube Master
 * Copyright (C) 2005-2006 The GameCube Linux Team
 * Copyright (C) 2005,2006 Albert Herranz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

#ifndef __GCM_H
#define __GCM_H

#include <stdint.h>

#define GCM_MAGIC		0xc2339f3d

#define GCM_OPENING_BNR		"opening.bnr"

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

/* "bi2.bin" */
struct gcm_disk_header_info {
	uint32_t debug_monitor_size;
	uint32_t simulated_memory_size;
	uint32_t argument_offset;
	uint32_t debug_flag;
	uint32_t track_location;
	uint32_t track_size;
	uint32_t country_code;
	uint32_t unknown_1;
} __attribute__ ((__packed__));

/* "appldr.bin" */
struct gcm_apploader_header {
	char date[10];
	char padding_1[6];
	uint32_t entry_point;
	uint32_t size;
	uint32_t trailer_size;
	uint32_t unknown_1;
} __attribute__ ((__packed__));

struct gcm_file_entry {
	union {
		uint8_t flags;
		struct {
			uint32_t fname_offset;
			uint32_t file_offset;
			uint32_t file_length;
		} file;
		struct {
			uint32_t fname_offset;
			uint32_t parent_directory_offset;
			uint32_t this_directory_offset;
		} dir;
		struct {
			uint32_t zeroed_1;
			uint32_t zeroed_2;
			uint32_t num_entries;
		} root_dir;
	};
} __attribute__ ((__packed__));

struct gcm_system_area {
	struct gcm_disk_header dh;
	struct gcm_disk_header_info dhi;

	struct gcm_apploader_header al_header;
	// void *al_image;
	// off_t al_size;		/* aligned to 32 bytes */

	// void *fst_image;
	// off_t fst_size;		/* aligned to 32 bytes */

	// void *bnr_image;
	// off_t bnr_size;		/* aligned to 32 bytes */
};


#define DI_SECTOR_SIZE		2048
#define DI_ALIGN		31

#define SYSTEM_AREA_SIZE	(16*DI_SECTOR_SIZE)

/*
 *
 */
static inline int di_align_size(int size)
{
	return ((size + 31) & ~31);
}

#endif /* __GCM_H */

