#!/usr/bin/env python3
"""Lightweight localhost bridge for emulator builds.

Bridges emulator requests from 127.0.0.1:8080 to a configured upstream ROMM
server, and can inject auth when the client doesn't send it.
"""

from __future__ import annotations

import argparse
import base64
import http.server
import socketserver
import ssl
import sys
import urllib.error
import urllib.request

HOP_BY_HOP_HEADERS = {
    "connection",
    "proxy-connection",
    "keep-alive",
    "te",
    "trailers",
    "transfer-encoding",
    "upgrade",
}


class BridgeHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stdout.write(f"[bridge] {self.address_string()} - {fmt % args}\n")
        sys.stdout.flush()

    def do_GET(self) -> None:  # noqa: N802
        self._forward()

    def do_POST(self) -> None:  # noqa: N802
        self._forward()

    def do_PUT(self) -> None:  # noqa: N802
        self._forward()

    def do_PATCH(self) -> None:  # noqa: N802
        self._forward()

    def do_DELETE(self) -> None:  # noqa: N802
        self._forward()

    def do_HEAD(self) -> None:  # noqa: N802
        self._forward()

    def do_OPTIONS(self) -> None:  # noqa: N802
        self._forward()

    def _forward(self) -> None:
        target_url = self.server.target_base + self.path  # type: ignore[attr-defined]
        content_length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(content_length) if content_length > 0 else None

        req = urllib.request.Request(target_url, data=body, method=self.command)
        for key, value in self.headers.items():
            lk = key.lower()
            if lk in HOP_BY_HOP_HEADERS or lk == "host":
                continue
            if lk == "accept-encoding":
                # Avoid gzip/deflate handling complexity in the bridge.
                continue
            req.add_header(key, value)

        auth_header = self.server.auth_header  # type: ignore[attr-defined]
        if auth_header and "authorization" not in (k.lower() for k in self.headers.keys()):
            req.add_header("Authorization", auth_header)

        try:
            with self.server.opener.open(req, timeout=self.server.timeout_seconds) as resp:  # type: ignore[attr-defined]
                payload = resp.read()
                self.send_response(resp.getcode())
                for key, value in resp.getheaders():
                    lk = key.lower()
                    if lk in HOP_BY_HOP_HEADERS or lk == "content-length":
                        continue
                    self.send_header(key, value)
                self.send_header("Content-Length", str(len(payload)))
                self.send_header("Connection", "close")
                self.end_headers()
                if self.command != "HEAD" and payload:
                    self.wfile.write(payload)
        except urllib.error.HTTPError as err:
            payload = err.read() if err.fp else b""
            self.send_response(err.code)
            for key, value in err.headers.items():
                lk = key.lower()
                if lk in HOP_BY_HOP_HEADERS or lk == "content-length":
                    continue
                self.send_header(key, value)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD" and payload:
                self.wfile.write(payload)
        except Exception as exc:  # noqa: BLE001
            message = f"bridge error: {exc}".encode("utf-8", "replace")
            self.send_response(502)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(message)))
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(message)


class ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def build_auth_header(args: argparse.Namespace) -> str | None:
    if args.bearer_token:
        return f"Bearer {args.bearer_token}"
    if args.basic_user is not None or args.basic_pass is not None:
        user = args.basic_user or ""
        pwd = args.basic_pass or ""
        encoded = base64.b64encode(f"{user}:{pwd}".encode("utf-8")).decode("ascii")
        return f"Basic {encoded}"
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8080)
    parser.add_argument("--target", required=True)
    parser.add_argument("--bearer-token")
    parser.add_argument("--basic-user")
    parser.add_argument("--basic-pass")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--insecure", action="store_true")
    args = parser.parse_args()

    target_base = args.target.rstrip("/")
    if not (target_base.startswith("http://") or target_base.startswith("https://")):
        parser.error("--target must begin with http:// or https://")

    opener = urllib.request.build_opener()
    if args.insecure and target_base.startswith("https://"):
        context = ssl._create_unverified_context()  # noqa: SLF001
        opener = urllib.request.build_opener(urllib.request.HTTPSHandler(context=context))

    server = ThreadingHTTPServer((args.listen_host, args.listen_port), BridgeHandler)
    server.target_base = target_base  # type: ignore[attr-defined]
    server.auth_header = build_auth_header(args)  # type: ignore[attr-defined]
    server.timeout_seconds = max(1.0, float(args.timeout))  # type: ignore[attr-defined]
    server.opener = opener  # type: ignore[attr-defined]

    auth_mode = "none"
    if server.auth_header:  # type: ignore[attr-defined]
        auth_mode = "injected"
    print(
        f"[bridge] listening on http://{args.listen_host}:{args.listen_port} -> "
        f"{target_base} (auth={auth_mode})",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
