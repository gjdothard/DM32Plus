# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
"""The product manual publishes current workflows without the shared engineering archive."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rfoundrysite import build, docs, repo


class ProductDocs(unittest.TestCase):
	def setUp(self):
		self.product = repo.product()
		self.guide = docs.Docs("https://github.com/owner/project", "CPS", ["en", "es"], "en",
			self.product["documentation"])

	def test_only_current_guide_is_published(self):
		self.assertEqual({p["section"] for p in self.guide.pages}, {"user"})
		outputs = {p["out"] for p in self.guide.pages}
		self.assertIn("docs/user/index.html", outputs)
		self.assertIn("docs/user/02-installing.html", outputs)
		self.assertIn("docs/user/20-troubleshooting.html", outputs)
		self.assertNotIn("docs/user/15-usb-network.html", outputs)
		self.assertFalse(any("docs/spec/" in p or "docs/testing/" in p or "docs/dev/" in p for p in outputs))

	def test_chapter_links_and_screenshots_use_public_paths(self):
		index = next(p for p in self.guide.pages if p["index"])
		body, _, fallback = self.guide.render(index, "en")
		self.assertFalse(fallback)
		self.assertIn('href="02-installing.html"', body)
		self.assertIn('src="../ux/screens/240x320i10x5/home_dmr.png"', body)
		self.assertIn('href="https://github.com/owner/project/blob/CPS/docs/spec/README.md"', body)
		self.assertTrue(self.guide.render(index, "es")[2])

	def test_product_index_and_sidebar_do_not_list_the_archive(self):
		with tempfile.TemporaryDirectory() as out:
			site = build.Site(out, "https://github.com/owner/project", "CPS")
			site.firmware_product = self.product["name"]
			site.release = ""
			site.docs = self.guide
			site.docs_pages("en")
			index = (Path(out) / "docs/index.html").read_text(encoding="utf-8")
			self.assertIn("DM32+ firmware guide", index)
			self.assertEqual(index.count('<article class="card">'), len(self.guide.pages))
			self.assertNotIn("USB Network Mode", index)
			self.assertNotIn("Moving from Stock MD-380", index)
			self.assertNotIn("Hardware survey", index)
			self.assertFalse((Path(out) / "docs/spec").exists())
			chapter = (Path(out) / "docs/user/02-installing.html").read_text(encoding="utf-8")
			self.assertIn("DM32+ firmware guide", chapter)
			self.assertNotIn('href="15-usb-network.html"', chapter)
			self.assertNotIn("Zadig", chapter)


if __name__ == "__main__":
	unittest.main()
