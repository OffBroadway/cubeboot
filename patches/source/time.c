#include <gctypes.h>
#include "time.h"

u32 gettick(void) {
	u32 result;
	__asm__ __volatile__ (
		"mftb	%0\n"
		: "=r" (result)
	);
	return result;
}

u64 gettime(void) {
	u32 tmp;
	union uulc {
		u64 ull;
		u32 ul[2];
	} v;

	__asm__ __volatile__(
		"1:	mftbu	%0\n\
		    mftb	%1\n\
		    mftbu	%2\n\
			cmpw	%0,%2\n\
			bne		1b\n"
		: "=r" (v.ul[0]), "=r" (v.ul[1]), "=&r" (tmp)
	);
	return v.ull;
}

u32 diff_sec(u64 start,u64 end) {
	u64 diff;

	diff = diff_ticks(start,end);
	return ticks_to_secs(diff);
}

u32 diff_msec(u64 start,u64 end) {
	u64 diff;

	diff = diff_ticks(start,end);
	return ticks_to_millisecs(diff);
}

u32 diff_usec(u64 start,u64 end) {
	u64 diff;

	diff = diff_ticks(start,end);
	return ticks_to_microsecs(diff);
}

u32 diff_nsec(u64 start,u64 end) {
	u64 diff;

	diff = diff_ticks(start,end);
	return ticks_to_nanosecs(diff);
}

// this function spins till timeout is reached
void udelay(unsigned us) {
	unsigned long long start, end;
	start = gettime();
	while (1) {
		end = gettime();
		if (diff_usec(start,end) >= us)
			break;
	}
}
