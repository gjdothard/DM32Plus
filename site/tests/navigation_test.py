# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""Product navigation, branding, support destinations, and release links."""
from html.parser import HTMLParser
from pathlib import Path
import sys
import tempfile
import unittest
from urllib.parse import urljoin

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rfoundrysite import build


class Header(HTMLParser):
	def __init__(self, text):
		super().__init__()
		self.nav, self.brand, self.in_nav = [], None, False
		self.feed(text)

	def handle_starttag(self, tag, attrs):
		attrs = dict(attrs)
		if tag == "nav" and attrs.get("id") == "site-nav":
			self.in_nav = True
		if tag == "a":
			if attrs.get("class") == "brand":
				self.brand = attrs["href"]
			if self.in_nav:
				self.nav.append(attrs)

	def handle_endtag(self, tag):
		if tag == "nav":
			self.in_nav = False


class Navigation(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self.tmp.cleanup)
		self.site = build.Site(self.tmp.name, "https://github.com/gjdothard/DM32Plus", "main")
		self.site.release = ""

	def test_same_product_header_on_every_page_and_language(self):
		for lang in self.site.i18n.codes:
			prefix = self.site.i18n.prefix(lang)
			for rel, kind, current in (("index.html", "home", "home"), ("codeplug/index.html", "codeplug", "codeplug"),
				("flash/index.html", "flash", "flash"), ("screenshots/index.html", "screenshots", "screenshots"),
				("tools/index.html", "tools", "tools"), ("docs/user/index.html", "doc", "docs"), ("404.html", "404", "")):
				with self.subTest(lang=lang, page=rel):
					self.site.page(lang, rel, "Test", "<p>Test</p>", kind, nav=current)
					text = (Path(self.tmp.name) / prefix / rel).read_text(encoding="utf-8")
					header = Header(text)
					base = "https://preview.example/" + prefix + rel
					actual = [urljoin(base, a["href"]) for a in header.nav]
					root = "https://preview.example/" + prefix
					expected = [root + "index.html", root + "flash/index.html#radio=" + self.site.product["board"],
						root + "codeplug/index.html", root + "docs/index.html", root + "screenshots/index.html",
						root + "tools/index.html", self.site.repo_url + "/releases"]
					self.assertEqual(actual, expected)
					self.assertEqual(urljoin(base, header.brand), root + "index.html")
					self.assertEqual(sum("aria-current" in a for a in header.nav), bool(current))
					self.assertIn(self.site.product["brand"] + "<b>" + self.site.product["accent"], text)
					self.assertNotIn("facebook", text.lower())
					self.assertNotIn("radiofoundry.org", text)

	def test_product_scope_excludes_other_boards_and_planned_ports(self):
		self.site.boards = [{"id": "other"}, {"id": self.site.product["board"]}]
		self.site.planned = [{"radio": "Future port"}]
		self.site.scope_product()
		self.assertEqual(self.site.boards, [{"id": self.site.product["board"]}])
		self.assertEqual(self.site.planned, [])

	def test_missing_product_board_is_an_error(self):
		self.site.boards, self.site.planned = [], []
		with self.assertRaises(build.repo.SourceError):
			self.site.scope_product()

	def test_release_footer_uses_canonical_semantic_tags(self):
		self.site.release = "v1.0.0"
		self.assertIn('/releases/tag/v1.0.0"', self.site.firmware_line("en"))
		self.site.release = "v26.11"
		self.assertIn('/releases/tag/' + self.site.product["prefix"] + '-v26.11"', self.site.firmware_line("en"))

	def test_root_relative_links_resolve_inside_the_site(self):
		out = Path(self.tmp.name)
		(out / "nested").mkdir()
		(out / "index.html").write_text('<a href="/nested/">Nested</a>', encoding="utf-8")
		(out / "nested/index.html").write_text('<a href="/">Home</a>', encoding="utf-8")
		self.site.check_links()
		self.assertEqual(self.site.warnings, [])
		(out / "index.html").write_text('<a href="/missing/">Missing</a>', encoding="utf-8")
		self.site.check_links()
		self.assertEqual(len(self.site.warnings), 1)


if __name__ == "__main__":
	unittest.main()
