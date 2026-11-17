# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Garrett Dothard
"""Serve the portable CPS on loopback using only the bundled standard library."""
import argparse
import functools
import http.server
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

APP = "radiofoundry-offline-cps"
HEALTH = "/__offline_cps_health"


def release_products():
	packaged = Path(__file__).with_name("release-products.json")
	if packaged.is_file():
		return json.loads(packaged.read_text(encoding="utf-8"))
	products = []
	for path in sorted((Path(__file__).resolve().parents[2] / "boards").glob("*/product.json")):
		product = json.loads(path.read_text(encoding="utf-8"))
		products.append({**product, "board": path.parent.name})
	return products


class LocalHTTPServer(http.server.ThreadingHTTPServer):
	# Windows permits duplicate listeners with SO_REUSEADDR; the second launcher
	# must instead detect the existing server and return.
	allow_reuse_address = False


def make_handler(site, package_id):
	products = []
	for product in release_products():
		prefix, board = re.escape(product["prefix"]), re.escape(product["board"])
		pattern = re.compile(rf"/_{prefix}_release/(?:(RC)/)?({prefix}-{board}-(v\d{{2}}\.\d{{1,2}}(?:\.\d+)?(?:b\d+)?))\.(bin|json)")
		products.append((pattern, product))
	class Handler(http.server.SimpleHTTPRequestHandler):
		extensions_map = {
			**http.server.SimpleHTTPRequestHandler.extensions_map,
			".js": "text/javascript", ".mjs": "text/javascript", ".json": "application/json",
			".wasm": "application/wasm", ".svg": "image/svg+xml",
		}

		def _request(self, head=False):
			try:
				host = urllib.parse.urlsplit("//" + self.headers.get("Host", "")).hostname
			except ValueError:
				host = None
			if host not in ("localhost", "127.0.0.1"):
				self.send_error(403, "Local access only")
				return
			path = urllib.parse.urlsplit(self.path).path
			asset, product = next(((match, product) for pattern, product in products
				if (match := pattern.fullmatch(path))), (None, None))
			if asset:
				self._release_asset(asset, product, head)
			elif path == HEALTH:
				body = json.dumps({"app": APP, "package": package_id}).encode()
				self.send_response(200)
				self.send_header("Content-Type", "application/json")
				self.send_header("Content-Length", str(len(body)))
				self.end_headers()
				if not head:
					self.wfile.write(body)
			elif head:
				super().do_HEAD()
			else:
				super().do_GET()

		def _release_asset(self, match, product, head):
			candidate, base, version, extension = match.groups()
			tag = ("RC-" if candidate else "") + product["prefix"] + "-" + version
			url = f"https://github.com/{product['repository']}/releases/download/{tag}/{base}.{extension}"
			request = urllib.request.Request(url, headers={"User-Agent": product["name"] + "-CPS"})
			limit = 1024 * 1024 if extension == "bin" else 32 * 1024
			try:
				with urllib.request.urlopen(request, timeout=15) as response:
					body = response.read(limit + 1)
				if len(body) > limit:
					raise ValueError("Release asset exceeds its size limit")
			except (OSError, ValueError):
				self.send_error(502, "Release asset is unavailable")
				return
			self.send_response(200)
			self.send_header("Content-Type", "application/octet-stream" if extension == "bin" else "application/json")
			self.send_header("Content-Length", str(len(body)))
			self.end_headers()
			if not head:
				self.wfile.write(body)

		def do_GET(self):
			self._request()

		def do_HEAD(self):
			self._request(head=True)

		def end_headers(self):
			self.send_header("Cache-Control", "no-cache")
			self.send_header("X-Content-Type-Options", "nosniff")
			super().end_headers()

		def list_directory(self, path):
			self.send_error(404, "File not found")
			return None

		def log_message(self, fmt, *args):
			pass

	return functools.partial(Handler, directory=str(site))


def probe_server(port):
	try:
		opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
		with opener.open(f"http://127.0.0.1:{port}{HEALTH}", timeout=1) as response:
			return json.load(response)
	except (OSError, ValueError, urllib.error.URLError):
		return None


def open_browser(url):
	if os.name == "nt":
		try:
			os.startfile("microsoft-edge:" + url)
			return
		except OSError:
			pass
	try:
		if webbrowser.open(url):
			return
	except webbrowser.Error:
		pass
	print("Open the address above in Microsoft Edge or Google Chrome.", flush=True)


def main(argv=None):
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--port", type=int, default=8765)
	ap.add_argument("--no-browser", action="store_true")
	args = ap.parse_args(argv)
	if not 0 <= args.port <= 65535:
		ap.error("port must be between 0 and 65535")
	base = Path(__file__).resolve().parent
	site = base / "site"
	if not (site / "codeplug" / "index.html").is_file():
		print("CPS files are missing. Extract the whole ZIP before starting.", flush=True)
		return 1
	package_id = json.loads((base / "build-info.json").read_text(encoding="utf-8"))["package_id"]
	try:
		server = LocalHTTPServer(("127.0.0.1", args.port), make_handler(site, package_id))
	except OSError:
		existing = probe_server(args.port)
		if existing != {"app": APP, "package": package_id}:
			print(f"Port {args.port} is in use. Close the other server, or start with --port 8766.", flush=True)
			return 1
		url = f"http://localhost:{args.port}/codeplug/"
		print("This CPS is already running: " + url, flush=True)
		if not args.no_browser:
			open_browser(url)
		return 0
	url = f"http://localhost:{server.server_address[1]}/codeplug/"
	print("RadioFoundry Offline CPS\n" + url, flush=True)
	print("Keep this window open while using the CPS. Close it or press Ctrl+C to stop.", flush=True)
	if not args.no_browser:
		open_browser(url)
	try:
		server.serve_forever()
	except KeyboardInterrupt:
		pass
	finally:
		server.server_close()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
