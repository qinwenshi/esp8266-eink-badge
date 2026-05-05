import requests
from .render import render_template


def push(host: str, port: int, epd_bytes: bytes) -> str:
    url = f"http://{host}:{port}/push"
    resp = requests.post(
        url,
        data=epd_bytes,
        headers={'Content-Type': 'application/octet-stream'},
        timeout=30,
    )
    resp.raise_for_status()
    return resp.text


def push_template(host: str, port: int, template: str, data: dict) -> str:
    epd = render_template(template, data)
    return push(host, port, epd)
