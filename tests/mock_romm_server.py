#!/usr/bin/env python3
import argparse
import base64
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

TOKEN = "test-token"
BASIC_USER = "root"
BASIC_PASS = "pw"

PLATFORMS = [
    {"id": 1, "slug": "switch", "name": "Nintendo Switch", "display_name": "Nintendo Switch"},
    {"id": 2, "slug": "wiiu", "name": "Wii U", "display_name": "Wii U"},
    {"id": 3, "slug": "wii", "name": "Wii", "display_name": "Wii"},
]

ROMS = {
    "switch": [
        {"id": "sw_001", "title": "Mock Mario", "subtitle": "Nintendo", "size_mb": 64},
        {"id": "sw_002", "title": "Mock Zelda", "subtitle": "Nintendo", "size_mb": 128},
        {"id": "sw_003", "title": "Mock Metroid", "subtitle": "Retro", "size_mb": 256},
    ],
    "wiiu": [
        {"id": "wu_001", "title": "Mock Kart 8", "subtitle": "Nintendo", "size_mb": 64},
        {"id": "wu_002", "title": "Mock Xenoblade", "subtitle": "Monolith", "size_mb": 512},
    ],
    "wii": [
        {"id": "wi_001", "title": "Mock Galaxy 2", "subtitle": "Nintendo", "size_mb": 64},
        {"id": "wi_002", "title": "Mock Prime Trilogy", "subtitle": "Retro", "size_mb": 512},
    ],
}

FILES = {
    "sw_001": b"MARIO-DATA-" * 200,
    "sw_002": b"ZELDA-DATA-" * 180,
    "sw_003": b"METROID-DATA-" * 150,
    "wu_001": b"KART-DATA-" * 210,
    "wu_002": b"XENO-DATA-" * 170,
    "wi_001": b"GALAXY-DATA-" * 160,
    "wi_002": b"PRIME-DATA-" * 190,
}

FAIL_COUNT = {"wi_002": 0}

PLATFORM_ID_BY_SLUG = {p["slug"]: str(p["id"]) for p in PLATFORMS}
PLATFORM_SLUG_BY_ID = {str(p["id"]): p["slug"] for p in PLATFORMS}


def paginate(items, page, limit):
    start = max(0, (page - 1) * limit)
    end = start + limit
    chunk = items[start:end]
    next_page = page + 1 if end < len(items) else 0
    return chunk, next_page


class Handler(BaseHTTPRequestHandler):
    server_version = "MockRomm/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        return

    def _write_json(self, code, payload):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _require_auth(self):
        auth = self.headers.get("Authorization", "")
        if auth == f"Bearer {TOKEN}":
            return True
        expected = base64.b64encode(f"{BASIC_USER}:{BASIC_PASS}".encode("utf-8")).decode("ascii")
        return auth == f"Basic {expected}"

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        if path == "/health":
            self._write_json(200, {"ok": True})
            return

        if path.startswith("/api/") and not self._require_auth():
            self._write_json(401, {"error": "unauthorized"})
            return

        if path == "/api/v1/preflight":
            self._write_json(200, {"ok": True, "user": "dev"})
            return

        if path == "/api/platforms":
            self._write_json(200, PLATFORMS)
            return

        if path == "/api/v1/platforms":
            q = parse_qs(parsed.query)
            page = int(q.get("page", ["1"])[0])
            limit = int(q.get("limit", ["32"])[0])
            items, next_page = paginate(PLATFORMS, page, limit)
            self._write_json(200, {"items": items, "next_page": next_page})
            return

        if path.startswith("/api/v1/platforms/") and path.endswith("/roms"):
            parts = path.split("/")
            if len(parts) < 6:
                self._write_json(404, {"error": "not found"})
                return
            platform_id = parts[4]
            roms = ROMS.get(platform_id)
            if roms is None:
                self._write_json(404, {"error": "platform not found"})
                return
            q = parse_qs(parsed.query)
            page = int(q.get("page", ["1"])[0])
            limit = int(q.get("limit", ["32"])[0])
            items, next_page = paginate(roms, page, limit)
            host = self.headers.get("Host", "127.0.0.1")
            out = []
            for it in items:
                row = dict(it)
                row["download_url"] = f"http://{host}/files/{it['id']}.bin"
                out.append(row)
            self._write_json(200, {"items": out, "next_page": next_page})
            return

        if path == "/api/roms":
            q = parse_qs(parsed.query)
            platform_ref = q.get("platform_ids", [""])[0]
            platform_slug = PLATFORM_SLUG_BY_ID.get(platform_ref, platform_ref)
            roms = ROMS.get(platform_slug)
            if roms is None:
                self._write_json(404, {"error": "platform not found"})
                return
            limit = int(q.get("limit", ["32"])[0])
            offset = int(q.get("offset", ["0"])[0])
            page = (offset // max(1, limit)) + 1
            items, next_page = paginate(roms, page, limit)
            host = self.headers.get("Host", "127.0.0.1")
            out = []
            for it in items:
                row = dict(it)
                row["id"] = row["id"]
                row["title"] = row["title"]
                row["download_url"] = f"http://{host}/files/{it['id']}.bin"
                out.append(row)
            self._write_json(200, {"items": out, "next_page": next_page})
            return

        if path.startswith("/files/") and path.endswith(".bin"):
            file_id = path.split("/")[-1].replace(".bin", "")
            data = FILES.get(file_id)
            if data is None:
                self._write_json(404, {"error": "file not found"})
                return
            if file_id == "wi_002" and FAIL_COUNT["wi_002"] == 0:
                FAIL_COUNT["wi_002"] += 1
                self._write_json(503, {"error": "temporary unavailable"})
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        self._write_json(404, {"error": "not found"})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()
    server = HTTPServer(("127.0.0.1", args.port), Handler)
    server.serve_forever()


if __name__ == "__main__":
    main()
