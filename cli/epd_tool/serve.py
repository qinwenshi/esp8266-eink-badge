import time
import threading
import json
from http.server import HTTPServer, BaseHTTPRequestHandler

from .render import render_template

_current_epd: bytes = b''
_current_version: int = 0
_lock = threading.Lock()


def update_content(epd_bytes: bytes) -> None:
    global _current_epd, _current_version
    with _lock:
        _current_epd = epd_bytes
        _current_version += 1


class EpdHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass  # suppress default logging

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
        if self.path == '/push':
            length = int(self.headers.get('Content-Length', 0))
            data = self.rfile.read(length)
            update_content(data)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'OK')
        else:
            self.send_response(404)
            self.end_headers()


def serve(host: str, port: int, template: str, data: dict, watch: bool = False) -> None:
    """Start HTTP server, optionally watch template file for changes."""
    import click

    def render_and_update():
        try:
            epd = render_template(template, data)
            update_content(epd)
            click.echo(f"[epd-tool] Rendered {len(epd)} bytes → version {_current_version}")
        except Exception as e:
            click.echo(f"[epd-tool] Render error: {e}", err=True)

    render_and_update()

    httpd = HTTPServer((host, port), EpdHandler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    click.echo(f"[epd-tool] Serving on http://{host}:{port}  (version {_current_version})")

    if watch:
        try:
            import watchfiles
            click.echo(f"[epd-tool] Watching {template} for changes…")
            for _ in watchfiles.watch(template):
                click.echo("[epd-tool] Template changed, re-rendering…")
                render_and_update()
        except ImportError:
            click.echo("[epd-tool] watchfiles not installed; running without watch.", err=True)
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                pass
    else:
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass

    httpd.shutdown()
