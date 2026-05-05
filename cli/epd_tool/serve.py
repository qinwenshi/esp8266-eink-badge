import time
import threading
import json
from http.server import HTTPServer, BaseHTTPRequestHandler

from .render import render_template

_current_epd: bytes = b''
_current_version: int = 0
_lock = threading.Lock()

# DB mode state
_db_path: str | None = None
_device_cache: dict[str, tuple[int, bytes]] = {}  # id -> (version, epd_bytes)
_cache_lock = threading.Lock()


def update_content(epd_bytes: bytes) -> None:
    global _current_epd, _current_version
    with _lock:
        _current_epd = epd_bytes
        _current_version += 1


def _render_for_device(device: dict) -> bytes:
    """Render .epd bytes for a device record. Raises on error."""
    data = json.loads(device.get('data') or '{}')
    return render_template(device['template'], data)


def _get_device_epd(device_id: str) -> tuple[int, bytes | None]:
    """Return (version, epd_bytes) for a device, using in-memory cache."""
    import click, os
    from . import db as _db
    device = _db.get_device(_db_path, device_id)
    click.echo(f"[epd-tool] _get_device_epd({device_id}): db={os.path.abspath(_db_path)}, device={device}")
    if not device or not device.get('template'):
        return 0, None
    version = device['version']
    with _cache_lock:
        cached = _device_cache.get(device_id)
    if cached and cached[0] == version:
        click.echo(f"[epd-tool]   → cache hit v{version}, {len(cached[1])} bytes")
        return version, cached[1]
    # Re-render
    click.echo(f"[epd-tool]   → rendering template={device['template']}")
    epd = _render_for_device(device)
    click.echo(f"[epd-tool]   → rendered {len(epd)} bytes")
    with _cache_lock:
        _device_cache[device_id] = (version, epd)
    return version, epd


class EpdHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        import click
        click.echo(f"[{self.address_string()}] {format % args}")

    def _read_json_body(self) -> dict:
        length = int(self.headers.get('Content-Length', 0))
        if length == 0:
            return {}
        try:
            raw = self.rfile.read(length)
            body = json.loads(raw)
            import click
            click.echo(f"[{self.address_string()}] body: {raw.decode(errors='replace')}")
            return body
        except Exception:
            return {}
        if length == 0:
            return {}
        try:
            return json.loads(self.rfile.read(length))
        except Exception:
            return {}

    def do_GET(self):
        if self.path == '/version':
            with _lock:
                v = str(_current_version).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(v)

        elif self.path == '/current.epd':
            with _lock:
                data = _current_epd
            if not data:
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        elif self.path == '/status':
            with _lock:
                v = _current_version
            resp = json.dumps({'version': v}).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(resp)

        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == '/version':
            body = self._read_json_body()
            device_id = body.get('id', '')

            if _db_path and device_id:
                import click
                from . import db as _db
                is_new = _db.get_device(_db_path, device_id) is None
                _db.upsert_seen(_db_path, device_id)
                if is_new:
                    click.echo(f"[epd-tool] New device: {device_id}  →  epd device assign {device_id} <template> --data '{{...}}'")
                version, _ = _get_device_epd(device_id)
                resp = str(version).encode()
            else:
                # Legacy / simple mode: global version
                with _lock:
                    resp = str(_current_version).encode()

            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(resp)

        elif self.path == '/current.epd':
            body = self._read_json_body()
            device_id = body.get('id', '')

            if _db_path and device_id:
                try:
                    version, epd = _get_device_epd(device_id)
                except Exception as e:
                    import click
                    click.echo(f"[epd-tool] Render error for {device_id}: {e}", err=True)
                    self.send_response(500)
                    self.end_headers()
                    return
                if not epd:
                    # Device not configured — return 204 No Content so firmware stays silent
                    self.send_response(204)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(len(epd)))
                self.end_headers()
                self.wfile.write(epd)
            else:
                # Legacy mode
                with _lock:
                    data = _current_epd
                if not data:
                    self.send_response(204)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        elif self.path == '/push':
            length = int(self.headers.get('Content-Length', 0))
            data = self.rfile.read(length)
            update_content(data)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'OK')

        else:
            self.send_response(404)
            self.end_headers()


def serve(
    host: str,
    port: int,
    template: str | None = None,
    data: dict | None = None,
    watch: bool = False,
    db_path: str | None = None,
) -> None:
    """Start HTTP server. Modes:
      - DB mode (db_path set): per-device content from SQLite
      - Simple mode (template set): single template for all devices
    """
    global _db_path
    import click

    if db_path:
        from . import db as _db
        _db.init_db(db_path)
        _db_path = db_path
        click.echo(f"[epd-tool] DB mode: {db_path}")
    elif template:
        def render_and_update():
            try:
                epd = render_template(template, data or {})
                update_content(epd)
                click.echo(f"[epd-tool] Rendered {len(epd)} bytes → version {_current_version}")
            except Exception as e:
                click.echo(f"[epd-tool] Render error: {e}", err=True)

        render_and_update()
    else:
        raise ValueError("Either template or db_path must be provided")

    httpd = HTTPServer((host, port), EpdHandler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    click.echo(f"[epd-tool] Serving on http://{host}:{port}")

    if watch and template:
        try:
            import watchfiles
            click.echo(f"[epd-tool] Watching {template} for changes…")
            for _ in watchfiles.watch(template):
                click.echo("[epd-tool] Template changed, re-rendering…")
                render_and_update()
        except ImportError:
            click.echo("[epd-tool] watchfiles not installed; running without watch.", err=True)
            _wait_forever()
    else:
        _wait_forever()

    httpd.shutdown()


def _wait_forever():
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass

