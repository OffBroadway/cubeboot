#include "extruded_save_cube.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <gctypes.h>

#include "attr.h"
#include "os.h"

typedef struct vtx1_header_t {
    u32 magic; // 'VTX1'
    u32 size;
    u8 *ptrs[14]; // After initialization, these point to within the VTX1 data
} vtx1_header_t;

typedef struct {
    vtx1_header_t header;
    u32 unk0[16];
    f32 vertices[348];
    u8 unk1[496];
} textured_cube_vtx1_t;

static_assert(sizeof(textured_cube_vtx1_t) == 2016);

typedef struct {
    vtx1_header_t header;
    u32 unk0[16];
    f32 vertices[336];
    u8 unk1[480];
} untextured_cube_vtx1_t;

static_assert(sizeof(untextured_cube_vtx1_t) == 1952);

static textured_cube_vtx1_t *original_textured_save_cube_vertices;
static untextured_cube_vtx1_t *original_untextured_save_cube_vertices;

static textured_cube_vtx1_t extruded_textured_save_cube_vertices;
static untextured_cube_vtx1_t extruded_untextured_save_cube_vertices;

static void extrude_cube_vertices_on_x_axis(float *vertices, size_t vertex_count, float extrusion_amount) {
    for (int i = 0; i < vertex_count; i += 3) {
        if (vertices[i] > 0) {
            vertices[i] += extrusion_amount;
        } else {
            vertices[i] -= extrusion_amount;
        }
    }
}

static void fixup_vertex_pointers(vtx1_header_t *header, const vtx1_header_t *original_header) {
    // This assumes that the model data's pointers have already been set up, e.g. within `menu_init()`
    const ptrdiff_t ptr_difference = (u8 *)header - (const u8 *)original_header;

    for (int i = 0; i < countof(header->ptrs); i++) {
        if (header->ptrs[i] == NULL) {
            continue;
        }

        header->ptrs[i] += ptr_difference;
    }
}

void set_up_extruded_cube_vertices(model_data *textured_model_data, model_data *untextured_model_data) {
    // This takes the original vertex buffers for the textured and non-textured
    // cube models, and makes an extruded copy with 3x the original texture width
    // This allows them to hold a 3:1-aspect banner image

    // First, keep a pointer to the original vertex buffers (so we can swap between them)
    original_textured_save_cube_vertices = (textured_cube_vtx1_t *)textured_model_data->vertices;
    original_untextured_save_cube_vertices = (untextured_cube_vtx1_t *)untextured_model_data->vertices;

    // Copy vertex buffers
    memcpy(&extruded_textured_save_cube_vertices, original_textured_save_cube_vertices, sizeof(extruded_textured_save_cube_vertices));
    memcpy(&extruded_untextured_save_cube_vertices, original_untextured_save_cube_vertices, sizeof(extruded_untextured_save_cube_vertices));

    // Update our copy's pointers, so that they point within this copy
    fixup_vertex_pointers(&extruded_textured_save_cube_vertices.header, &original_textured_save_cube_vertices->header);
    fixup_vertex_pointers(&extruded_untextured_save_cube_vertices.header, &original_untextured_save_cube_vertices->header);

    // We need to scale the width of the texture's plane by 3, and keep the same padding around the cube
    // As we know the max coordinate indices ahead of time, let's use them (rather than traversing them all at runtime)
    float texture_plane_max_coordinate = extruded_textured_save_cube_vertices.vertices[336]; // approx. 13.85
    float textured_cube_max_coordinate = extruded_textured_save_cube_vertices.vertices[166]; // approx. 16.76
    float textured_extrusion_amount = texture_plane_max_coordinate * 2.0f;

    // For the untextured cube, we want to scale it by the same effective amount
    // (taking into the account that it's smaller than the textured cube)
    float untextured_cube_max_coordinate = extruded_untextured_save_cube_vertices.vertices[166]; // approx. 12.10
    float untextured_cube_scale = untextured_cube_max_coordinate / textured_cube_max_coordinate;
    float untextured_extrusion_amount = untextured_cube_scale * textured_extrusion_amount;

    // Finally, perform the actual extrusion
    extrude_cube_vertices_on_x_axis(extruded_textured_save_cube_vertices.vertices, countof(extruded_textured_save_cube_vertices.vertices), textured_extrusion_amount);
    extrude_cube_vertices_on_x_axis(extruded_untextured_save_cube_vertices.vertices, countof(extruded_untextured_save_cube_vertices.vertices), untextured_extrusion_amount);
}

void use_original_save_cubes(model_data *textured_model_data, model_data *untextured_model_data) {
    textured_model_data->vertices = &original_textured_save_cube_vertices->header;
    untextured_model_data->vertices = &original_untextured_save_cube_vertices->header;
}

void use_extruded_save_cubes(model_data *textured_model_data, model_data *untextured_model_data) {
    textured_model_data->vertices = &extruded_textured_save_cube_vertices.header;
    untextured_model_data->vertices = &extruded_untextured_save_cube_vertices.header;
}
