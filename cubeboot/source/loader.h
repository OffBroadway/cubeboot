#include <stdbool.h>

#define MAX_NUM_ARGV 1024

typedef struct cli_params_t {
    int argc;
    char *argv[MAX_NUM_ARGV];
} cli_params;

bool check_load_program();
bool load_program(char *path, cli_params *params);
bool load_cli_file(char *path, cli_params *params);
void boot_program(char *path);
