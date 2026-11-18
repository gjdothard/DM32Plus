# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Garrett Dothard
"""Portable launcher HTTP behavior, independent of the site build and installed runtimes."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from io import BytesIO
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("offline_server", ROOT / "tools/offline_cps/server.py")
server_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server_module)


class OfflineServerTest(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory(prefix="offline CPS ")
		root = Path(self.tmp.name)
		site = root / "site"
		(site / "codeplug").mkdir(parents=True)
		(site / "codeplug/index.html").write_text("<title>CPS</title>", encoding="utf-8")
		(site / "app.mjs").write_text("export const offline = true;", encoding="utf-8")
		(root / "private.txt").write_text("outside site", encoding="utf-8")
		self.http = server_module.LocalHTTPServer(("127.0.0.1", 0), server_module.make_handler(site, "test-package"))
		self.thread = threading.Thread(target=self.http.serve_forever, daemon=True)
		self.thread.start()
		self.port = self.http.server_address[1]
		self.base = f"http://127.0.0.1:{self.port}"
		self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

	def tearDown(self):
		self.http.shutdown()
		self.http.server_close()
		self.thread.join()
		self.tmp.cleanup()

	def test_page_module_and_head(self):
		with self.opener.open(self.base + "/codeplug/?tab=encryption") as r:
			self.assertEqual(r.status, 200)
			self.assertIn(b"CPS", r.read())
			self.assertEqual(r.headers["Cache-Control"], "no-cache")
		with self.opener.open(urllib.request.Request(self.base + "/app.mjs?v=1", method="HEAD")) as r:
			self.assertEqual(r.headers.get_content_type(), "text/javascript")
			self.assertEqual(r.read(), b"")

	def test_identity_and_no_host_or_path_escape(self):
		self.assertEqual(server_module.probe_server(self.port), {"app": server_module.APP, "package": "test-package"})
		for path in ("/../private.txt", "/%2e%2e/private.txt", "/%2e%2e%5cprivate.txt", "/"):
			with self.assertRaises(urllib.error.HTTPError) as error:
				self.opener.open(self.base + path)
			self.assertEqual(error.exception.code, 404)
		for host in ("other.example", "["):
			with self.assertRaises(urllib.error.HTTPError) as error:
				self.opener.open(urllib.request.Request(self.base + "/codeplug/", headers={"Host": host}))
			self.assertEqual(error.exception.code, 403)

	def test_release_assets_resolve_to_the_selected_tag(self):
		product = server_module.release_products()[0]
		filename = f"{product['prefix']}-{product['board']}-v26.11b1.json"
		for endpoint, tag in (("", product["prefix"] + "-v26.11b1"), ("RC/", "RC-" + product["prefix"] + "-v26.11b1")):
			with self.subTest(tag=tag):
				with patch.object(server_module.urllib.request, "urlopen", return_value=BytesIO(b"{}")) as upstream:
					with self.opener.open(self.base + f"/_{product['prefix']}_release/" + endpoint + filename) as response:
						self.assertEqual(response.read(), b"{}")
				self.assertEqual(upstream.call_args.args[0].full_url,
					f"https://github.com/{product['repository']}/releases/download/{tag}/{filename}")

	def test_semantic_release_assets_use_canonical_tags(self):
		product = server_module.release_products()[0]
		for version in ("v1.0.0", "v1.0.0-beta.10", "v1.0.0-rc.2"):
			filename = f"{product['prefix']}-{product['board']}-{version}.json"
			with patch.object(server_module.urllib.request, "urlopen", return_value=BytesIO(b"{}")) as upstream:
				with self.opener.open(self.base + f"/_{product['prefix']}_release/" + filename) as response:
					self.assertEqual(response.read(), b"{}")
			self.assertEqual(upstream.call_args.args[0].full_url,
				f"https://github.com/{product['repository']}/releases/download/{version}/{filename}")

	def test_second_listener_cannot_take_existing_port(self):
		with self.assertRaises(OSError):
			server_module.LocalHTTPServer(("127.0.0.1", self.port), server_module.make_handler(Path(self.tmp.name), "test"))

	def test_release_proxy_refuses_unrecognized_assets(self):
		product = server_module.release_products()[0]
		prefix, board = product["prefix"], product["board"]
		with patch.object(server_module.urllib.request, "urlopen") as upstream:
			for path in (f"/_{prefix}_release/{prefix}-other-v26.11.bin",
				     f"/_{prefix}_release/{prefix}-{board}-v26.11.zip",
				     f"/_{prefix}_release/RC/../../outside.bin",
				     f"/_{prefix}_release/{prefix}-{board}-v1.0.0-beta.01.bin",
				     f"/_{prefix}_release/RC/{prefix}-{board}-v1.0.0-rc.1.bin"):
				with self.subTest(path=path), self.assertRaises(urllib.error.HTTPError) as error:
					self.opener.open(self.base + path)
				self.assertEqual(error.exception.code, 404)
			upstream.assert_not_called()


class ReleaseProductsTest(unittest.TestCase):
	def test_packaged_metadata_needs_no_repository(self):
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp)
			products = [{"name": "Example", "prefix": "example", "board": "sample", "repository": "owner/project"}]
			(root / "release-products.json").write_text(json.dumps(products), encoding="utf-8")
			with patch.object(server_module, "__file__", str(root / "server.py")):
				self.assertEqual(server_module.release_products(), products)

	def test_source_metadata_infers_board_from_its_directory(self):
		with tempfile.TemporaryDirectory() as tmp:
			root = Path(tmp)
			product = {"name": "Example", "prefix": "example", "repository": "owner/project"}
			(root / "boards/sample").mkdir(parents=True)
			(root / "boards/sample/product.json").write_text(json.dumps(product), encoding="utf-8")
			with patch.object(server_module, "__file__", str(root / "tools/offline_cps/server.py")):
				self.assertEqual(server_module.release_products(), [{**product, "board": "sample"}])


if __name__ == "__main__":
	unittest.main()
