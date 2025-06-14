#pragma once

#define STATE_WAIT_LOAD   0x0f // delay after animation
#define STATE_START_GAME  0x10 // play full animation and start game
#define STATE_NO_DISC     0x12 // play full animation before menu
#define STATE_COVER_OPEN  0x13 // force direct to menu
#define STATE_READ_ERROR  0x16 // 'The disc could not be read' error message
#define STATE_FATAL_ERROR 0x17 // 'An error has occurred' message, UI stops responding to inputs

extern bool is_disc_drive_selected;

void bs2init();
bool bs2_is_switching_device();
