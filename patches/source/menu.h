#define MENU_SELECTION_ID 0
#define MENU_GAMESELECT_ID 1
#define MENU_GAMESELECT_TRANSITION_ID 2

#define SUBMENU_GAMESELECT_LOADER 0
#define SUBMENU_GAMESELECT_START 1

#define SAVE_COLOR_BLUE 0
#define SAVE_COLOR_GREEN 1
#define SAVE_COLOR_YELLOW 2
#define SAVE_COLOR_ORANGE 3
#define SAVE_COLOR_RED 4
#define SAVE_COLOR_PURPLE 5

#define SAVE_ICON_SEL 0
#define SAVE_ICON 1
#define SAVE_EMPTY_SEL 2
#define SAVE_EMPTY 3

#define SOUND_SUBMENU_ENTER 0x0c
#define SOUND_SUBMENU_EXIT 0x07
#define SOUND_MENU_ENTER 0x05
#define SOUND_MENU_FINAL 0x16
#define SOUND_CARD_MOVE 0x0b
#define SOUND_CARD_ERROR 0x0d

void custom_gameselect_init();
void update_icon_positions();

// TODO: void custom_gameselect_controls() - in progress
// TODO: void custom_gameselect_update() - in progress
