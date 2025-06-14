#pragma once

#include "attr.h"
#include "blob.h"

#define L_BUTTON_BLOB_TYPE make_type('L','b','t','n')
#define R_BUTTON_BLOB_TYPE make_type('R','b','t','n')
#define DISC_BLOB_TYPE make_type('D','I','d','v')
#define FLIPPYDRIVE_BLOB_TYPE make_type('F','D','d','v')

extern const blob_header_t *const custom_ui_blob;
