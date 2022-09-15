#include "os.h"

#if 0
void DCInvalidateRange(void *addr, u32 nBytes)
{
	if (!nBytes) return;
	nBytes = OSRoundUp32B(((u32)(addr) & (32 - 1)) + nBytes);
	int i = nBytes / 32;

	do {
		DCBlockInvalidate(addr);
		addr += 32;
	} while (--i);
}
#endif

void DCFlushRange(void *addr, u32 nBytes)
{
	if (!nBytes) return;
	nBytes = OSRoundUp32B(((u32)(addr) & (32 - 1)) + nBytes);
	int i = nBytes / 32;

	do {
		DCBlockFlush(addr);
		addr += 32;
	} while (--i);

	asm volatile("sc" ::: "r9", "r10");
}

#if 0
void DCFlushRangeNoSync(void *addr, u32 nBytes)
{
	if (!nBytes) return;
	nBytes = OSRoundUp32B(((u32)(addr) & (32 - 1)) + nBytes);
	int i = nBytes / 32;

	do {
		DCBlockFlush(addr);
		addr += 32;
	} while (--i);
}

void DCStoreRangeNoSync(void *addr, u32 nBytes)
{
	if (!nBytes) return;
	nBytes = OSRoundUp32B(((u32)(addr) & (32 - 1)) + nBytes);
	int i = nBytes / 32;

	do {
		DCBlockStore(addr);
		addr += 32;
	} while (--i);
}

void DCZeroRange(void *addr, u32 nBytes)
{
	if (!nBytes) return;
	nBytes = OSRoundUp32B(((u32)(addr) & (32 - 1)) + nBytes);
	int i = nBytes / 32;

	do {
		DCBlockZero(addr);
		addr += 32;
	} while (--i);
}

void ICInvalidateRange(void *addr, u32 nBytes)
{
	if (!nBytes) return;
	nBytes = OSRoundUp32B(((u32)(addr) & (32 - 1)) + nBytes);
	int i = nBytes / 32;

	do {
		ICBlockInvalidate(addr);
		addr += 32;
	} while (--i);
}
#endif
