# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Garrett Dothard
"""Portable launcher HTTP behavior, independent of the site build and installed runtimes."""
import importlib.util
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
		filename = "dm32plus-dm32-v26.11b1.json"
		for endpoint, tag in (("", "dm32plus-v26.11b1"), ("RC/", "RC-dm32plus-v26.11b1")):
			with self.subTest(tag=tag):
				with patch.object(server_module.urllib.request, "urlopen", return_value=BytesIO(b"{}")) as upstream:
					with self.opener.open(self.base + "/_dm32plus_release/" + endpoint + filename) as response:
						self.assertEqual(response.read(), b"{}")
				self.assertEqual(upstream.call_args.args[0].full_url,
					f"https://github.com/gjdothard/DM32Plus/releases/download/{tag}/{filename}")

	def test_second_listener_cannot_take_existing_port(self):
		with self.assertRaises(OSError):
			server_module.LocalHTTPServer(("127.0.0.1", self.port), server_module.make_handler(Path(self.tmp.name), "test"))


if __name__ == "__main__":
	unittest.main()
