#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <gccore.h>

#include "config.h"

extern s32 InitializeUART(void);
extern s32 WriteUARTN(void *buf, u32 len);

int iprintf(const char *fmt, ...);
