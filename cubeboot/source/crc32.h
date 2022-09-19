#include <stdint.h>

/**
   @file
   CRC32 support.
*/

/**
   Calculate checksum for a given memory area.
   @param[in] addr memory address
   @param[in] length length of memory to do checksum on
   @return checksum
*/
uint32_t csp_crc32_memory(const uint8_t * addr, uint32_t length);
