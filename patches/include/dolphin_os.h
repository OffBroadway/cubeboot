#include <gctypes.h>
#include "config.h"

// extern void OSReport(const char* text, ...);
#ifdef DEBUG
extern void (*OSReport)(const char* text, ...);
#else
#define OSReport(...)
#endif

// Flags to turn blocking on/off when sending/receiving message
#define OS_MESSAGE_NOBLOCK 0
#define OS_MESSAGE_BLOCK 1

struct OSThread {
    u8 unk[792];
};

struct OSThreadQueue {
    struct OSThread * head;
    struct OSThread * tail;
};

struct OSMessageQueue {
    struct OSThreadQueue sending_queue;
    struct OSThreadQueue receiving_queue;
    void **message_array;
    s32 num_messages;
    s32 first_index;
    s32 num_used;
};

typedef struct OSMessageQueue OSMessageQueue;
typedef void* OSMessage;

// extern void OSInitMessageQueue(OSMessageQueue *queue, OSMessage* messages, int message_count);
// extern void OSReceiveMessage(OSMessageQueue *queue, OSMessage message, int flags);
// extern void OSSendMessage(OSMessageQueue *queue, OSMessage message, int flags);

extern u32 __OSBusClock;
extern u64 __OSGetSystemTime(void);
