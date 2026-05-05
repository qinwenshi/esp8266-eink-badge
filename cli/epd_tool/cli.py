import click
import json
import yaml
from pathlib import Path

from .render import render_template
from .serve import serve as _serve
from .push import push as _push, push_template
from .format import decode

DEFAULT_DB = 'epd-devices.db'


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
@click.argument('template', required=False, default=None)
@click.option('--data', '-d', default='{}', help='JSON data (simple mode only)')
@click.option('--db', default=None, help=f'SQLite DB path for per-device mode (default: {DEFAULT_DB})')
@click.option('--host', default='0.0.0.0', show_default=True)
@click.option('--port', default=8080, show_default=True)
@click.option('--watch', '-w', is_flag=True, help='Watch template file (simple mode only)')
def serve(template, data, db, host, port, watch):
    """Serve .epd content over HTTP.

    \b
    Simple mode:  epd serve templates/badge.yaml --data '{"name":"Leon"}'
    DB mode:      epd serve --db epd-devices.db
    """
    if template and db:
        raise click.UsageError("Specify either TEMPLATE (simple mode) or --db (DB mode), not both.")
    if not template and not db:
        db = DEFAULT_DB  # default to DB mode with standard path
    if db:
        import os
        db = os.path.abspath(db)
    data_dict = _load_data(data) if template else {}
    _serve(host, port, template=template, data=data_dict, watch=watch, db_path=db)


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


# ── Device management ─────────────────────────────────────────────────────────

@main.group()
def device():
    """Manage per-device template assignments (DB mode)."""
    pass


@device.command('list')
@click.option('--db', default=DEFAULT_DB, show_default=True, help='SQLite DB path')
def device_list(db):
    """List all known devices and their assignments."""
    from . import db as _db
    if not Path(db).exists():
        click.echo(f"DB not found: {db}  (no devices registered yet)")
        return
    _db.init_db(db)
    devices = _db.list_devices(db)
    if not devices:
        click.echo("No devices registered yet.")
        return
    click.echo(f"{'ID':<10} {'Label':<20} {'Template':<35} {'Ver':>4}  {'Last seen'}")
    click.echo('-' * 90)
    for d in devices:
        label = d.get('label') or ''
        tpl   = d.get('template') or '(none)'
        ver   = d.get('version', 0)
        seen  = d.get('last_seen') or 'never'
        click.echo(f"{d['id']:<10} {label:<20} {tpl:<35} {ver:>4}  {seen}")


@device.command('assign')
@click.argument('device_id')
@click.argument('template')
@click.option('--data', '-d', default='{}', help='JSON data string or @file.json/.yaml')
@click.option('--label', '-l', default=None, help='Human-readable label for this device')
@click.option('--db', default=DEFAULT_DB, show_default=True, help='SQLite DB path')
def device_assign(device_id, template, data, label, db):
    """Assign a template (and optional data) to a device."""
    from . import db as _db
    _db.init_db(db)
    data_dict = _load_data(data)
    _db.assign_device(db, device_id.upper(), template, data_dict, label)
    click.echo(f"Assigned {template} to device {device_id.upper()} (DB: {db})")


@device.command('label')
@click.argument('device_id')
@click.argument('label')
@click.option('--db', default=DEFAULT_DB, show_default=True)
def device_label(device_id, label, db):
    """Set a human-readable label for a device."""
    from . import db as _db
    _db.init_db(db)
    ok = _db.set_label(db, device_id.upper(), label)
    if ok:
        click.echo(f"Label set: {device_id.upper()} → {label!r}")
    else:
        click.echo(f"Device {device_id.upper()} not found in {db}", err=True)


@device.command('delete')
@click.argument('device_id')
@click.option('--db', default=DEFAULT_DB, show_default=True)
@click.confirmation_option(prompt='Delete this device?')
def device_delete(device_id, db):
    """Remove a device from the DB."""
    from . import db as _db
    _db.init_db(db)
    ok = _db.delete_device(db, device_id.upper())
    if ok:
        click.echo(f"Deleted {device_id.upper()}")
    else:
        click.echo(f"Device {device_id.upper()} not found", err=True)


def _load_data(data_str: str) -> dict:
    if data_str.startswith('@'):
        path = Path(data_str[1:])
        text = path.read_text()
        return yaml.safe_load(text) if path.suffix in ('.yaml', '.yml') else json.loads(text)
    return json.loads(data_str)

