#!/usr/bin/env python3
"""Serve the v0.4 T1/T2/T3 fixtures to a New 3DS on the same LAN."""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import os, socket
ROOT = Path(__file__).resolve().parent
# Deterministic T3: >300 KiB HTML and >1,500 DOM elements.
items = "".join(f'<section><h2>Bloc {i}</h2><p>Texte de mesure numéro {i} pour le test DOM.</p></section>' for i in range(850))
padding = "x" * 240_000
(ROOT / "T3.html").write_text('<!doctype html><html><head><title>T3</title></head><body><h1>T3 - charge DOM</h1>' + items + '<p>' + padding + '</p></body></html>', encoding="utf-8")
os.chdir(ROOT)
class Handler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args): print(fmt % args)
with socket.socket() as s:
    s.connect(("8.8.8.8", 80)); ip = s.getsockname()[0]
print(f"T1: http://{ip}:8000/T1.html\nT2: http://{ip}:8000/T2.html\nT3: http://{ip}:8000/T3.html\nArrêt: Ctrl+C", flush=True)
ThreadingHTTPServer(("0.0.0.0", 8000), Handler).serve_forever()
