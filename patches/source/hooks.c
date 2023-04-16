#include "dolphin_os.h"
#include "os.h"

#include "tinyprintf/tinyprintf.h"
#include "picolibc.h"

#include <ogc/machine/processor.h>

#define __attribute_data__ __attribute__((section(".data")))
#define __attribute_reloc__ __attribute__((section(".reloc")))

typedef struct menu_entry_pairs {
    u8 type;
    f32 speed;
} menu_entry_pairs;

__attribute_data__ OSMessageQueue aud_queue;
__attribute_data__ OSMessage aud_message;

__attribute_data__ u32 temp_total_size = 0;
__attribute_data__ u8 *temp_dump_head = (u8*)0x80004000;

__attribute_data__ u32 is_capturing = 0;
__attribute_data__ u32 is_playing_track = 0;

__attribute_reloc__ void (*Jac_PlaySe)(u32 sound);
__attribute_reloc__ u32 (*Jaq_CloseTrack)(u32 unk);
__attribute_reloc__ void (*Jac_StopSoundAll)(void);
__attribute_reloc__ void (*Jac_PlayMenuEntry)(u8 type, f32 speed);

__attribute_reloc__ void (*AIInitDMA)(u32 addr, u32 length);

__attribute_reloc__ void (*OSInitMessageQueue)(OSMessageQueue *queue, OSMessage* messages, int message_count);
__attribute_reloc__ void (*OSReceiveMessage)(OSMessageQueue *queue, OSMessage message, int flags);
__attribute_reloc__ void (*OSSendMessage)(OSMessageQueue *queue, OSMessage message, int flags);
__attribute_reloc__ void (*OSDumpHeap)();

void play_sfx_hook(u32 track) {
    OSReport("HOOK: play_sfx_hook\n");
    is_playing_track = 1;
    return;
}

u32 close_sfx_track_hook(u32 trackptr) {
    u32 output = Jaq_CloseTrack(trackptr);
    if (is_playing_track == 1) {
        OSReport("HOOK: close_sfx_track_hook\n");
        is_playing_track = 0;

        OSMessage msg;
        OSSendMessage(&aud_queue, &msg, OS_MESSAGE_NOBLOCK);
    }
    return output;
}

void ai_dma_hook(u32 addr, u32 length) {
    // OSReport("HOOK: ai_dma_hook\n");
    if (is_capturing == 1) {
        memcpy(temp_dump_head, (void*)addr, length);

        temp_total_size += length;
        temp_dump_head += length;
    }

    AIInitDMA(addr, length);
    return;
}

// #define FTOA_PRECISION 1000000
// static const char *cheap_ftoa(float x) {
//   static char buf[20];
//   int c = (x * FTOA_PRECISION) + 0.5; // handle rounding
//   int d = c / FTOA_PRECISION;         // the integer part
//   int f = c - (d * FTOA_PRECISION);   // the fractional part
//   tfp_sprintf(buf, "%d.%06d", d, f);      // the "06 " must equal log10(FTOA_PRECISION)
//   return buf;
// }

// void menu_entry_snd_hook(u8 type, f32 speed) {
//     OSReport("HOOK: menu_entry_snd_hook(%u, %s)\n", type, cheap_ftoa(speed));

//     Jac_PlayMenuEntry(type, speed);
//     return;
// }

// helpers
void msleep(u32 msec) {
    u32 now = OSGetTick();
    while(OSTicksToMilliseconds(OSDiffTick(OSGetTick(), now)) < msec);
    return;
}

void mega_trap(u32 r3, u32 r4, u32 r5, u32 r6) {
    u32 caller = (u32)__builtin_return_address(0);
    OSReport("(0) [%08x] (r3=%08x r4=%08x r5=%08x, r6=%08x)\n", caller, r3, r4, r5, r6);

    // caller = (u32)__builtin_return_address(1);
    // OSReport("(1) [%08x] (...)\n", caller);

    // caller = (u32)__builtin_return_address(2);
    // OSReport("(2) [%08x] (...)\n", caller);

    // caller = (u32)__builtin_return_address(3);
    // OSReport("(3) [%08x] (...)\n", caller);

    OSReport("You hit the mega trap dog\n");
    while(1);
}

// main
void audio_extract_main() {
    OSReport("CALL: audio_extract_main\n");

    *(u32*)temp_dump_head = 0xFEEDFACE;
    OSReport("Dump = %08x\n", *temp_dump_head);
    temp_dump_head += 32;
    OSDumpHeap();

    OSInitMessageQueue(&aud_queue, &aud_message, 1);

    // entry table
    // call snd (0, 0.624000)
    // call snd (0, 0.810300)
    // call snd (0, 0.972000)
    // call snd (0, 1.000000)
    // call snd (0, 1.000000)
    // call snd (0, 1.000000)
    // call snd (1, 0.983200)
    // call snd (1, 0.904600)
    // call snd (1, 0.824000)
    // call snd (1, 0.717600)
    // call snd (1, 0.596400)

    u32 entry_table_len = 9;
    menu_entry_pairs entry_table[] = {
        {0, 0.624000},
        {0, 0.810300},
        {0, 0.972000},
        {0, 1.000000},
        {1, 0.983200},
        {1, 0.904600},
        {1, 0.824000},
        {1, 0.717600},
        {1, 0.596400},
    };

    for (u32 i = 0; i < entry_table_len; i++) {
        OSReport("ENTRY Play: %u\n", i);
        // is_playing_track = 1;

        OSReport("Dump @ %p\n", temp_dump_head);

        menu_entry_pairs entry = entry_table[i];

        u32 length = (u32)OSSecondsToTicks(entry.speed);
        u32 before = OSGetTick();

        is_capturing = 1;
        Jac_PlayMenuEntry(entry.type, entry.speed);
        while(OSDiffTick(OSGetTick(), before) < length);

        msleep(25); // buffer room
        is_capturing = 0;
    }

    // table
    // bad single inst: 0, 1, 2
    // wait for first CloseTrack: 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 19, 20, 21, 22
    // wait for first CloseTrack then StopSoundAll: 17, 18
    for (u32 i = 3; i <= 22; i++) {
        OSReport("Dump @ %p\n", temp_dump_head);

        switch(i) {
            case 0:
            case 1:
            case 2:
            {
                OSReport("WAV\n");
                u32 length = 5846625; // (187092 * 1000 * 1000) / 32000
                u32 before = OSGetTick();

                is_capturing = 1;

                // play sound
                Jac_PlaySe(i);

                while(OSTicksToMicroseconds(OSDiffTick(OSGetTick(), before)) < length);
                OSReport("SLEEP: Track done!\n");

                Jac_StopSoundAll();
                is_capturing = 0;

                msleep(25);
                OSReport("SLEEP: Jac reset\n");
            }
            break;
            case 17:
            case 18:
            {
                OSReport("Weird artifact\n");

                is_capturing = 1;

                // play sound
                Jac_PlaySe(i);

                OSMessage msg;
                OSReceiveMessage(&aud_queue, &msg, OS_MESSAGE_BLOCK);
                OSReport("MUX: Track done!\n");

                Jac_StopSoundAll();
                is_capturing = 0;

                msleep(25);
                OSReport("SLEEP: Jac reset\n");
            }
            break;
            default:
            {
                OSReport("Normal SFX\n");

                is_capturing = 1;

                // play sound
                Jac_PlaySe(i);

                OSMessage msg;
                OSReceiveMessage(&aud_queue, &msg, OS_MESSAGE_BLOCK);
                OSReport("MUX: Track done!\n");

                is_capturing = 0;
                msleep(25); // buffer room
            }
        }

        // break; // early exit
    }

    OSReport("Total data size = %x\n", temp_total_size);
    OSReport("DONE\n");

    OSReport("Dump @ %p\n", temp_dump_head);
    OSDumpHeap();

    ppchalt();
    __builtin_unreachable();
}
