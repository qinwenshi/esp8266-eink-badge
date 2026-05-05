import struct
from PIL import Image

EPD_MAGIC = b'EPD2'
EPD_WIDTH = 240
EPD_HEIGHT = 416
PLANE_SIZE = EPD_WIDTH * EPD_HEIGHT // 8  # 12480


def encode(bw_image: Image.Image, red_image: Image.Image) -> bytes:
    """
    bw_image:  '1' mode PIL image, 240×416. Pixel 0=black, 255=white.
    red_image: '1' mode PIL image, 240×416. Pixel 0=red,   255=no-red.
    Returns raw .epd bytes (24968 bytes).
    """
    assert bw_image.size == (EPD_WIDTH, EPD_HEIGHT)
    assert red_image.size == (EPD_WIDTH, EPD_HEIGHT)

    header = EPD_MAGIC + struct.pack('<HH', EPD_WIDTH, EPD_HEIGHT)
    bw_bytes = bw_image.tobytes()    # PIL '1' mode: 1=white packed MSB
    red_bytes = red_image.tobytes()
    return header + bw_bytes + red_bytes


def decode(data: bytes) -> tuple[Image.Image, Image.Image]:
    """Returns (bw_image, red_image) as '1' mode PIL images."""
    assert data[:4] == EPD_MAGIC, "Invalid magic"
    w, h = struct.unpack_from('<HH', data, 4)
    bw_bytes = data[8: 8 + PLANE_SIZE]
    red_bytes = data[8 + PLANE_SIZE: 8 + 2 * PLANE_SIZE]
    bw = Image.frombytes('1', (w, h), bw_bytes)
    red = Image.frombytes('1', (w, h), red_bytes)
    return bw, red
