import os
from gclib import texture_utils
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


def convert_icon_texture_to_bin(image_name: str, image_format: ImageFormat):
    png_path = os.path.join(png_dir, image_name + ".png")
    bin_path = os.path.join(data_dir, image_name + ".bin")

    image_data, palette_data, encoded_colors, width, height = texture_utils.encode_image_from_path(
      png_path, image_format, PaletteFormat.IA8,
      mipmap_count=1
    )

    assert(width == 32 and height == 32)
    assert(len(image_data.getbuffer()) == 2048)

    with open(bin_path, "wb") as f:
        image_data.seek(0)
        f.write(image_data.read())


def write_bnr1(bnr_path: str, image_data: bytes, short_title: bytes, short_company: bytes, long_title: bytes, long_company: bytes, description: bytes):
    assert(len(image_data.getbuffer()) == 0x1800)
    assert(len(short_title) <= 0x20 and len(short_company) <= 0x20)
    assert(len(long_title) <= 0x40 and len(long_company) <= 0x40)
    assert(len(description) <= 0x80)

    image_data.seek(0)

    with open(bnr_path, "wb") as f:
        f.write(b"BNR1".ljust(0x20, b'\00'))
        f.write(image_data.read())
        f.write(short_title.ljust(0x20, b'\00'))
        f.write(short_company.ljust(0x20, b'\00'))
        f.write(long_title.ljust(0x40, b'\00'))
        f.write(long_company.ljust(0x40, b'\00'))
        f.write(description.ljust(0x80, b'\00'))


def generate_banner():
    png_path = os.path.join(png_dir, "default_opening.png")
    bnr_path = os.path.join(data_dir, "default_opening.bin")

    image_data, palette_data, encoded_colors, width, height = texture_utils.encode_image_from_path(
      png_path, ImageFormat.RGB5A3, PaletteFormat.IA8,
      mipmap_count=1
    )

    assert(width == 96 and height == 32)

    write_bnr1(bnr_path,
        image_data=image_data,
        short_title=b"Cubeboot Loader",
        short_company=b"Team OffBroadway",
        long_title=b"Cubeboot Loader",
        long_company=b"Team OffBroadway",
        description=b"Fluffy Systems LLC"
    )


if __name__ == "__main__":
    convert_ui_texture_to_bti("disc", ImageFormat.I4)
    convert_ui_texture_to_bti("flippydrive", ImageFormat.I4)
    convert_ui_texture_to_bti("l_button", ImageFormat.I4)
    convert_ui_texture_to_bti("r_button", ImageFormat.I4)

    convert_icon_texture_to_bin("dir_tex", ImageFormat.RGB5A3)
    convert_icon_texture_to_bin("dol_tex", ImageFormat.RGB5A3)

    generate_banner()
