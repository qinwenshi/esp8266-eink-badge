"""SQLite helpers for per-device configuration."""

import json
import sqlite3
from pathlib import Path


DDL = """
CREATE TABLE IF NOT EXISTS devices (
    id          TEXT PRIMARY KEY,
    label       TEXT,
    template    TEXT,
    data        TEXT NOT NULL DEFAULT '{}',
    version     INTEGER NOT NULL DEFAULT 0,
    last_seen   TEXT
);
"""


def init_db(db_path: str) -> None:
    with sqlite3.connect(db_path) as conn:
        conn.executescript(DDL)
        conn.commit()


def get_device(db_path: str, device_id: str) -> dict | None:
    with sqlite3.connect(db_path) as conn:
        conn.row_factory = sqlite3.Row
        row = conn.execute("SELECT * FROM devices WHERE id = ?", (device_id,)).fetchone()
        return dict(row) if row else None


def upsert_seen(db_path: str, device_id: str) -> None:
    """Register device on first contact; update last_seen on subsequent calls."""
    with sqlite3.connect(db_path) as conn:
        conn.execute(
            """
            INSERT INTO devices (id, last_seen)
            VALUES (?, datetime('now'))
            ON CONFLICT(id) DO UPDATE SET last_seen = datetime('now')
            """,
            (device_id,),
        )
        conn.commit()


def assign_device(
    db_path: str,
    device_id: str,
    template: str,
    data: dict,
    label: str | None = None,
) -> None:
    """Assign or update a device's template + data; bumps version."""
    with sqlite3.connect(db_path) as conn:
        conn.execute(
            """
            INSERT INTO devices (id, label, template, data, version)
            VALUES (?, ?, ?, ?, 1)
            ON CONFLICT(id) DO UPDATE SET
                label    = COALESCE(excluded.label, label),
                template = excluded.template,
                data     = excluded.data,
                version  = version + 1
            """,
            (device_id, label, template, json.dumps(data, ensure_ascii=False)),
        )
        conn.commit()


def set_label(db_path: str, device_id: str, label: str) -> bool:
    with sqlite3.connect(db_path) as conn:
        cur = conn.execute(
            "UPDATE devices SET label = ? WHERE id = ?", (label, device_id)
        )
        conn.commit()
        return cur.rowcount > 0


def list_devices(db_path: str) -> list[dict]:
    with sqlite3.connect(db_path) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            "SELECT * FROM devices ORDER BY last_seen DESC NULLS LAST"
        ).fetchall()
        return [dict(r) for r in rows]


def delete_device(db_path: str, device_id: str) -> bool:
    with sqlite3.connect(db_path) as conn:
        cur = conn.execute("DELETE FROM devices WHERE id = ?", (device_id,))
        conn.commit()
        return cur.rowcount > 0
