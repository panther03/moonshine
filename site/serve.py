#!/usr/bin/env python3
"""Serve the configurator locally and open it in a browser.

The page is built from ES modules, which browsers refuse to load over file://,
so it needs a real HTTP origin even for local use. This is that, and nothing
more -- deploying is still a plain copy of this directory.

    python serve.py [port]
"""

import functools
import http.server
import os
import sys
import webbrowser

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Editing a module and hard-reloading should show the edit, not a cached
        # copy from thirty seconds ago.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass  # the request log is noise here


def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    url = f"http://localhost:{PORT}/"
    with http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler) as httpd:
        print(f"Susamune GUI Configurator: {url}\nCtrl-C to stop.")
        webbrowser.open(url)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()


if __name__ == "__main__":
    main()
