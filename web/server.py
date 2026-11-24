# Static server with the headers SharedArrayBuffer (pthreads) needs.
# Usage (from repo root):  python3 web/serve.py build-web   -> http://localhost:8000
import functools, http.server, sys

class H(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".wasm": "application/wasm", ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")  # avoid stale .wasm while iterating
        super().end_headers()

root = sys.argv[1] if len(sys.argv) > 1 else "."
print(f"Serving {root} on http://localhost:8000 (COOP/COEP on)")
http.server.ThreadingHTTPServer(("", 8000), functools.partial(H, directory=root)).serve_forever()