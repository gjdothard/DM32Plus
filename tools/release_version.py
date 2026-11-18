#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Garrett Dothard
"""Release names and ordering (docs/dev/release.md).

New releases use vMAJOR.MINOR.PATCH, optionally followed by -beta.N or -rc.N.
Historical vYY.M[.N][bN] names remain readable and sort before the new series.
The historical calendar spelling is reserved and recognized first.

    release_version.py check TAG | pick TAG... | prerelease TAG
    release_version.py next BASE beta|rc TAG...
    release_version.py --self-test
"""
import os
import re
import subprocess
import sys

# Calendar-shaped names are historical identifiers, including their patch releases.
LEGACY_RE = re.compile(r"v(2[6-9]|[3-9][0-9])\.(1[0-2]|[1-9])(?:\.([1-9][0-9]{0,2}))?(?:b([1-9][0-9]{0,2}))?")
NUMBER = r"(?:0|[1-9][0-9]{0,5})"
POSITIVE = r"[1-9][0-9]{0,5}"
SEMANTIC_RE = re.compile(rf"v({NUMBER})\.({NUMBER})\.({NUMBER})(?:-(beta|rc)\.({POSITIVE}))?")


def parse(tag):
    """Comparable (series, major, minor, patch, stage, count), or None.

    Stages are beta=0, rc=1, final=2. The semantic series follows calendar releases
    so a radio on an old calendar build can upgrade to v1.0.0.
    """
    if not isinstance(tag, str):
        return None
    m = LEGACY_RE.fullmatch(tag)
    if m:
        year, month, patch, beta = m.groups()
        return (0, int(year), int(month), int(patch or 0), 0 if beta else 2, int(beta or 0))
    m = SEMANTIC_RE.fullmatch(tag)
    if not m:
        return None
    major, minor, patch, stage, count = m.groups()
    return (1, int(major), int(minor), int(patch), {"beta": 0, "rc": 1, None: 2}[stage], int(count or 0))


def is_prerelease(tag):
    key = parse(tag)
    return key is not None and key[4] < 2


def from_tag(tag, prefix):
    """Read a canonical tag or an existing product-prefixed calendar tag."""
    if not isinstance(tag, str):
        return None
    key = parse(tag)
    if key is not None and key[0] == 1:
        return tag
    for start in (prefix + "-", "RC-" + prefix + "-"):
        if tag.startswith(start) and (key := parse(tag[len(start):])) is not None and key[0] == 0:
            return tag[len(start):]
    return None


def next_prerelease(base, channel, tags):
    tags = list(tags)
    key = parse(base)
    if key is None or key[0] != 1 or key[4] != 2 or channel not in ("beta", "rc"):
        raise ValueError("base must be a semantic final version and channel must be beta or rc")
    if base in tags:
        raise ValueError("the base version is already released; advance the product version first")
    start = base + "-" + channel + "."
    counts = [parse(t)[5] for t in tags if t.startswith(start) and parse(t) is not None]
    count = max(counts, default=0) + 1
    version = start + str(count)
    if parse(version) is None:
        raise ValueError("prerelease counter exhausted")
    return version


def newest(tags):
    names = [t for t in tags if parse(t) is not None]
    return max(names, key=parse) if names else None


def self_test():
    ok = True

    def want(cond, what):
        nonlocal ok
        if not cond:
            print("FAIL:", what)
            ok = False

    for good in ("v1.0.0", "v0.1.0", "v1.0.0-beta.1", "v1.0.0-rc.1", "v26.11", "v26.1", "v26.11.1", "v26.12b1", "v27.3.2b10"):
        want(parse(good) is not None, "accepts " + good)
    for bad in ("v01.1.0", "v26.011", "v26.13", "v26.0", "2026.11", "v26.11-rc.1",
                "v26.11b0", "v26.11b", "v26.11B1", "v26.12-beta.1", "v26.12-b1", "v2026", "v26.11.1.1", "v2026.11", "v25.12", "v026.1", ""):
        want(parse(bad) is None, "refuses " + repr(bad))
    want(newest(["v26.11", "v26.12b1", "v26.11.1", "noise"]) == "v26.12b1", "newest with a beta")
    want(newest(["v26.12b2", "v26.12", "v26.12b10"]) == "v26.12", "the final after its betas")
    want(newest(["v26.2", "v26.10"]) == "v26.10", "months sort as numbers")
    want(newest(["main", "invalid"]) is None, "no release name")
    want(is_prerelease("v26.12b1") and not is_prerelease("v26.12"), "pre-release")

    # the Makefile takes RELEASE= only when it is a release name
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

    # any board will do (print-vars builds nothing): the first in boards/
    board = sorted(d for d in os.listdir(os.path.join(root, "boards"))
                   if os.path.isfile(os.path.join(root, "boards", d, "board.mk")))[0]

    def make(*args):
        return subprocess.run(["make", "-s", "-C", root, "BOARD=" + board, "print-vars", "VARS=RFOUNDRY_RELEASE"] + list(args),
                              capture_output=True, text=True, timeout=120)
    r = make("RELEASE=v26.11.1")
    want(r.returncode == 0 and "RFOUNDRY_RELEASE=v26.11.1" in r.stdout.splitlines(), "make RELEASE=v26.11.1: " + r.stdout + r.stderr)
    r = make("RELEASE=v1.2")
    want(r.returncode != 0 and "not a release name" in r.stderr, "make RELEASE=v1.2 refused: " + r.stdout + r.stderr)
    r = make("RELEASE=v1.0.0-rc.1")
    want(r.returncode == 0 and "RFOUNDRY_RELEASE=v1.0.0-rc.1" in r.stdout.splitlines(), "make semantic release: " + r.stdout + r.stderr)
    r = make("RELEASE=")
    want(r.returncode == 0 and "RFOUNDRY_RELEASE=" in r.stdout.splitlines(), "make RELEASE= (none): " + r.stdout + r.stderr)
    # tools/release_build.sh refuses a tag that isn't one before it builds anything
    r = subprocess.run([os.path.join(root, "tools", "release_build.sh"), "--toolchain", "arm", "--version", "v1.2",
                        "--out", os.devnull], capture_output=True, text=True, timeout=120)
    want(r.returncode == 2 and "not a release name" in r.stderr, "release_build.sh v1.2 refused: " + r.stdout + r.stderr)

    print("release_version self-test: %s" % ("passed" if ok else "FAILED"))
    return ok


def main(argv):
    if argv[:1] == ["--self-test"]:
        return 0 if self_test() else 1
    if len(argv) == 2 and argv[0] == "check":
        if parse(argv[1]) is None:
            print("%r is not a release name: vMAJOR.MINOR.PATCH[-beta.N|-rc.N], or a historical calendar version (docs/dev/release.md)" % argv[1],
                  file=sys.stderr)
            return 1
        return 0
    if len(argv) >= 3 and argv[0] == "next":
        try:
            print(next_prerelease(argv[1], argv[2], argv[3:]))
            return 0
        except ValueError as e:
            print(str(e), file=sys.stderr)
            return 1
    if argv[:1] == ["pick"]:
        n = newest(argv[1:])
        if n:
            print(n)
        return 0
    if len(argv) == 2 and argv[0] == "prerelease":
        return 0 if is_prerelease(argv[1]) else 1
    print("usage: release_version.py check TAG | pick TAG... | prerelease TAG | next BASE beta|rc TAG... | --self-test", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
