/*-------------------------------------------------------------

exception_handler.S -- PPC exception handling support

Copyright (C) 2004
Michael Wiedenbauer (shagkur)
Dave Murphy (WinterMute)

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/


#include "asm.h"

	.global systemcallhandler_start,systemcallhandler_end
systemcallhandler_start:
	mtspr	SPRG2,r9
	mtspr	SPRG3,r10
	mfspr	r9,HID0
	ori		r10,r9,0x0008
	mtspr	HID0,r10
	isync
	sync
	mtspr	HID0,r9
	mfspr	r9,SPRG2
	mfspr	r10,SPRG3
	rfi
systemcallhandler_end:
	nop
