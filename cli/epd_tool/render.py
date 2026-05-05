from PIL import Image, ImageDraw, ImageFont
import yaml
import json
from jinja2 import Template
from pathlib import Path
from .format import encode, EPD_WIDTH, EPD_HEIGHT


def _load_font(font_name: str, size: int) -> ImageFont.ImageFont:
    """Try system fonts with CJK fallbacks, last resort is Pillow default."""
    candidates = [
        font_name,
        f"/System/Library/Fonts/Supplemental/{font_name}.ttf",
        f"/usr/share/fonts/truetype/{font_name}.ttf",
        # macOS CJK fonts
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        # Linux CJK fonts
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(path, size)
        except Exception:
            pass
    return ImageFont.load_default()


def render_template(template_path: str, data: dict) -> bytes:
    """
    Render a YAML/JSON template with data variables.
    Returns .epd bytes.

    Template format:
    ```yaml
    size: [240, 416]
    background: white
    elements:
      - type: text
        text: "{{ name }}"
        font: Helvetica
        size: 28
        color: black   # or red
        pos: [x, y]
        align: left    # left | center | right (optional)
      - type: rect
        rect: [x, y, w, h]
        fill: black    # or red or white
        outline: black # optional
      - type: image
        src: "path/to/image.png"
        rect: [x, y, w, h]
      - type: line
        start: [x1, y1]
        end: [x2, y2]
        color: black
        width: 2
    ```
    """
    path = Path(template_path)
    raw = path.read_text()
    # Jinja2 render with data
    raw = Template(raw).render(**data)

    if path.suffix in ('.yaml', '.yml'):
        tmpl = yaml.safe_load(raw)
    else:
        tmpl = json.loads(raw)

    w, h = tmpl.get('size', [EPD_WIDTH, EPD_HEIGHT])

    # Two canvases: black plane (B+W), red plane
    bw_img = Image.new('RGB', (w, h), 'white')
    red_img = Image.new('RGB', (w, h), 'white')
    bw_draw = ImageDraw.Draw(bw_img)
    red_draw = ImageDraw.Draw(red_img)

    for elem in tmpl.get('elements', []):
        etype = elem['type']
        color = elem.get('color', 'black')
        draw = red_draw if color == 'red' else bw_draw
        fill_color = elem.get('fill', 'black')
        fill_draw = red_draw if fill_color == 'red' else bw_draw

        if etype == 'text':
            font = _load_font(elem.get('font', 'Helvetica'), elem.get('size', 16))
            x, y = elem['pos']
            align = elem.get('align', 'left')
            actual_color = 'red' if draw is red_draw else 'black'
            anchor = 'ma' if align == 'center' else 'la'
            draw.text((x, y), str(elem['text']), font=font, fill=actual_color, anchor=anchor)

        elif etype == 'rect':
            x, y, rw, rh = elem['rect']
            fill = elem.get('fill')
            outline = elem.get('outline')
            fc = 'red' if fill == 'red' else ('black' if fill == 'black' else None)
            oc = 'red' if outline == 'red' else ('black' if outline == 'black' else None)
            fill_draw.rectangle([x, y, x + rw, y + rh], fill=fc, outline=oc)

        elif etype == 'line':
            x1, y1 = elem['start']
            x2, y2 = elem['end']
            lc = 'red' if color == 'red' else 'black'
            draw.line([(x1, y1), (x2, y2)], fill=lc, width=elem.get('width', 1))

        elif etype == 'image':
            src = Path(path.parent) / elem['src']
            img = Image.open(src).convert('RGBA')
            x, y, iw, ih = elem['rect']
            img = img.resize((iw, ih))
            bw_img.paste(img, (x, y), img.split()[3] if img.mode == 'RGBA' else None)

    # BW plane: dither to 1-bit (white=1, black=0)
    bw_1bit = bw_img.convert('1')

    # Red plane: extract red pixels from red canvas
    # Pixels drawn as 'red' RGB(255,0,0) → 0 (has red); others → 1 (no red)
    r, g, _ = red_img.split()[:3]
    red_mask = r.point(lambda x: 0 if x > 200 else 255)
    green_mask = g.point(lambda x: 255 if x < 100 else 0)
    # Combine: pixel is red only when both masks agree
    combined = Image.new('L', (w, h))
    for py in range(h):
        for px in range(w):
            combined.putpixel((px, py),
                              0 if (red_mask.getpixel((px, py)) == 0 and
                                    green_mask.getpixel((px, py)) == 255) else 255)
    red_1bit = combined.point(lambda x: 0 if x == 0 else 255, '1')

    return encode(bw_1bit, red_1bit)
