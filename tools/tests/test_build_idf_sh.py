# SPDX-FileCopyrightText: 2026 Daniel Abramowitz
# SPDX-License-Identifier: GPL-3.0-or-later

"""scripts/build_idf.sh mirrors scripts/build_idf.ps1 against a fake ESP-IDF."""

import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "build_idf.sh"

FAKE_PYTHON = """#!/bin/sh
echo "IDF_PATH=$IDF_PATH IDF_TOOLS_PATH=${IDF_TOOLS_PATH-<unset>} JOBS=$IDF_PY_BUILD_JOBS PWD=$PWD" >> "$FAKE_LOG"
for a in "$@"; do echo "  $a" >> "$FAKE_LOG"; done
case "$*" in *idf.py*) exit "${FAKE_IDF_EXIT:-0}" ;; esac
exit "${FAKE_PREPARE_EXIT:-0}"
"""


@unittest.skipUnless(sys.platform == "linux" and shutil.which("bash"), "Linux build wrapper")
class BuildIdfShTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.idf = root / "esp-idf"
        self.tools = root / "idf-tools"
        self.log = root / "calls.log"
        venv = self.tools / "python_env" / "bin"
        venv.mkdir(parents=True)
        (self.idf / "tools").mkdir(parents=True)
        python = venv / "python"
        python.write_text(FAKE_PYTHON, encoding="utf-8")
        python.chmod(python.stat().st_mode | stat.S_IXUSR)
        self.python = python
        (self.idf / "export.sh").write_text(
            'if [ -n "${FAKE_EXPORT_FAIL-}" ]; then return 1; fi\n'
            'IDF_PYTHON_ENV_PATH="$IDF_TOOLS_PATH/python_env"; export IDF_PYTHON_ENV_PATH\n',
            encoding="utf-8",
        )

    def run_script(self, *args, **env_extra):
        env = {"PATH": os.environ["PATH"], "HOME": self.temp.name, "FAKE_LOG": str(self.log)}
        env.update(env_extra)
        return subprocess.run(
            ["bash", str(SCRIPT), *args], env=env, capture_output=True, text=True, timeout=30
        )

    def calls(self):
        return self.log.read_text(encoding="utf-8").splitlines() if self.log.exists() else []

    def test_runs_prepare_then_idf_py_exactly_like_the_powershell_wrapper(self):
        result = self.run_script(
            "--profile", "7_dual_i2c", "--idf-path", str(self.idf), "--tools-path", str(self.tools),
            "--jobs", "8",
        )
        self.assertEqual(0, result.returncode, result.stderr)
        build_dir = REPO_ROOT / "build-idf-7_dual_i2c"
        context = f"IDF_PATH={self.idf} IDF_TOOLS_PATH={self.tools} JOBS=8 PWD={REPO_ROOT}"
        self.assertEqual(
            [
                context,
                f"  {REPO_ROOT / 'scripts' / 'idf_prepare.py'}",
                "  --profile", "  7_dual_i2c", "  --build-dir", f"  {build_dir}",
                context,
                f"  {self.idf / 'tools' / 'idf.py'}",
                "  -B", f"  {build_dir}",
                "  -D", "  AURA_PROFILE=7_dual_i2c",
                "  -D", f"  Python3_EXECUTABLE={self.python}",
                "  build",
            ],
            self.calls(),
        )
        self.assertIn("development build", result.stdout)

    def test_defaults_come_from_environment_and_match_the_powershell_defaults(self):
        result = self.run_script("--action", "reconfigure", IDF_PATH=str(self.idf), IDF_TOOLS_PATH=str(self.tools))
        self.assertEqual(0, result.returncode, result.stderr)
        calls = self.calls()
        self.assertIn("JOBS=4", calls[0])
        self.assertIn("  4_3", calls)
        self.assertEqual("  reconfigure", calls[-1])
        self.assertNotIn("development build", result.stdout)

    def test_requires_an_idf_checkout(self):
        result = self.run_script()
        self.assertEqual(2, result.returncode)
        self.assertIn("Set IDF_PATH or pass --idf-path to an ESP-IDF 6.1 checkout.", result.stderr)
        result = self.run_script("--idf-path", str(self.idf / "missing"))
        self.assertEqual(2, result.returncode)
        self.assertIn("ESP-IDF path not found", result.stderr)
        self.assertEqual([], self.calls())

    def test_rejects_values_the_powershell_wrapper_rejects(self):
        for args in (
            ("--profile", "7"), ("--jobs", "0"), ("--jobs", "33"), ("--jobs", "x"), ("--jobs", "08"), ("--jobs", "-1"),
            ("--action", "flash"), ("--profile",), ("--bogus",),
        ):
            with self.subTest(args=args):
                result = self.run_script(*args, "--idf-path", str(self.idf), "--tools-path", str(self.tools))
                self.assertEqual(2, result.returncode)
        self.assertEqual([], self.calls())

    def test_failures_stop_the_build_and_are_reported(self):
        result = self.run_script("--idf-path", str(self.idf), "--tools-path", str(self.tools), FAKE_EXPORT_FAIL="1")
        self.assertEqual(2, result.returncode)
        self.assertIn("activation failed", result.stderr)
        self.assertEqual([], self.calls())

        result = self.run_script("--idf-path", str(self.idf), "--tools-path", str(self.tools), FAKE_PREPARE_EXIT="1")
        self.assertEqual(2, result.returncode)
        self.assertIn("preparation failed", result.stderr)
        self.assertNotIn("  build", self.calls())

        self.log.unlink()
        result = self.run_script("--idf-path", str(self.idf), "--tools-path", str(self.tools), FAKE_IDF_EXIT="3")
        self.assertEqual(2, result.returncode)
        self.assertIn("ESP-IDF build failed with exit code 3", result.stderr)


if __name__ == "__main__":
    unittest.main()
