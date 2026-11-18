# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""The public build proxy accepts only same-origin build operations and bounded artifacts."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

from offline_cps_test import server_module


class Proxy(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.calls = []
        calls = self.calls

        class API(BaseHTTPRequestHandler):
            def do_POST(self):
                calls.append((self.path, self.rfile.read(int(self.headers["Content-Length"]))))
                body = b'{"id":"sample","status":"queued"}'
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                calls.append((self.path, None))
                body = b"firmware"
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_):
                pass

        self.api = ThreadingHTTPServer(("127.0.0.1", 0), API)
        self.start(self.api)
        self.http = server_module.LocalHTTPServer(("127.0.0.1", 0), server_module.make_handler(
            Path(self.tmp.name), "test", build_api=f"http://127.0.0.1:{self.api.server_port}/v1/",
            build_origins=("https://public.example",)))
        self.start(self.http)
        self.base = f"http://127.0.0.1:{self.http.server_port}"
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def start(self, http):
        threading.Thread(target=http.serve_forever, daemon=True).start()
        self.addCleanup(http.server_close)
        self.addCleanup(http.shutdown)

    def tearDown(self):
        self.tmp.cleanup()

    def post(self, path="builds", origin=None, data=b'{"features":["FM"]}'):
        return urllib.request.Request(self.base + "/_firmware_build/v1/" + path, data=data,
                                      headers={"Origin": origin or self.base, "Content-Type": "application/json"})

    def test_same_origin_post_and_artifact_download(self):
        with self.opener.open(self.post()) as response:
            self.assertEqual(json.load(response)["status"], "queued")
        self.assertEqual(self.calls, [("/v1/builds", b'{"features":["FM"]}')])
        with self.opener.open(self.base + "/_firmware_build/v1/builds/" + "a" * 64 + "/firmware.bin") as response:
            self.assertEqual(response.headers.get_content_type(), "application/octet-stream")
            self.assertEqual(response.read(), b"firmware")

    def test_cross_origin_post_does_not_reach_builder(self):
        with self.assertRaises(urllib.error.HTTPError) as raised:
            self.opener.open(self.post(origin="https://unrelated.invalid"))
        self.assertEqual(raised.exception.code, 403)
        self.assertEqual(self.calls, [])

    def test_operator_configured_public_origin_works_behind_a_proxy(self):
        with self.opener.open(self.post(origin="https://public.example")) as response:
            self.assertEqual(response.status, 200)
        self.assertEqual(len(self.calls), 1)

    def test_arbitrary_paths_and_oversized_requests_are_rejected(self):
        for path, data, code in (("../reports", b"{}", 404), ("builds", b"x" * 32769, 413)):
            with self.subTest(path=path), self.assertRaises(urllib.error.HTTPError) as raised:
                self.opener.open(self.post(path=path, data=data))
            self.assertEqual(raised.exception.code, code)
        self.assertEqual(self.calls, [])


if __name__ == "__main__":
    unittest.main()
