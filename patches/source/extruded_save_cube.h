#pragma once

#include "structs.h"

void set_up_extruded_cube_vertices(model_data *textured_model_data, model_data *untextured_model_data);

void use_original_save_cubes(model_data *textured_model_data, model_data *untextured_model_data);
void use_extruded_save_cubes(model_data *textured_model_data, model_data *untextured_model_data);
