#!/usr/bin/env python3
"""
wokwi_gate.py — does this change set need Wokwi runtime validation?

The CI `wokwi` job always runs; this script decides whether it spends
simulator minutes. Wokwi CI minutes are a monthly quota (TESTING.md §4), so a
change that cannot alter the firmware, the simulated circuit or the scenarios
does not run the simulator — the job reports "not applicable" and succeeds.
Everything else runs every scenario exactly as before, and any failure there
(token, quota, scenario) still fails the job.

The rule is conservative by construction: a path is runtime-relevant unless it
matches NOT_RUNTIME below. Anything new, unknown or unclassifiable runs Wokwi.

NOT_RUNTIME holds only inputs that never reach the firmware image or the
simulator:
  * Markdown anywhere — no build step reads a .md file (if a component ever
    embeds one via EMBED_FILES/EMBED_TXTFILES, remove the rule first);
  * docs/, licence files;
  * test/host/ — the standalone host test project, never part of idf.py build;
  * repository lint tooling (formatting config, check.sh, fmt.sh,
    check_pins.py, check_layers.py, test_host.sh) and this gate with its tests;
  * .github/ — CI orchestration. It changes how validation is invoked, not
    what the firmware does; actionlint and the gate's unit tests (lint job)
    cover it, and the next runtime-relevant change runs the Wokwi path.

Runtime-relevant (non-exhaustive, all implied by the default): components/,
apps/, main/, CMakeLists.txt, sdkconfig.defaults*, partitions.csv,
dependencies.lock, idf_component.yml, .idf-version, diagram.json, wokwi.toml,
test/scenarios/, tools/, scripts/build.sh, scripts/setup.sh,
scripts/test_wokwi.sh.

Usage (CI):
  wokwi_gate.py --event pull_request --base <sha> --head <sha> \
      [--fallback-ref origin/main] [--github-output "$GITHUB_OUTPUT"]
  wokwi_gate.py --files a.md components/x.c      # classify a list, no git

Exit status 0 with the decision printed (and `required=true|false` appended to
--github-output); non-zero only on a usage error. When the change set cannot be
determined (unknown event, missing or all-zero base SHA without a usable
fallback, git failure, empty diff) the decision is "required".
"""
import argparse
import fnmatch
import subprocess
import sys

NOT_RUNTIME = (
    "*.md",
    "docs/*",
    "LICENSE",
    "LICENSE.*",
    "test/host/*",
    ".github/*",
    ".clang-format",
    ".editorconfig",
    ".gitignore",
    "scripts/check.sh",
    "scripts/fmt.sh",
    "scripts/check_pins.py",
    "scripts/check_layers.py",
    "scripts/test_host.sh",
    "scripts/wokwi_gate.py",
    "scripts/test_wokwi_gate.py",
)

PR_EVENTS = ("pull_request", "push")


def is_runtime_relevant(path):
    """True unless path matches a NOT_RUNTIME pattern ('*' spans '/')."""
    return not any(fnmatch.fnmatchcase(path, pattern) for pattern in NOT_RUNTIME)


def classify(paths):
    """(required, runtime_relevant_paths). An empty change set is required:
    no evidence that nothing changed is treated as 'unknown'."""
    relevant = [p for p in paths if is_runtime_relevant(p)]
    return (bool(relevant) or not paths), relevant


def _git(*args):
    return subprocess.run(
        ("git",) + args, check=True, capture_output=True, text=True
    ).stdout


def _is_commit(sha):
    if not sha or set(sha) == {"0"}:
        return False
    try:
        _git("cat-file", "-e", sha + "^{commit}")
        return True
    except subprocess.CalledProcessError:
        return False


def changed_files(event, base, head, fallback_ref):
    """(paths, note) or (None, reason) when the change set is unknown."""
    if event not in PR_EVENTS:
        return None, "event '%s' always runs the full validation" % event
    if not _is_commit(head):
        return None, "head %r is not a commit" % head
    note = ""
    if not _is_commit(base):
        # New branch (all-zero `before`), force push, or shallow history.
        note = "base %r unusable; diffing against %s" % (base, fallback_ref)
        base = fallback_ref
        if not _is_commit(base):
            return None, "base unusable and fallback %r not found" % fallback_ref
    try:
        merge_base = _git("merge-base", base, head).strip()
        out = _git("diff", "--name-only", "--no-renames", merge_base, head)
    except subprocess.CalledProcessError as e:
        return None, "git failed: %s" % (e.stderr or e).strip()
    return [line for line in out.splitlines() if line], note


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--files", nargs="*", help="classify these paths (no git)")
    ap.add_argument("--event", default="")
    ap.add_argument("--base", default="")
    ap.add_argument("--head", default="")
    ap.add_argument("--fallback-ref", default="origin/main")
    ap.add_argument("--github-output", help="append required=true|false here")
    args = ap.parse_args(argv)

    if args.files is not None:
        paths, note = args.files, ""
    else:
        paths, note = changed_files(args.event, args.base, args.head, args.fallback_ref)

    if note:
        print("wokwi-gate: " + note)
    if paths is None:
        required, relevant = True, []
    else:
        required, relevant = classify(paths)

    if not required:
        print("Wokwi: not applicable for this change set (%d file(s), none "
              "runtime-relevant)" % len(paths))
    elif paths is None:
        print("Wokwi: required (change set unknown)")
    elif not paths:
        print("Wokwi: required (empty change set)")
    else:
        print("Wokwi: required — runtime-relevant change(s):")
        for p in relevant[:20]:
            print("  " + p)
        if len(relevant) > 20:
            print("  … and %d more" % (len(relevant) - 20))

    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as f:
            f.write("required=%s\n" % ("true" if required else "false"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
