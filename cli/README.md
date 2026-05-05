# epd-tool

Python CLI for managing a WFT0371CZ78 3.7" BWR e-ink display (240×416) over WiFi.

## Installation

```bash
cd cli
pip install -e .
```

## Usage

### Preview a template locally

```bash
epd render templates/badge.yaml \
    -d '{"name":"张三","title":"工程师","department":"研发部","company":"ACME"}' \
    --preview
```

### Pull mode — serve images, device polls

Start the server on your machine (device must have `SYNC_MODE 0` and `SYNC_HOST` pointing here):

```bash
epd serve templates/badge.yaml \
    -d '{"name":"张三","title":"工程师","department":"研发部","company":"ACME"}' \
    --port 8080
```

Add `--watch` to auto-re-render whenever the template file changes:

```bash
epd serve templates/meeting-room.yaml \
    -d '{"room_name":"会议室A","status":"Available","next_meeting":"Sales","next_time":"14:00"}' \
    --port 8080 --watch
```

The device polls `GET /version` every `PULL_INTERVAL_MS` ms. If the version number
increased it fetches `GET /current.epd` and renders the new image.

### Push mode — push directly to device

Flash the device with `SYNC_MODE 1`. Then push from the CLI:

```bash
# Push a rendered template
epd push templates/namecard.yaml \
    -d '{"name":"李四","title":"设计师","email":"li@acme.com","phone":"138-0000-0000","company":"ACME"}' \
    -H epd-display.local

# Push a pre-built .epd file
epd push output.epd -H 192.168.1.42 --port 80
```

The device exposes `POST /push` (accepts raw `.epd` bytes) and `GET /status`.

### Save a .epd file

```bash
epd render templates/namecard.yaml \
    -d '{"name":"王五","title":"PM","email":"w@acme.com","phone":"","company":"ACME"}' \
    --out output.epd
```

## .epd binary format

| Offset | Size  | Field                                        |
|--------|-------|----------------------------------------------|
| 0      | 4     | Magic: `EPD2`                                |
| 4      | 2     | Width  (uint16 LE) = 240                     |
| 6      | 2     | Height (uint16 LE) = 416                     |
| 8      | 12480 | BW plane  (1=white, 0=black) → cmd `0x10`   |
| 12488  | 12480 | Red plane (1=no-red, 0=red)  → cmd `0x13`   |
| **Total** | **24968** |                                       |

## Template format

Templates are YAML (or JSON) files with Jinja2 variable interpolation.

```yaml
size: [240, 416]
background: white
elements:
  - type: text
    text: "{{ name }}"
    font: Helvetica
    size: 28
    color: black        # black | red | white
    pos: [x, y]
    align: left         # left | center

  - type: rect
    rect: [x, y, w, h]
    fill: black         # black | red | white
    outline: black      # optional

  - type: line
    start: [x1, y1]
    end:   [x2, y2]
    color: red
    width: 2

  - type: image
    src: "relative/path.png"
    rect: [x, y, w, h]
```
