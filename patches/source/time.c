#include <gctypes.h>

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