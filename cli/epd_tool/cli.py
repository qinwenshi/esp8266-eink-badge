import click
import json
import yaml
from pathlib import Path

from .render import render_template
from .serve import serve as _serve
from .push import push as _push, push_template
from .format import decode


@click.group()
def main():
    """epd-tool — WFT0371CZ78 e-ink display manager"""
    pass


@main.command()
@click.argument('template')
@click.option('--data', '-d', default='{}', help='JSON data string or @file.json/.yaml')
@click.option('--out', '-o', default=None, help='Output .epd file (default: stdout preview)')
@click.option('--preview', '-p', is_flag=True, help='Open preview image')
def render(template, data, out, preview):
    """Render a YAML/JSON template to .epd format."""
    data_dict = _load_data(data)
    epd = render_template(template, data_dict)
    if out:
        Path(out).write_bytes(epd)
        click.echo(f"Written {len(epd)} bytes → {out}")
    if preview:
        from PIL import Image
        bw, red = decode(epd)
        img = Image.new('RGB', bw.size, 'white')
        for y in range(bw.height):
            for x in range(bw.width):
                if not red.getpixel((x, y)):
                    img.putpixel((x, y), (255, 0, 0))
                elif not bw.getpixel((x, y)):
                    img.putpixel((x, y), (0, 0, 0))
        img.show()
    if not out and not preview:
        click.echo(f"Rendered {len(epd)} bytes (use --out or --preview to save/view)")


@main.command()
@click.argument('template')
@click.option('--data', '-d', default='{}', help='JSON data string or @file.json/.yaml')
@click.option('--host', default='0.0.0.0', show_default=True)
@click.option('--port', default=8080, show_default=True)
@click.option('--watch', '-w', is_flag=True, help='Watch template file and auto re-render')
def serve(template, data, host, port, watch):
    """Serve current .epd image over HTTP. Device polls /version + /current.epd."""
    data_dict = _load_data(data)
    _serve(host, port, template, data_dict, watch)


@main.command()
@click.argument('template_or_epd')
@click.option('--data', '-d', default='{}', help='JSON data string or @file.json/.yaml')
@click.option('--host', '-H', required=True, help='Device IP or hostname')
@click.option('--port', default=80, show_default=True)
def push(template_or_epd, data, host, port):
    """Push image directly to device (Push mode, device runs HTTP server)."""
    path = Path(template_or_epd)
    if path.suffix == '.epd':
        epd = path.read_bytes()
        result = _push(host, port, epd)
    else:
        data_dict = _load_data(data)
        result = push_template(host, port, template_or_epd, data_dict)
    click.echo(f"Device responded: {result}")


def _load_data(data_str: str) -> dict:
    if data_str.startswith('@'):
        path = Path(data_str[1:])
        text = path.read_text()
        return yaml.safe_load(text) if path.suffix in ('.yaml', '.yml') else json.loads(text)
    return json.loads(data_str)
