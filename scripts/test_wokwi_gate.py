#!/usr/bin/env python3
"""Unit tests for scripts/wokwi_gate.py (run: python3 -m unittest discover -s scripts)."""
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wokwi_gate as gate  # noqa: E402


class Classify(unittest.TestCase):
    def required(self, *paths):
        return gate.classify(list(paths))[0]

    def test_planning_docs_only_not_required(self):
        self.assertFalse(self.required("TASKS.md"))
        self.assertFalse(self.required("ARCHITECTURE.md", "ROADMAP.md", "DECISIONS.md",
                                       "TESTING.md", "README.md", "WOKWI.md"))
        self.assertFalse(self.required("docs/measurements.md", "docs/bringup/v1.md"))
        self.assertFalse(self.required("components/stark_gfx/fonts/README.md",
                                       "apps/README.md"))

    def test_ci_and_lint_tooling_not_required(self):
        self.assertFalse(self.required(".github/workflows/ci.yml", "scripts/check.sh",
                                       "scripts/fmt.sh", "scripts/check_pins.py",
                                       "scripts/test_host.sh", "scripts/wokwi_gate.py",
                                       "scripts/test_wokwi_gate.py", ".clang-format"))
        self.assertFalse(self.required("test/host/test_gfx.c", "test/host/CMakeLists.txt"))

    def test_firmware_sources_required(self):
        self.assertTrue(self.required("components/stark_ui/ui_stack.c"))
        self.assertTrue(self.required("components/stark_ui/include/stark_ui.h"))
        self.assertTrue(self.required("apps/app_about/app_about.c"))
        self.assertTrue(self.required("main/stark_main.c"))
        self.assertTrue(self.required("main/CMakeLists.txt"))

    def test_simulator_inputs_required(self):
        self.assertTrue(self.required("diagram.json"))
        self.assertTrue(self.required("wokwi.toml"))
        self.assertTrue(self.required("test/scenarios/v0-boot.yaml"))
        self.assertTrue(self.required("test/scenarios/soak/v01-soak.yaml"))
        self.assertTrue(self.required("scripts/test_wokwi.sh"))

    def test_build_configuration_required(self):
        for p in ("CMakeLists.txt", "components/stark_ui/CMakeLists.txt",
                  "components/stark_ui/Kconfig", "sdkconfig.defaults",
                  "sdkconfig.defaults.esp32s3", "partitions.csv", "dependencies.lock",
                  "components/stark_display/idf_component.yml", ".idf-version",
                  "scripts/build.sh", "scripts/setup.sh", "tools/fontconv.py"):
            self.assertTrue(self.required(p), p)

    def test_mixed_change_set_required(self):
        required, relevant = gate.classify(["TASKS.md", "components/stark_gfx/gfx_draw.c"])
        self.assertTrue(required)
        self.assertEqual(relevant, ["components/stark_gfx/gfx_draw.c"])

    def test_unknown_and_empty_required(self):
        self.assertTrue(self.required("some/new/thing.bin"))
        self.assertTrue(self.required(".gitmodules"))
        self.assertTrue(self.required())


class ChangedFiles(unittest.TestCase):
    """changed_files() against a throwaway repository."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.old = os.getcwd()
        os.chdir(self.tmp.name)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "t@example.invalid")
        self.git("config", "user.name", "t")
        self.base = self.commit("README.md")
        self.git("update-ref", "refs/remotes/origin/main", self.base)

    def tearDown(self):
        os.chdir(self.old)
        self.tmp.cleanup()

    def git(self, *args):
        return subprocess.run(("git",) + args, check=True, capture_output=True,
                              text=True).stdout.strip()

    def commit(self, path):
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "a", encoding="utf-8") as f:
            f.write("x\n")
        self.git("add", path)
        self.git("commit", "-q", "-m", path)
        return self.git("rev-parse", "HEAD")

    def decide(self, event, base, head):
        paths, _ = gate.changed_files(event, base, head, "origin/main")
        return None if paths is None else gate.classify(paths)[0]

    def test_pull_request_docs_only(self):
        head = self.commit("TASKS.md")
        self.assertEqual(gate.changed_files("pull_request", self.base, head,
                                            "origin/main")[0], ["TASKS.md"])
        self.assertFalse(self.decide("pull_request", self.base, head))

    def test_push_with_source_change(self):
        self.commit("TASKS.md")
        head = self.commit("components/stark_ui/ui_stack.c")
        self.assertTrue(self.decide("push", self.base, head))

    def test_base_ahead_uses_merge_base(self):
        # PR base moved on after branching: only the branch's own files count.
        branch = self.git("rev-parse", "HEAD")
        self.git("checkout", "-q", "-b", "feature")
        head = self.commit("docs/x.md")
        self.git("checkout", "-q", "main")
        base = self.commit("main/stark_main.c")
        self.assertEqual(self.git("merge-base", base, head), branch)
        self.assertFalse(self.decide("pull_request", base, head))

    def test_zero_before_falls_back(self):
        head = self.commit("ROADMAP.md")
        self.assertFalse(self.decide("push", "0" * 40, head))
        head = self.commit("diagram.json")
        self.assertTrue(self.decide("push", "0" * 40, head))

    def test_missing_base_falls_back(self):
        head = self.commit("TASKS.md")
        self.assertFalse(self.decide("push", "deadbeef" * 5, head))
        self.assertFalse(self.decide("push", "", head))

    def test_unknown_change_set_is_required(self):
        head = self.commit("TASKS.md")
        self.assertIsNone(self.decide("workflow_dispatch", self.base, head))
        self.assertIsNone(self.decide("push", self.base, "f" * 40))
        self.git("update-ref", "-d", "refs/remotes/origin/main")
        self.assertIsNone(self.decide("push", "0" * 40, head))

    def test_main_writes_github_output(self):
        head = self.commit("TASKS.md")
        out = os.path.join(self.tmp.name, "out")
        for event, base, want in (("pull_request", self.base, "false"),
                                  ("workflow_dispatch", self.base, "true")):
            open(out, "w").close()
            gate.main(["--event", event, "--base", base, "--head", head,
                       "--github-output", out])
            with open(out, encoding="utf-8") as f:
                self.assertEqual(f.read(), "required=%s\n" % want)


if __name__ == "__main__":
    unittest.main()
