#!/usr/bin/env python3

import http.server
import os.path
import socketserver
from pathlib import Path

USE_SSL = False  # create key.pem and cert.pem and set this to True to host via https://
PORT = 4443 if USE_SSL else 8000

script_dir = Path(os.path.dirname(os.path.realpath(__file__)))

Handler = http.server.SimpleHTTPRequestHandler
Handler.extensions_map.update({
    '.wasm': 'application/wasm',
})

socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    httpd.allow_reuse_address = True
    if USE_SSL:
        import ssl
        httpd.socket = ssl.wrap_socket(httpd.socket,
                                       keyfile=script_dir / "key.pem",
                                       certfile=script_dir / "cert.pem",
                                       server_side=True)
    print("serving at port", PORT)
    httpd.serve_forever()
