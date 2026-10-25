#!/usr/bin/env python3
"""Unit tests for scripts/check_emu_image.py (no build needed)."""
import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_emu_image as chk  # noqa: E402

REPO = "/p"


def comp(d, sources=(), reqs=()):
    return {"dir": d, "sources": list(sources), "include_dirs": [d + "/include"],
            "reqs": list(reqs), "priv_reqs": []}


def desc(build_dir, medium_dir, lcd_dir, nfc_dir):
    return {
        "build_dir": build_dir,
        "build_components": ["stark_ui", "stark_storage_medium", "stark_nfc_transport",
                             "esp_lcd_ili9341", "main", ""],
        "build_component_info": {
            "stark_ui": comp(REPO + "/components/stark_ui", [REPO + "/components/stark_ui/a.c"]),
            "stark_storage_medium": comp(medium_dir, [medium_dir + "/m.c"]),
            "stark_nfc_transport": comp(nfc_dir, [nfc_dir + "/t.c"]),
            "esp_lcd_ili9341": comp(lcd_dir),
            "main": comp(REPO + "/main", [build_dir + "/gen.c"]),
        },
    }


PROD = desc("/p/build", REPO + "/components/stark_storage_medium",
            REPO + "/managed_components/esp_lcd_ili9341", REPO + "/components/stark_nfc_transport")
EMU = desc("/p/build/emu-storage", REPO + "/" + chk.SUBSTITUTE_DIRS["stark_storage_medium"],
           REPO + "/test/emu/storage_image/managed_components/esp_lcd_ili9341",
           REPO + "/" + chk.SUBSTITUTE_DIRS["stark_nfc_transport"])


class Components(unittest.TestCase):
    def test_nfc_transport_not_substituted(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_nfc_transport"] = \
            PROD["build_component_info"]["stark_nfc_transport"]
        msg = " ".join(chk.compare_components(PROD, emu, REPO))
        self.assertIn("stark_nfc_transport not substituted", msg)

    def test_only_the_two_substitutions_differ(self):
        self.assertEqual(chk.compare_components(PROD, EMU, REPO), [])

    def test_medium_not_substituted(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_storage_medium"] = \
            PROD["build_component_info"]["stark_storage_medium"]
        self.assertIn("not substituted", " ".join(chk.compare_components(PROD, emu, REPO)))

    def test_extra_component(self):
        emu = copy.deepcopy(EMU)
        emu["build_components"].append("fake_sd")
        emu["build_component_info"]["fake_sd"] = comp("/x")
        self.assertIn("component sets differ", chk.compare_components(PROD, emu, REPO)[0])

    def test_other_component_from_elsewhere(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_ui"]["dir"] = "/p/test/stark_ui"
        self.assertIn("stark_ui comes from", chk.compare_components(PROD, emu, REPO)[0])

    def test_changed_sources(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_ui"]["sources"].append("/p/components/stark_ui/b.c")
        self.assertEqual(chk.compare_components(PROD, emu, REPO), ["stark_ui: sources differ"])


class Config(unittest.TestCase):
    def test_sdkconfig(self):
        self.assertEqual(chk.compare_sdkconfig({"A": 1}, {"A": 1}), [])
        self.assertEqual(chk.compare_sdkconfig({"A": 1}, {"A": 2, "B": 3}),
                         ["sdkconfig differs: A, B"])

    def test_partitions(self):
        prod = "# c\nnvs, data, nvs, , 24K,\nfactory, app, factory, , 4M,\n"
        self.assertEqual(chk.compare_partitions(prod, prod + "emufat, data, fat, , 1M,\n"), [])
        self.assertEqual(len(chk.compare_partitions(prod, "nvs, data, nvs, , 8K,\n")), 1)


if __name__ == "__main__":
    unittest.main()
