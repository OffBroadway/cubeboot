#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <gctypes.h>
#include "reloc.h"
#include "attr.h"

#include "grid.h"
#include "menu.h"
#include "games.h"

// ===============================================================================

#define MAX_ANIMS 100000
#define START_LINE 0
#define ANIM_DIRECTION_UP 0
#define ANIM_DIRECTION_DOWN 1
#define MAX_LINES 240 // 240 lines * 8 slots = 1920 slots

__attribute_data__ menu_grid_type_t menu_grid_type;

bool grid_setup_done = false;
int columns_per_line = 8;
__attribute_data_empty__ line_backing_t browser_lines[MAX_LINES];

// ===============================================================================

const f32 offset_y = DRAW_OFFSET_Y;

f32 get_position_after(line_backing_t *line_backing) {
    anim_list_t *anims = &line_backing->anims;

    f32 position_y = line_backing->raw_position_y;
    if (anims->direction == ANIM_DIRECTION_UP) {
        return position_y - anims->remaining;
    } else {
        return position_y + anims->remaining;
    }
}

// other stuff
void grid_setup_columns_per_line() {
    switch (menu_grid_type) {
        case MENU_GRID_SQUARE_ICONS:
        default:
            columns_per_line = 8;
            break;

        case MENU_GRID_BANNERS:
            columns_per_line = 3;
            break;

        case MENU_GRID_SMALL_BANNERS:
            columns_per_line = 4;
            break;
    }

    assert(columns_per_line <= MAX_COLUMNS_PER_LINE);
}

void grid_setup_func() {
    OSReport("browser_lines = %p\n", browser_lines);
    OSReport("number_of_lines = %d\n", number_of_lines);

    for (int line_num = 0; line_num < MAX_LINES; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        line_backing->transparency = 0.0;

        anim_list_t *anims = &line_backing->anims;
        anims->pending_count = 0;
        anims->remaining = 0;
    }

    // initial
    selected_slot = START_LINE * columns_per_line;
    top_line_num = START_LINE;

    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        // line_backing->relative_index = line_num;
 
        int row = line_num;
        f32 raw_pos_y = (row * offset_y);
        line_backing->raw_position_y = DRAW_BOUND_TOP - (offset_y * START_LINE) + raw_pos_y;
        line_backing->transparency = 1.0;
        if (line_num < START_LINE || line_num >= START_LINE + DRAW_TOTAL_ROWS) {
            line_backing->transparency = 0.0;
        }

        // anim_list_t *anims = &line_backing->anims;
        // anims->pending_count = 0;
        // anims->remaining = 0;

        // OSReport("Setting line position %d = %f\n", line_num, line_backing->raw_position_y);
    }

    grid_setup_done = true;
    return;
}

void grid_add_anim(int line_num, int direction, f32 distance) {
    line_backing_t *line_backing = &browser_lines[line_num];
    anim_list_t *anims = &line_backing->anims;

    if (anims->pending_count > 0 && anims->direction != direction) {
        anims->pending_count = 0;
        OSReport("ERROR: Clearing anims\n");
    }

    if (anims->pending_count < MAX_ANIMS) {
        anims->pending_count++;
        anims->remaining += distance;
        anims->direction = direction;
    } else {
        OSReport("ERROR: Max anims reached\n");
    }
}


int grid_dispatch_navigate_up() {
    // OSReport("Up pressed %d\n", number_of_lines);

    int max_pending = 0;
    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        anim_list_t *anims = &line_backing->anims;
        if (anims->pending_count > max_pending) {
            max_pending = anims->pending_count;
        }

        if (anims->pending_count > 0 && anims->direction == ANIM_DIRECTION_UP) {
            OSReport("ERROR: Pending up anims\n");
            return GRID_MOVE_FAIL;
        }
    }

    // if (max_pending == MAX_ANIMS) {
    //     OSReport("ERROR: Max anims exceeded\n");
    //     return 1;
    // }

    bool found_move_in = false;
    bool found_move_out = false;
    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        // anim_list_t *anims = &line_backing->anims;

        f32 partial_position_y = get_position_after(line_backing);
        if (!found_move_in && partial_position_y + offset_y + 10 >= DRAW_BOUND_TOP && partial_position_y - 10 < DRAW_BOUND_TOP) {
            // OSReport("ERROR: End position is out of bounds (%d)\n", line_num);
            f32 anim_distance = offset_y * 0.5;
            line_backing->raw_position_y = DRAW_BOUND_TOP - anim_distance;
            line_backing->transparency = 0.25;
            line_backing->moving_in = true;
            line_backing->moving_out = false;

            found_move_in = true;
            // OSReport("Moving in %d\n", line_num);
            grid_add_anim(line_num, ANIM_DIRECTION_DOWN, anim_distance);
        } else if (partial_position_y < (DRAW_BOUND_BOTTOM + offset_y - 10) && partial_position_y >= (DRAW_BOUND_TOP - offset_y + 10)) {
            grid_add_anim(line_num, ANIM_DIRECTION_DOWN, offset_y);
            // OSReport("Adding anim %d, current=%f sub=%f\n", line_num, position_y, offset_y);
        } else {
            line_backing->raw_position_y += offset_y;
        }

        f32 end_position_y = get_position_after(line_backing);
        if (!found_move_out && end_position_y + 10 >= DRAW_BOUND_BOTTOM && end_position_y - offset_y < DRAW_BOUND_BOTTOM) {
            line_backing->transparency = 0.999;
            line_backing->moving_out = true;
            line_backing->moving_in = false;

            found_move_out = true;
            // OSReport("Moving out %d\n", line_num);
            // OSReport("Current position = %f\n", position_y);
            // OSReport("End position = %f\n", end_position_y);
        }

        // // remove boxes that are out of bounds
        // if (line_backing->raw_position_y <= DRAW_BOUND_TOP - (offset_y * 0.4)  || line_backing->raw_position_y >= DRAW_BOUND_BOTTOM + (offset_y * 0.4)) {
        //     line_backing->transparency = 0.0;
        // }
    }

    return GRID_MOVE_SUCCESS;
}

int grid_dispatch_navigate_down() {
    // OSReport("Down pressed %d\n", number_of_lines);

    // check all anims full or empty
    // calculate final pos after all pending anims would complete
    // if final pos is within bounds, add anims

    // int count_visible = 0;
    int max_pending = 0;
    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        anim_list_t *anims = &line_backing->anims;
        if (anims->pending_count > max_pending) {
            max_pending = anims->pending_count;
        }

        // if (line_backing->raw_position_y >= DRAW_BOUND_TOP - 10 && line_backing->raw_position_y < DRAW_BOUND_BOTTOM + 10) {
        //     count_visible++;
        // }

        if (anims->pending_count > 0 && anims->direction == ANIM_DIRECTION_DOWN) {
            OSReport("ERROR: Pending down anims\n");
            return GRID_MOVE_FAIL;
        }
    }

    // if (max_pending == MAX_ANIMS) {
    //     OSReport("ERROR: Max anims exceeded\n");
    //     return 1;
    // }

    // finish pending moves (fixes the visual bug!)
    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        if (line_backing->moving_out) {
            line_backing->transparency *= 0.1;
        }
    }


    bool found_move_in = false;
    bool found_move_out = false;
    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        // anim_list_t *anims = &line_backing->anims;

        f32 partial_position_y = get_position_after(line_backing);
        if (!found_move_in && partial_position_y + 10 >= DRAW_BOUND_BOTTOM && partial_position_y - offset_y - 10 < DRAW_BOUND_BOTTOM) {
            // OSReport("ERROR: End position is out of bounds (%d)\n", line_num);
            f32 anim_distance = offset_y * 0.5;
            line_backing->raw_position_y = DRAW_BOUND_BOTTOM - anim_distance;
            line_backing->transparency = 0.25;
            line_backing->moving_in = true;
            line_backing->moving_out = false;

            found_move_in = true;
            // OSReport("Moving in %d\n", line_num);
            grid_add_anim(line_num, ANIM_DIRECTION_UP, anim_distance);
        } else if (partial_position_y < (DRAW_BOUND_BOTTOM + offset_y - 10) && partial_position_y >= (DRAW_BOUND_TOP - offset_y + 10)) {
            grid_add_anim(line_num, ANIM_DIRECTION_UP, offset_y);
            // OSReport("Adding anim %d, current=%f sub=%f\n", line_num, position_y, offset_y);
        } else {
            line_backing->raw_position_y -= offset_y;
            // if (line_backing->raw_position_y < DRAW_BOUND_BOTTOM - 10 && line_backing->raw_position_y >= DRAW_BOUND_TOP + 10) {
            //     OSReport("ERROR: Moved in-bounds (%d)\n", count_visible);
            //     if (count_visible > 5) {
            //         OSReport("ERROR: Too many visible\n");
            //         while(1);
            //         line_backing->transparency = 0.0;
            //     }
            // }
        }

        f32 end_position_y = get_position_after(line_backing);
        if (!found_move_out && end_position_y + offset_y + 10 >= DRAW_BOUND_TOP && end_position_y < DRAW_BOUND_TOP) {
            line_backing->transparency = 0.999;
            line_backing->moving_out = true;
            line_backing->moving_in = false;

            found_move_out = true;
            // OSReport("Moving out %d\n", line_num);
            // OSReport("Moving out: Current position = %f\n", line_backing->raw_position_y);
            // OSReport("Moving out: End position = %f\n", end_position_y);
            // if (line_backing->raw_position_y - end_position_y > DRAW_OFFSET_Y) {
            //     OSReport("ERROR: Too far out\n");
            //     line_backing->transparency = 0;
            //     // while(1);
            // }
        }
    }

    return GRID_MOVE_SUCCESS;
}

void grid_update_icon_positions() {
    // OSReport("UPDATE\n");
    if (!grid_setup_done) {
        return;
    }

    // only currently showing or "about to be shown"
    // cells get anim entries added

    // other cells get moved by one entire place

    int count_partially_visibile = 0;
    int count_visible = 0;
    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        // anim_list_t *anims = &line_backing->anims;
        if (line_backing->raw_position_y - offset_y >= DRAW_BOUND_TOP && line_backing->raw_position_y + offset_y < DRAW_BOUND_BOTTOM) {
            count_visible++;
            if (line_backing->transparency < 1.0) {
                count_partially_visibile++;
            }
        }
    }

    // OSReport("Partially Visible %d\n", count_partially_visibile);

    for (int line_num = 0; line_num < number_of_lines; line_num++) {
        line_backing_t *line_backing = &browser_lines[line_num];
        anim_list_t *anims = &line_backing->anims;

        f32 multiplier = 3 * 2;
        if (anims->pending_count) {
            multiplier += anims->pending_count;
            if (multiplier > 8) multiplier = 8;
        }

        // transparency
        if (line_backing->transparency < 1.0) {
            f32 delta = 0.01 * multiplier;
            if (line_backing->moving_in) {
                line_backing->transparency += (delta * 0.4);
                if (line_backing->transparency > 1.0) {
                    line_backing->transparency = 1.0;
                }
            }

            if (line_backing->moving_out) {
                line_backing->transparency -= (delta * 0.8);
                if (line_backing->transparency < 0.0) {
                    line_backing->transparency = 0.0;
                    line_backing->moving_out = false;
                }

                // if (count_visible > 5) {
                //     // blip out far away lines
                //     if (line_backing->anims.direction == ANIM_DIRECTION_UP && line_num < number_of_lines - 1) {
                //         line_backing_t *next_line_backing = &browser_lines[line_num+1];
                //         if (next_line_backing->raw_position_y > line_backing->raw_position_y + DRAW_OFFSET_Y - 20) {
                //             line_backing->transparency = 0.0;
                //         }
                //     }
                // }
            }
        }

        if (anims->pending_count) {
            // position
            // if (line_backing->moving_in && count_visible < 4 && line_backing->transparency < 1.0) {
            //     multiplier /= 2;
            // }

            // if (count_visible - count_partially_visibile > 4) {
            //     multiplier *= 12;
            // } else if (count_visible > 6) {
            //     multiplier *= count_visible;
            // }
            
            f32 delta = offset_y * 0.01 * multiplier / 1.5;
            if (delta > anims->remaining) {
                delta = anims->remaining;
            }

            anims->remaining -= delta;
            if (anims->direction == ANIM_DIRECTION_UP) {
                line_backing->raw_position_y -= delta;
            } else {
                line_backing->raw_position_y += delta;
            }

            if (anims->remaining < 0.1) {
                // OSReport("Final pos = %f\n", line_backing->raw_position_y);
                anims->pending_count--;
                anims->remaining = 0;
            }
        }

        // OSReport("Update %d\n ", line_backing->relative_index);
    }
}
