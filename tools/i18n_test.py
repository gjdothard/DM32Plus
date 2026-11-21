#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Garrett Dothard
"""Tests of tools/i18n.py (docs/dev/translating.md): a string with no usable translation is left out of the generated
table, so the radio shows its English; a string whose English changed a little keeps its translation; the check lists
every string still to do."""
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n  # noqa: E402

PO = """msgid ""
msgstr ""
"Language: xx\\n"
"X-Language-Name: Test\\n"

msgid "Done"
msgstr "Hecho"

msgid "Untranslated"
msgstr ""

#, fuzzy
msgid "Fuzzy"
msgstr "Borroso"

msgid "Volume "
msgstr "Volumen"

msgid "Bad"
msgstr "中"
"""


class Generate(unittest.TestCase):
    def test_synced_catalog_keeps_copyright_notices(self):
        path = os.path.join(self.dir.name, "xx.po")
        headers, entries, refs = i18n.synced(path, [("Done", ["core/ui/x.c"])])
        i18n.write_po(path, headers, entries, refs)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        self.assertIn("# Copyright (c) 2026 The RadioFoundry Authors\n", text)
        self.assertIn("# Copyright (c) 2026 Garrett Dothard\n", text)

    def test_catalog_on_another_drive(self):
        path = os.path.join(self.dir.name, "xx.po")
        with patch.object(i18n.os.path, "relpath", side_effect=ValueError("different drives")):
            headers, entries = i18n.read_po(path)
            keys, built = i18n.tables()
        self.assertEqual(headers["Language"], "xx")
        self.assertIn(("Done", "Hecho", False), entries)
        self.assertEqual(i18n.lookup(keys, built[0][4], built[0][5], b"Done"), b"Hecho")

    def test_catalog_error_on_another_drive_names_the_file(self):
        path = os.path.join(self.dir.name, "bad.po")
        with open(path, "w", encoding="utf-8") as f:
            f.write("msgstr \"orphan\"\n")
        with patch.object(i18n.os.path, "relpath", side_effect=ValueError("different drives")):
            with self.assertRaises(i18n.I18nError) as error:
                i18n.read_po(path)
        self.assertIn(os.path.abspath(path) + ":1:", str(error.exception))

    def test_source_references_use_forward_slashes(self):
        self.assertTrue(all("\\" not in path for path in i18n.source_files()))

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.saved = i18n.LANG_DIR
        i18n.LANG_DIR = self.dir.name
        with open(os.path.join(self.dir.name, "xx.po"), "w", encoding="utf-8") as f:
            f.write(PO)
        out = os.path.join(self.dir.name, "tables.h")
        i18n.generate(out)
        with open(out, encoding="utf-8") as f:
            self.h = f.read()

    def tearDown(self):
        i18n.LANG_DIR = self.saved
        self.dir.cleanup()

    def test_only_usable_translations_in_the_table(self):
        self.assertIn('"Hecho"', self.h)
        self.assertIn("1 strings", self.h)      # empty, fuzzy, a lost space, an undrawable character: English
        for text in ("Borroso", "Volumen"):
            self.assertNotIn(text, self.h)

    def test_languages_share_the_hashes(self):
        # A second catalog: one hash array for both; a translation equal to the English is left out ("" in its text)
        with open(os.path.join(self.dir.name, "yy.po"), "w", encoding="utf-8") as f:
            f.write(PO.replace("Language: xx", "Language: yy").replace('"Hecho"', '"Done"')
                    + '\nmsgid "Only here"\nmsgstr "Solo qui"\n')
        out = os.path.join(self.dir.name, "tables.h")
        i18n.generate(out)
        with open(out, encoding="utf-8") as f:
            h = f.read()
        self.assertEqual(h.count("static const uint32_t"), 1)
        self.assertIn("#define LANG_STRINGS\t2", h)     # "Done" (xx) and "Only here" (yy)
        self.assertNotIn('"Done"', h)
        keys, built = i18n.tables()
        got = {code: [i18n.lookup(keys, blob, offs, en) for en in (b"Done", b"Only here", b"Fuzzy")]
               for code, _, _, _, blob, offs in built}
        self.assertEqual(got, {"xx": [b"Hecho", None, None], "yy": [None, b"Solo qui", None]})

    def test_default_language_switch(self):
        # an empty language setting is the language whose CONFIG_LANG_DEFAULT_<CODE> is on, else English
        self.assertIn("#if CONFIG_LANG_DEFAULT_XX\n#define LANG_DEFAULT\t1\n#else\n#define LANG_DEFAULT\tLANG_ENGLISH\n#endif",
                      self.h)

    def test_bad_translations_found(self):
        _, body = i18n.read_po(os.path.join(self.dir.name, "xx.po"))
        for msgid, msgstr, _ in body:
            errs = i18n.check_translation(msgid, msgstr, msgid) if msgstr else []
            self.assertEqual(bool(errs), msgid in ("Volume ", "Bad"), msgid)

    def test_changed_english_keeps_its_translation(self):
        path = os.path.join(self.dir.name, "xx.po")
        # "Done" reworded, a new string, "Bad" gone
        source = [("Done!", ["core/ui/x.c"]), ("Untranslated", ["core/ui/x.c"]), ("Fuzzy", ["core/ui/x.c"]),
                  ("Volume ", ["core/ui/x.c"]), ("Brand new text", ["core/ui/x.c"])]
        headers, entries, refs = i18n.synced(path, source)
        got = {m: (s, f) for m, s, f in entries}
        self.assertEqual(got["Done!"], ("Hecho", False))
        self.assertEqual(got["Brand new text"], ("", False))
        todo = "\n".join(i18n.pending("xx.po", entries))
        for text in ("'Untranslated': not translated", "'Fuzzy': marked fuzzy", "'Brand new text': not translated"):
            self.assertIn(text, todo)
        self.assertNotIn("Done", todo)
        self.assertEqual(i18n.pending("xx.po", [("Done", "Hecho", False)]), [])

    def test_fuzzy_note_survives_a_rewrite(self):
        path = os.path.join(self.dir.name, "xx.po")
        entries = [("Done!", "Hecho", "Done"), ("Fuzzy", "Borroso", True)]
        i18n.write_po(path, {"Language": "xx"}, entries, {})
        self.assertEqual(i18n.read_po(path)[1], entries)
        self.assertIn("made for 'Done'", "\n".join(i18n.pending("xx.po", entries)))

class Catalogs(unittest.TestCase):
    def test_every_translation_found(self):
        # The radio's catalogs through the lookup the firmware does: each usable translation at its place in the
        # stride, the rest the English
        keys, built = i18n.tables()
        for code, _, _, _, blob, offs in built:
            _, body = i18n.read_po(i18n.catalogs()[code])
            for msgid, msgstr, fz in body:
                en = i18n.text_to_bytes(msgid, msgid)
                usable = msgstr and not fz and not i18n.check_translation(msgid, msgstr, msgid)
                want = i18n.text_to_bytes(msgstr, msgid) if usable else None
                self.assertEqual(i18n.lookup(keys, blob, offs, en), None if want == en else want, (code, msgid))


if __name__ == "__main__":
    unittest.main()
