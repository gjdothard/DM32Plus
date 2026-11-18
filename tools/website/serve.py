# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Container entry point; retain the portable server's fixed release proxy."""
import os
from pathlib import Path
from server import LocalHTTPServer, make_handler

site = Path(__file__).resolve().parent / "site"
public_host = os.environ.get("CPS_PUBLIC_HOST", "").strip().lower()
allowed_hosts = ("localhost", "127.0.0.1", public_host)
with LocalHTTPServer(("0.0.0.0", 8080), make_handler(site, "docker", allowed_hosts=allowed_hosts,
        build_api=os.environ.get("CPS_BUILD_API"),
        build_origins=tuple(filter(None, os.environ.get("CPS_BUILD_ORIGINS", "").split(","))))) as server:
    server.serve_forever()
