import os
from gclib.bti import BTI
from gclib.gx_enums import *

# This script generates BTI textures from source PNGs
# (while SVGs are available too, they must be converted back to PNG first after any edits)

data_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)), "../data/")
png_dir = os.path.join(data_dir, "original_png/")


def create_ui_bti(image_path: str, image_format: ImageFormat) -> BTI:
    bti = BTI()

    bti.image_format = image_format
    bti.alpha_setting = u8(0)

    bti.wrap_s = WrapMode.ClampToEdge
    bti.wrap_t = WrapMode.ClampToEdge

    bti.palettes_enabled = False
    bti.palette_format = PaletteFormat.IA8
    bti.num_colors = 0
    bti.palette_data_offset = u32(0)

    bti.min_filter = FilterMode.Linear
    bti.mag_filter = FilterMode.Linear

    bti.min_lod = u8(0)
    bti.max_lod = u8(0)
    bti.mipmap_count = u8(1)
    bti.unknown_3 = u8(0)
    bti.lod_bias = u8(0)

    bti.replace_image_from_path(image_path)
    bti.save_changes()
    return bti


def convert_ui_texture_to_bti(image_name: str, image_format: ImageFormat):
    png_path = os.path.join(png_dir, image_name + ".png")
    bti_path = os.path.join(data_dir, image_name + ".bti")

    bti = create_ui_bti(png_path, image_format)
    with open(bti_path, "wb") as f:
        bti.data.seek(0)
        f.write(bti.data.read())


if __name__ == "__main__":
    convert_ui_texture_to_bti("disc", ImageFormat.I4)
    convert_ui_texture_to_bti("flippydrive", ImageFormat.I4)
    convert_ui_texture_to_bti("l_button", ImageFormat.I4)
    convert_ui_texture_to_bti("r_button", ImageFormat.I4)
