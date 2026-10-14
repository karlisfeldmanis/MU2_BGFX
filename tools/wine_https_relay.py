#!/usr/bin/env python3
"""HTTPS for a program under Wine on this Mac, which has none: Wine's schannel wants GnuTLS and this
Wine has it not, so WinHTTP fails every HTTPS call with error 12157 (docs/plans/launcher.md).

The program is pointed at http://127.0.0.1:PORT, and this passes each request on to the real site
over HTTPS and its answer back, streamed: the same accounts, the same manifest, the same files.
Only the TLS hop is the Mac's. A real Windows does TLS itself and never needs this.

    python3 -I tools/wine_https_relay.py [--port 8096] [--site https://mu-classic.com]
    wine MU2Launcher.exe --site http://127.0.0.1:8096
"""
import argparse
import http.client
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

PASSED = ("Content-Type", "Authorization", "Range", "Accept", "User-Agent")
RETURNED = ("Content-Type", "Content-Length", "Content-Range", "Accept-Ranges", "Cache-Control", "ETag",
            "Last-Modified")


class Relay(BaseHTTPRequestHandler):
    site = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        print(f"relay {fmt % args}", flush=True)

    def relay(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length > 0 else None
        headers = {k: self.headers[k] for k in PASSED if k in self.headers}
        far = http.client.HTTPSConnection(self.site.hostname, self.site.port or 443, timeout=60,
                                          context=ssl.create_default_context())
        try:
            far.request(self.command, self.path, body=body, headers=headers)
            answer = far.getresponse()
            self.send_response(answer.status, answer.reason)
            for k in RETURNED:
                v = answer.getheader(k)
                if v is not None:
                    self.send_header(k, v)
            if answer.getheader("Content-Length") is None:
                self.send_header("Connection", "close")
                self.close_connection = True
            self.end_headers()
            while True:
                chunk = answer.read(1 << 16)
                if not chunk:
                    break
                self.wfile.write(chunk)
        except (OSError, http.client.HTTPException) as e:
            try:
                self.send_error(502, f"relay: {e}")
            except OSError:
                pass
        finally:
            far.close()

    do_GET = do_POST = do_HEAD = relay


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=8096)
    ap.add_argument("--site", default="https://mu-classic.com")
    args = ap.parse_args()
    Relay.site = urlparse(args.site)
    print(f"relay on http://127.0.0.1:{args.port} -> {args.site}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), Relay).serve_forever()


if __name__ == "__main__":
    main()
