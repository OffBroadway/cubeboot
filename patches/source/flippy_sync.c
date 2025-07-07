#include <stddef.h> // bug ipc doesn't include this
#include <string.h>
#include "os.h"
#include "ipc.h"

#include "flippy_sync.h"
#include "reloc.h"
#include "time.h"

#ifdef NOSYS
// we don't know where this is defined
extern void DCInvalidateRange(void *startaddress, u32 len);
#endif

// DI regs from YAGCD
#define DI_SR      0 // 0xCC006000 - DI Status Register
#define DI_SR_BRKINT     (1 << 6) // Break Complete Interrupt Status
#define DI_SR_BRKINTMASK (1 << 5) // Break Complete Interrupt Mask. 0:masked, 1:enabled
#define DI_SR_TCINT      (1 << 4) // Transfer Complete Interrupt Status
#define DI_SR_TCINTMASK  (1 << 3) // Transfer Complete Interrupt Mask. 0:masked, 1:enabled
#define DI_SR_DEINT      (1 << 2) // Device Error Interrupt Status
#define DI_SR_DEINTMASK  (1 << 1) // Device Error Interrupt Mask. 0:masked, 1:enabled
#define DI_SR_BRK        (1 << 0) // DI Break

#define DI_CVR     1 // 0xCC006004 - DI Cover Register (status2)
#define DI_CMDBUF0 2 // 0xCC006008 - DI Command Buffer 0
#define DI_CMDBUF1 3 // 0xCC00600c - DI Command Buffer 1 (offset in 32 bit words)
#define DI_CMDBUF2 4 // 0xCC006010 - DI Command Buffer 2 (source length)
#define DI_MAR     5 // 0xCC006014 - DMA Memory Address Register
#define DI_LENGTH  6 // 0xCC006018 - DI DMA Transfer Length Register
#define DI_CR      7 // 0xCC00601c - DI Control Register
#define DI_CR_RW     (1 << 2) // access mode, 0:read, 1:write
#define DI_CR_DMA    (1 << 1) // 0: immediate mode, 1: DMA mode (*1)
#define DI_CR_TSTART (1 << 0) // transfer start. write 1: start transfer, read 1: transfer pending (*2)

#define DI_IMMBUF  8 // 0xCC006020 - DI immediate data buffer (error code ?)
#define DI_CFG     9 // 0xCC006024 - DI Configuration Register

// DI Commands
#define DVD_OEM_INQUIRY 0x12000000
#define DVD_OEM_AUDIO 0xE4000000
#define DVD_OEM_READ 0xA8000000
#define DVD_FLIPPY_BOOTLOADER_STATUS 0xB4000000
#define DVD_FLIPPY_FILEAPI_BASE 0xB5000000

static vu32* const _di_regs = (vu32*)0xCC006000;

// === OEM commands

static GCN_ALIGNED(dvd_info_t) info = { 0 };
dvd_info_t *dvd_inquiry() {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_OEM_INQUIRY;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = ((u32)&info) & 0x1FFFFFFF; // Cached -> Effective
    _di_regs[DI_LENGTH] = sizeof(dvd_info_t);
    _di_regs[DI_CR] = (DI_CR_DMA | DI_CR_TSTART);

    while (_di_regs[DI_CR] & DI_CR_TSTART); // transfer complete register
    
    DCInvalidateRange((u8 *)&info, sizeof(dvd_info_t));

    return &info;
}


void dvd_audio_config(char use_streaming, char size) {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

	if(use_streaming) {
        if (!size) size = 10;
        _di_regs[DI_CMDBUF0] = DVD_OEM_AUDIO | 0x10000 | size;
        _di_regs[DI_CMDBUF1] = 0;
        _di_regs[DI_CMDBUF2] = 0;
	} else {
        _di_regs[DI_CMDBUF0] = DVD_OEM_AUDIO;
        _di_regs[DI_CMDBUF1] = 0;
        _di_regs[DI_CMDBUF2] = 0;
	}

    _di_regs[DI_MAR] = 0;
    _di_regs[DI_LENGTH] = 0;
    _di_regs[DI_CR] = DI_CR_TSTART; // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART); // transfer complete register
}

// === flippy custom commands

void dvd_custom_close(uint32_t fd) {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_CLOSE | ((fd & 0xFF) << 16);
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = 0;
    _di_regs[DI_LENGTH] = 0;
    _di_regs[DI_CR] = DI_CR_TSTART; // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART); // transfer complete register
}

void dvd_set_default_fd(uint32_t current_fd, uint32_t second_fd) {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_SET_DEFAULT_FD | ((current_fd & 0xFF) << 16) | ((second_fd & 0xFF) << 8);
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = 0;
    _di_regs[DI_LENGTH] = 0;
    _di_regs[DI_CR] = DI_CR_TSTART; // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART); // transfer complete register
}

int dvd_custom_write(char *buf, uint32_t offset, uint32_t length, uint32_t fd) {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_WRITE | ((fd & 0xFF) << 16);
    _di_regs[DI_CMDBUF1] = offset;
    _di_regs[DI_CMDBUF2] = length;

    _di_regs[DI_MAR] = (u32)buf & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = length;
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART);
    while (_di_regs[DI_CR] & DI_CR_TSTART) {
        if (ticks_to_millisecs(gettime()) % 1000 == 0) {
            OSReport("Still writing: %u/%u\n", (u32)_di_regs[DI_LENGTH], length);
        }
    }

    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

int dvd_read(void* dst, unsigned int len, uint64_t offset, unsigned int fd) {

    if (offset >> 2 > 0xFFFFFFFF) return -1;

    /* TODO What was going on with this setup code previously? Seems wrong
    if ((((int)dst) & 0xC0000000) == 0x80000000) // cached?
    {
        dvd[0] = 0x2E;
    }
    */
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_OEM_READ | ((fd & 0xFF) << 16);
    _di_regs[DI_CMDBUF1] = offset >> 2;
    _di_regs[DI_CMDBUF2] = len;

    _di_regs[DI_MAR] = (u32)dst & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = len;
    _di_regs[DI_CR] = (DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART); // transfer complete register

    DCInvalidateRange(dst, len);

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

int dvd_read_data(void* dst, unsigned int len, uint64_t offset, unsigned int fd) {
    uint64_t current_offset = offset;
    unsigned int total_read = 0;
    unsigned int remaining = len;

    if(((uint32_t)dst & 0x1F) || (len & 0x1F) || (offset & 0x3)) //Buffer, length, or offset is not aligned
    {
        static GCN_ALIGNED(u8) aligned_buffer[FD_IPC_MAXRESP];

        while (remaining > 0)
        {
            // Ensure the read offset is aligned to 4 bytes
            uint64_t aligned_offset = current_offset & ~0x3;
            uint32_t offset_adjustment = current_offset - aligned_offset;

            // Calculate the amount to read, taking alignment into account
            unsigned int to_read = remaining > FD_IPC_MAXRESP ? FD_IPC_MAXRESP : remaining;
            if (to_read + offset_adjustment > FD_IPC_MAXRESP) {
                to_read = FD_IPC_MAXRESP - offset_adjustment;
            }
            to_read = (to_read + 31) & ~31; // Round up to nearest 32 bytes

            // Perform the read into the aligned buffer
            int result = dvd_read(aligned_buffer, to_read, aligned_offset, fd);
            if (result != 0) {
                custom_OSReport("dvd_read_data failed: %d\n", result);
                return result;  // Return the error code if dvd_read fails
            }

            // Copy the relevant portion from the aligned buffer to destination
            unsigned int to_copy = remaining > FD_IPC_MAXRESP ? FD_IPC_MAXRESP : remaining;
            memcpy(dst + total_read, aligned_buffer + offset_adjustment, to_copy);
            total_read += to_copy;
            current_offset += to_copy;
            remaining -= to_copy;
        }
    }
    else //Buffer, length, and offset are aligned
    {
        int result = dvd_read(dst, len, offset, fd);
        if (result != 0)
        {
            custom_OSReport("dvd_read_data failed: %d\n", result);
            return result; // Return the error code if dvd_read fails
        }
    }

    return 0;
}

static GCN_ALIGNED(file_status_t) status;
file_status_t *dvd_custom_status() {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_READ_STATUS;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = (u32)(&status) & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_status_t);
    _di_regs[DI_CR] = (DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    DCInvalidateRange(&status, sizeof(file_status_t));

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return NULL;
    }
    return &status;
}

int dvd_custom_readdir(file_entry_t* dst, unsigned int fd) {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_READDIR | ((fd & 0xFF) << 16);
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = (u32)dst & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_entry_t);
    _di_regs[DI_CR] = (DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    DCInvalidateRange(dst, sizeof(file_entry_t));

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

int dvd_custom_unlink(char *path) {
    GCN_ALIGNED(file_entry_t) entry;

    strncpy(entry.name, path, 256);
    entry.name[255] = 0;

    DCFlushRange(&entry, sizeof(file_entry_t));

    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_UNLINK;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0; //TODO this was sizeof(file_entry_t) before for no particular reason

    _di_regs[DI_MAR] = (u32)&entry & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_entry_t);
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

int dvd_custom_unlink_flash(char *path) {
    GCN_ALIGNED(file_entry_t) entry;

    strncpy(entry.name, path, 256);
    entry.name[255] = 0;

    DCFlushRange(&entry, sizeof(file_entry_t));

    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_UNLINK_FLASH;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0; //TODO this was sizeof(file_entry_t) before for no particular reason

    _di_regs[DI_MAR] = (u32)&entry & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_entry_t);
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

int dvd_custom_open(const char *path, uint8_t type, uint8_t flags) {
    GCN_ALIGNED(file_entry_t) entry;

    strncpy(entry.name, path, 256);
    entry.name[255] = 0;
    entry.type = type;
    entry.flags = flags;

    DCFlushRange(&entry, sizeof(file_entry_t));

    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_OPEN;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0; //TODO this was sizeof(file_entry_t) before for no particular reason

    _di_regs[DI_MAR] = (u32)&entry & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_entry_t);
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

int dvd_custom_open_flash(const char *path, uint8_t type, uint8_t flags) {
    GCN_ALIGNED(file_entry_t) entry;

    strncpy(entry.name, path, 256);
    entry.name[255] = 0;
    entry.type = type;
    entry.flags = flags;

    DCFlushRange(&entry, sizeof(file_entry_t));

    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_OPEN_FLASH;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0; //TODO this was sizeof(file_entry_t) before for no particular reason

    _di_regs[DI_MAR] = (u32)&entry & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(file_entry_t);
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}

void dvd_custom_bypass_enter() {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = 0xDC000000;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0;

    _di_regs[DI_MAR] = 0;
    _di_regs[DI_LENGTH] = 0;
    _di_regs[DI_CR] = DI_CR_TSTART;

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    return;
}

void dvd_custom_bypass_exit() {
    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = 0xDC000000;
    _di_regs[DI_CMDBUF1] = FD_BYPASS_EXIT_MAGIC0;
    _di_regs[DI_CMDBUF2] = FD_BYPASS_EXIT_MAGIC1;

    _di_regs[DI_MAR] = 0;
    _di_regs[DI_LENGTH] = 0;
    _di_regs[DI_CR] = DI_CR_TSTART;

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    return;
}

int dvd_custom_presence(bool playing, const char *status, const char *sub_status)
{
    GCN_ALIGNED(flippydrive_net_presence_t) presence = {};

    strncpy(presence.status, status, sizeof(presence.status)-1);
    strncpy(presence.sub_status, sub_status, sizeof(presence.sub_status)-1);

    presence.presence = playing ? 0x01 : 0x00;

    DCFlushRange(&presence, sizeof(flippydrive_net_presence_t));

    _di_regs[DI_SR] = (DI_SR_BRKINTMASK | DI_SR_TCINTMASK | DI_SR_DEINT | DI_SR_DEINTMASK);
    _di_regs[DI_CVR] = 0; // clear cover int

    _di_regs[DI_CMDBUF0] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_OPEN_FLASH;
    _di_regs[DI_CMDBUF1] = 0;
    _di_regs[DI_CMDBUF2] = 0; //TODO this was sizeof(file_entry_t) before for no particular reason

    _di_regs[DI_MAR] = (u32)&presence & 0x1FFFFFFF;
    _di_regs[DI_LENGTH] = sizeof(flippydrive_net_presence_t);
    _di_regs[DI_CR] = (DI_CR_RW | DI_CR_DMA | DI_CR_TSTART); // start transfer

    while (_di_regs[DI_CR] & DI_CR_TSTART)
        ; // transfer complete register

    // check if ERR was asserted
    if (_di_regs[DI_SR] & DI_SR_DEINT) {
        return 1;
    }
    return 0;
}
