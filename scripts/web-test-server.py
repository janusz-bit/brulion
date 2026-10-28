#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A static web server for browser tests of the web build, with a report endpoint.

    python3 scripts/web-test-server.py ROOT PORT REPORT.jsonl

Serves the files under ROOT (the site, e.g. ROOT/brulion/ -> dist-wasm) on 127.0.0.1:PORT with
no caching, and appends every POST to /__report[/<name>] as one JSON line to REPORT.jsonl:
{"path": "/__report/<name>", "time": <unix seconds>, "body": <the body: JSON when it parses, else
the text>}. Browsers on the Android emulator (scripts/android-emulator-test.sh --web-keyboard)
reach it at http://10.0.2.2:PORT/ (the emulator's alias of the host's loopback) and send the
app's self-test results there (wasm/index.html ?report=).
Prints "listening on PORT" once ready. Standard library only.
"""
import http.server
import json
import os
import sys
import threading
import time


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".webmanifest": "application/manifest+json",
        ".svg": "image/svg+xml",
    }
    report_file = ""
    lock = threading.Lock()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/__report" and not path.startswith("/__report/"):
            self.send_error(404)
            return
        try:
            length = min(int(self.headers.get("Content-Length") or 0), 1 << 20)
        except ValueError:
            length = 0
        raw = self.rfile.read(length).decode("utf-8", "replace")
        try:
            body = json.loads(raw)
        except ValueError:
            body = raw
        line = json.dumps({"path": path, "time": round(time.time(), 3), "body": body}, ensure_ascii=False)
        with Handler.lock, open(Handler.report_file, "a", encoding="utf-8") as out:
            out.write(line + "\n")
        self.send_response(204)
        self.end_headers()

    def log_message(self, format, *args):  # noqa: A002 (the base class's name)
        sys.stderr.write("%s %s\n" % (self.log_date_time_string(), format % args))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    root, port, report = sys.argv[1], int(sys.argv[2]), os.path.abspath(sys.argv[3])
    if not os.path.isdir(root):
        sys.exit("not a directory: " + root)
    Handler.report_file = report
    open(report, "a", encoding="utf-8").close()
    os.chdir(root)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("listening on %d" % port, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
