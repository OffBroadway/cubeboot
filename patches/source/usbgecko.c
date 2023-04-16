/* 
 * Copyright (c) 2019-2021, Extrems <extrems@extremscorner.org>
 * 
 * This file is part of Swiss.
 * 
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "tinyprintf/tinyprintf.h"
#include "usbgecko.h"

#include "config.h"

#ifdef DEBUG

u32 *uart_init = (u32*)0x81481c70;
 
u32 (*InitializeUART)(u32) = (u32 (*)(u32))0x81360920;
s32 (*orig_WriteUARTN)(const void *buf, u32 len) = (s32 (*)(const void *, u32))0x81360970;

s32 WriteUARTN(const void *buf, u32 len)
{
  if (*uart_init == 0) {
	InitializeUART(0xe100);
    *uart_init = 1;
  }

	return orig_WriteUARTN(buf, len);
}

void usb_OSReport(const char *fmt, ...) {
	va_list args;
    static char buf[256];

    va_start(args, fmt);
    int length = vsprintf((char *)buf, (char *)fmt, args);

	WriteUARTN(buf, length);

    va_end(args);
}

#else

void usb_OSReport(const char *fmt, ...) {
	(void)fmt;
}

#endif
