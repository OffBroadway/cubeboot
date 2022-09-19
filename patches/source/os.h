#ifndef OS_H
#define OS_H

#include <gctypes.h>

#define OSRoundUp32B(x)   (((u32)(x) + (32 - 1)) & ~(32 - 1))
#define OSRoundDown32B(x) ((u32)(x) & ~(32 - 1))

#define OS_BASE_CACHED   0x80000000
#define OS_BASE_UNCACHED 0xC0000000

#define OSPhysicalToCached(paddr)    ((void *)(paddr) + OS_BASE_CACHED)
#define OSPhysicalToUncached(paddr)  ((void *)(paddr) + OS_BASE_UNCACHED)
#define OSCachedToPhysical(caddr)    ((u32)(caddr) - OS_BASE_CACHED)
#define OSUncachedToPhysical(ucaddr) ((u32)(ucaddr) - OS_BASE_UNCACHED)
#define OSCachedToUncached(caddr)    ((void *)(caddr) + (OS_BASE_UNCACHED - OS_BASE_CACHED))
#define OSUncachedToCached(ucaddr)   ((void *)(ucaddr) - (OS_BASE_UNCACHED - OS_BASE_CACHED))

typedef s64 OSTime;
typedef u32 OSTick;

#define OS_CORE_CLOCK  486000000
#define OS_BUS_CLOCK   162000000
#define OS_TIMER_CLOCK (OS_BUS_CLOCK / 4)

#define OSTicksToCycles(ticks)       (((ticks) * ((OS_CORE_CLOCK * 2) / OS_TIMER_CLOCK)) / 2)
#define OSTicksToSeconds(ticks)      ((ticks) / OS_TIMER_CLOCK)
#define OSTicksToMilliseconds(ticks) ((ticks) / (OS_TIMER_CLOCK / 1000))
#define OSTicksToMicroseconds(ticks) (((ticks) * 8) / (OS_TIMER_CLOCK / 125000))
#define OSTicksToNanoseconds(ticks)  (((ticks) * 8000) / (OS_TIMER_CLOCK / 125000))
#define OSSecondsToTicks(sec)        ((sec)  * OS_TIMER_CLOCK)
#define OSMillisecondsToTicks(msec)  ((msec) * (OS_TIMER_CLOCK / 1000))
#define OSMicrosecondsToTicks(usec)  (((usec) * (OS_TIMER_CLOCK / 125000)) / 8)
#define OSNanosecondsToTicks(nsec)   (((nsec) * (OS_TIMER_CLOCK / 125000)) / 8000)

#define OSDiffTick(tick1, tick0) ((s32)(tick1) - (s32)(tick0))

static OSTick OSGetTick(void)
{
	return __builtin_ppc_mftb();
}

static OSTime OSGetTime(void)
{
	return __builtin_ppc_get_timebase();
}

typedef struct OSAlarm OSAlarm;
typedef struct OSContext OSContext;

typedef void (*OSAlarmHandler)(OSAlarm *alarm, OSContext *context);

struct OSAlarm {
	OSAlarmHandler handler;
	u32 tag;
	OSTime fire;
	OSAlarm *prev;
	OSAlarm *next;
	OSTime period;
	OSTime start;
};

static void OSCreateAlarm(OSAlarm *alarm)
{
	alarm->handler = 0;
}

extern void (*OSSetAlarm)(OSAlarm *alarm, OSTime tick, OSAlarmHandler handler);
extern void (*OSCancelAlarm)(OSAlarm *alarm);

static void DCBlockZero(void *addr)
{
	asm("dcbz %y0" : "=Z" (*(char(*)[32])addr) :: "memory");
}

static void DCBlockStore(void *addr)
{
	asm("dcbst %y0" : "=Z" (*(char(*)[32])addr) :: "memory");
}

static void DCBlockFlush(void *addr)
{
	asm("dcbf %y0" : "=Z" (*(char(*)[32])addr) :: "memory");
}

static void DCBlockInvalidate(void *addr)
{
	asm("dcbi %y0" : "=Z" (*(char(*)[32])addr) :: "memory");
}

void DCInvalidateRange(void *addr, u32 nBytes);
void DCFlushRange(void *addr, u32 nBytes);
void DCFlushRangeNoSync(void *addr, u32 nBytes);
void DCStoreRangeNoSync(void *addr, u32 nBytes);
void DCZeroRange(void *addr, u32 nBytes);
void ICInvalidateRange(void *addr, u32 nBytes);

static void ICBlockInvalidate(void *addr)
{
	asm("icbi %y0" : "=Z" (*(char(*)[32])addr) :: "memory");
}

#endif /* OS_H */
