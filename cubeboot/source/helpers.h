#include <stdbool.h>

#define PPC_NOP 0x60000000
#define PPC_BLR 0x4e800020

#define ROUND_UP_1K(x) (((x) + 0x400) & ~(0x400 - 1))

extern void udelay(int us);

u32 generate_random_color();
bool is_dolphin();
int ensdwith(const char *str, const char *suffix);
size_t arrlen(char **arr);
