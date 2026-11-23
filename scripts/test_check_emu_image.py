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


def desc(build_dir, medium_dir, lcd_dir, nfc_dir, mod_dir, sg_dir, lab_dir):
    return {
        "build_dir": build_dir,
        "build_components": ["stark_ui", "stark_storage_medium", "stark_nfc_transport",
                             "stark_module_transport", "stark_subghz_transport",
                             "stark_lab_transport", "esp_lcd_ili9341", "main", ""],
        "build_component_info": {
            "stark_ui": comp(REPO + "/components/stark_ui", [REPO + "/components/stark_ui/a.c"]),
            "stark_storage_medium": comp(medium_dir, [medium_dir + "/m.c"]),
            "stark_nfc_transport": comp(nfc_dir, [nfc_dir + "/t.c"]),
            "stark_module_transport": comp(mod_dir, [mod_dir + "/t.c"]),
            "stark_subghz_transport": comp(sg_dir, [sg_dir + "/t.c"]),
            "stark_lab_transport": comp(lab_dir, [lab_dir + "/t.c"]),
            "esp_lcd_ili9341": comp(lcd_dir),
            "main": comp(REPO + "/main", [build_dir + "/gen.c"]),
        },
    }


PROD = desc("/p/build", REPO + "/components/stark_storage_medium",
            REPO + "/managed_components/esp_lcd_ili9341", REPO + "/components/stark_nfc_transport",
            REPO + "/components/stark_module_transport",
            REPO + "/components/stark_subghz_transport",
            REPO + "/components/stark_lab_transport")
EMU = desc("/p/build/emu-storage", REPO + "/" + chk.SUBSTITUTE_DIRS["stark_storage_medium"],
           REPO + "/test/emu/storage_image/managed_components/esp_lcd_ili9341",
           REPO + "/" + chk.SUBSTITUTE_DIRS["stark_nfc_transport"],
           REPO + "/" + chk.SUBSTITUTE_DIRS["stark_module_transport"],
           REPO + "/" + chk.SUBSTITUTE_DIRS["stark_subghz_transport"],
           REPO + "/" + chk.SUBSTITUTE_DIRS["stark_lab_transport"])


class Components(unittest.TestCase):
    def test_nfc_transport_not_substituted(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_nfc_transport"] = \
            PROD["build_component_info"]["stark_nfc_transport"]
        msg = " ".join(chk.compare_components(PROD, emu, REPO))
        self.assertIn("stark_nfc_transport not substituted", msg)

    def test_module_transport_not_substituted(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_module_transport"] = \
            PROD["build_component_info"]["stark_module_transport"]
        msg = " ".join(chk.compare_components(PROD, emu, REPO))
        self.assertIn("stark_module_transport not substituted", msg)

    def test_subghz_transport_not_substituted(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_subghz_transport"] = \
            PROD["build_component_info"]["stark_subghz_transport"]
        msg = " ".join(chk.compare_components(PROD, emu, REPO))
        self.assertIn("stark_subghz_transport not substituted", msg)

    def test_lab_transport_not_substituted(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_lab_transport"] = \
            PROD["build_component_info"]["stark_lab_transport"]
        msg = " ".join(chk.compare_components(PROD, emu, REPO))
        self.assertIn("stark_lab_transport not substituted", msg)

    def test_exactly_five_substitutions_are_allowed(self):
        self.assertEqual(len(chk.SUBSTITUTED), 5)
        self.assertEqual(chk.MAX_SUBSTITUTIONS, 5)
        self.assertEqual(chk.compare_components(PROD, EMU, REPO), [])

    def test_a_sixth_substitution_is_refused(self):
        emu = copy.deepcopy(EMU)
        emu["build_component_info"]["stark_ui"]["dir"] = \
            REPO + "/test/emu/storage_image/components/stark_ui"
        self.assertIn("stark_ui comes from", " ".join(chk.compare_components(PROD, emu, REPO)))

    def test_a_sixth_name_in_the_list_is_refused(self):
        listed = chk.SUBSTITUTED
        try:
            chk.SUBSTITUTED = listed + ("stark_sixth_transport",)
            msg = " ".join(chk.compare_components(PROD, EMU, REPO))
        finally:
            chk.SUBSTITUTED = listed
        self.assertIn("ADR-0023 allows at most 5", msg)

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
