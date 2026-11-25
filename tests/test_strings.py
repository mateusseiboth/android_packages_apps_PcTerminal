"""UI texts: every string and plural in both languages, the same placeholders, never an em dash, and
every id the code uses exists."""
import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / "res"
SRC = ROOT / "src"
LANGUAGES = ("values", "values-pt")
PLACEHOLDER = re.compile(r"%\d\$[sd]")


def read_strings(folder):
    return {e.get("name"): e.text or "" for e in ET.parse(RES / folder / "strings.xml").getroot() if e.tag == "string"}


def read_plurals(folder):
    root = ET.parse(RES / folder / "strings.xml").getroot()
    return {e.get("name"): {item.get("quantity"): item.text or "" for item in e} for e in root if e.tag == "plurals"}


def used_ids(kind):
    ids = set()
    for source in SRC.rglob("*.kt"):
        ids |= set(re.findall(rf"R\.{kind}\.([a-z0-9_]+)", source.read_text()))
    return ids


class StringsTest(unittest.TestCase):
    def test_both_languages_have_the_same_strings(self):
        english, portuguese = (read_strings(f) for f in LANGUAGES)
        self.assertEqual(set(english), set(portuguese))

    def test_both_languages_have_the_same_plurals(self):
        english, portuguese = (read_plurals(f) for f in LANGUAGES)
        self.assertEqual(set(english), set(portuguese))
        for name, items in english.items():
            self.assertIn("other", items, name)
            self.assertIn("other", portuguese[name], name)

    def test_placeholders_match(self):
        english, portuguese = (read_strings(f) for f in LANGUAGES)
        for name, text in english.items():
            self.assertEqual(sorted(PLACEHOLDER.findall(text)), sorted(PLACEHOLDER.findall(portuguese[name])), name)

    def test_plural_placeholders_match(self):
        english, portuguese = (read_plurals(f) for f in LANGUAGES)
        for name, items in english.items():
            expected = sorted(PLACEHOLDER.findall(items["other"]))
            for language in (items, portuguese[name]):
                for quantity, text in language.items():
                    self.assertEqual(expected, sorted(PLACEHOLDER.findall(text)), f"{name}/{quantity}")

    def test_no_em_dash(self):
        for folder in LANGUAGES:
            for name, text in read_strings(folder).items():
                self.assertNotIn("—", text, f"{folder}/{name}")
            for name, items in read_plurals(folder).items():
                for quantity, text in items.items():
                    self.assertNotIn("—", text, f"{folder}/{name}/{quantity}")

    def test_every_id_the_code_uses_exists(self):
        self.assertEqual(set(), used_ids("string") - set(read_strings("values")))
        self.assertEqual(set(), used_ids("plurals") - set(read_plurals("values")))

    def test_no_em_dash_in_code_texts(self):
        for source in SRC.rglob("*.kt"):
            for literal in re.findall(r'"([^"\n]*)"', source.read_text()):
                self.assertNotIn("—", literal, source.name)


if __name__ == "__main__":
    unittest.main()
