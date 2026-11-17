from __future__ import annotations

import importlib.util
import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "release.py"
SPEC = importlib.util.spec_from_file_location("divecan_release", SCRIPT)
assert SPEC is not None
assert SPEC.loader is not None
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseScriptTests(unittest.TestCase):
    SOURCE_COMMIT = "a" * 40
    HARNESS_COMMIT = "b" * 40
    TEST_SUITE_COMMIT = "c" * 40

    def _release_workflow_script(self, name):
        workflow = (SCRIPT.parents[2] / ".github/workflows/release.yml").read_text()
        step = workflow.split(f"      - name: {name}\n", 1)[1]
        script = re.search(r"        run: \|\n((?:          .*\n)+)", step)
        self.assertIsNotNone(script)
        return "".join(line[10:] for line in script.group(1).splitlines(keepends=True))

    def test_release_tag_is_local_and_cleaned_after_success_or_failure(self):
        create = self._release_workflow_script("Create local release tag for build identity")
        cleanup = self._release_workflow_script("Remove local release tag")
        for fail_build in (False, True):
            with self.subTest(fail_build=fail_build), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                checkout = root / "checkout"
                remote = root / "remote.git"

                def git(*args):
                    return subprocess.run(
                        ["git", "-C", str(checkout), *args], check=True,
                        capture_output=True, text=True,
                    ).stdout.strip()

                subprocess.run(["git", "init", "--quiet", str(checkout)], check=True)
                subprocess.run(["git", "init", "--bare", "--quiet", str(remote)], check=True)
                git("config", "user.name", "Release test")
                git("config", "user.email", "release-test@example.invalid")
                git("commit", "--quiet", "--allow-empty", "-m", "source")
                git("remote", "add", "origin", str(remote))
                commit = git("rev-parse", "HEAD")
                output = root / "github-output"
                env = dict(os.environ, RELEASE_VERSION="1.2.3", RELEASE_COMMIT=commit,
                           GITHUB_OUTPUT=str(output))
                script = create + ("\nfalse\n" if fail_build else "")
                result = subprocess.run(["bash", "-e", "-c", script], cwd=checkout, env=env)
                self.assertEqual(result.returncode, int(fail_build))
                self.assertEqual(git("describe", "--tags", "--always", "--dirty"), "v1.2.3")
                self.assertEqual(git("rev-parse", "v1.2.3"), commit)
                self.assertIn("created=true", output.read_text())
                self.assertEqual(git("ls-remote", "--tags", "origin"), "")
                subprocess.run(["bash", "-e", "-c", cleanup], cwd=checkout, env=env, check=True)
                self.assertEqual(git("tag", "--list", "v1.2.3"), "")
                self.assertEqual(git("ls-remote", "--tags", "origin"), "")

    def test_six_variants_are_qualified_and_published_including_tecme(self):
        expected = {
            "Poseidon_Aren", "AP_Aren", "AP_Paul", "eCCR_classic",
            "Sidewinder_Gabriel", "TECME",
        }
        self.assertEqual(set(release.PRODUCTION_VARIANTS), expected)
        repository = SCRIPT.parents[2]
        workflow = (repository / ".github/workflows/release.yml").read_text()
        matrix = re.search(
            r"      matrix:\n        variant:\n((?:          - \w+\n)+)",
            workflow,
        )
        self.assertIsNotNone(matrix)
        self.assertEqual(set(re.findall(r"- (\w+)", matrix.group(1))), expected)
        dry_run = workflow.split("- name: Dry-run GitHub Release publish inputs", 1)[1]
        dry_run, publish = dry_run.split("- name: Publish GitHub Release", 1)
        for variant in expected:
            asset = f"DiveCANHead-{variant}-v${{RELEASE_VERSION}}.zip"
            with self.subTest(variant=variant):
                self.assertIn(asset, dry_run)
                self.assertIn(asset, publish)

    def test_physical_hardware_selection_is_forwarded_safely(self):
        repository = SCRIPT.parents[2]
        for filename in ("release.yml", "hil-full.yml"):
            with self.subTest(workflow=filename):
                workflow = (repository / ".github/workflows" / filename).read_text()
                self.assertIn("default: legacy", workflow)
                self.assertIn("- jr-plus-v1.1", workflow)
                expected_calls = 5 if filename == "release.yml" else 4
                self.assertEqual(workflow.count('--dut-hardware "$HIL_DUT_HARDWARE"'), expected_calls)

    def _write_inputs(self, root: Path, version: str = "1.2.3"):
        major, minor, patch = version.split(".")
        version_file = root / "VERSION"
        version_file.write_text(
            f"VERSION_MAJOR = {major}\n"
            f"VERSION_MINOR = {minor}\n"
            f"PATCHLEVEL = {patch}\n"
            "VERSION_TWEAK = 0\n"
            "EXTRAVERSION =\n",
            encoding="utf-8",
        )
        changelog = root / "changelog.txt"
        changelog.write_text(
            "# Changelog\n\n"
            "## Unreleased\n\n"
            "### Added\n\n"
            f"## {version} - 2026-07-29\n\n"
            "### Added\n\n"
            "- Qualified release.\n",
            encoding="utf-8",
        )
        return version_file, changelog

    def test_validate_and_reads_release_notes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            version_file, changelog = self._write_inputs(root)
            self.assertEqual(
                release.derive_release_version(version_file, changelog),
                "1.2.3",
            )
            self.assertEqual(
                release.validate_release("1.2.3", version_file, changelog),
                "1.2.3",
            )
            self.assertIn(
                "- Qualified release.",
                release.read_changelog_release(changelog, "1.2.3"),
            )

    def test_validate_rejects_version_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            version_file, changelog = self._write_inputs(root)
            with self.assertRaises(release.ReleaseError):
                release.validate_release("1.2.4", version_file, changelog)

    def test_stage_rejects_manifest_variant_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            zephyr = build / "Firmware" / "zephyr"
            zephyr.mkdir(parents=True)
            (build / "merged_board.hex").write_bytes(b"hex")
            (zephyr / "zephyr.signed.bin").write_bytes(b"ota")
            (zephyr / "test_manifest.json").write_text(
                json.dumps(
                    {
                        "variant": "AP_Aren",
                        "version": "1.2.3",
                        "commit": self.SOURCE_COMMIT,
                    }
                ),
                encoding="utf-8",
            )
            args = type(
                "Args",
                (),
                {
                    "version": "1.2.3",
                    "variant": "Poseidon_Aren",
                    "commit": self.SOURCE_COMMIT,
                    "build_dir": build,
                    "output_root": root / "staging",
                    "run_url": "https://example.invalid/run",
                    "harness_commit": self.HARNESS_COMMIT,
                    "test_suite_commit": self.TEST_SUITE_COMMIT,
                },
            )()
            with self.assertRaises(release.ReleaseError):
                release.stage_variant(args)

    def test_stage_records_hil_test_suite_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            zephyr = build / "Firmware" / "zephyr"
            zephyr.mkdir(parents=True)
            (build / "merged_board.hex").write_bytes(b"hex")
            (zephyr / "zephyr.signed.bin").write_bytes(b"ota")
            (zephyr / "test_manifest.json").write_text(
                json.dumps(
                    {
                        "variant": "AP_Aren",
                        "version": "1.2.3",
                        "commit": self.SOURCE_COMMIT,
                    }
                ),
                encoding="utf-8",
            )
            args = type(
                "Args",
                (),
                {
                    "version": "1.2.3",
                    "variant": "AP_Aren",
                    "commit": self.SOURCE_COMMIT,
                    "build_dir": build,
                    "output_root": root / "staging",
                    "run_url": "https://example.invalid/run",
                    "harness_commit": self.HARNESS_COMMIT,
                    "test_suite_commit": self.TEST_SUITE_COMMIT,
                    "dut_hardware": "jr-plus-v1.1",
                },
            )()

            release.stage_variant(args)

            qualification = json.loads(
                (
                    root
                    / "staging"
                    / "AP_Aren"
                    / "qualification.json"
                ).read_text(encoding="utf-8")
            )
            self.assertEqual(
                qualification["test_suite_commit"], self.TEST_SUITE_COMMIT
            )
            self.assertEqual(qualification["dut_hardware"], "jr-plus-v1.1")

    def test_bundle_requires_every_production_variant(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            version_file, changelog = self._write_inputs(root)
            source_root = root / "source"
            source_root.mkdir()
            args = type(
                "Args",
                (),
                {
                    "version": "1.2.3",
                    "commit": self.SOURCE_COMMIT,
                    "source_root": source_root,
                    "output_dir": root / "output",
                    "version_file": version_file,
                    "changelog": changelog,
                    "run_url": "https://example.invalid/run",
                },
            )()
            with self.assertRaises(release.ReleaseError):
                release.bundle_release(args)

    def test_bundle_contains_all_qualified_variants(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            version_file, changelog = self._write_inputs(root)
            source_root = root / "source"
            source_root.mkdir()
            for variant in release.PRODUCTION_VARIANTS:
                variant_dir = source_root / variant
                variant_dir.mkdir()
                full_name = f"DiveCANHead-{variant}-v1.2.3-full.hex"
                ota_name = f"DiveCANHead-{variant}-v1.2.3-ota.bin"
                (variant_dir / full_name).write_bytes(b"full")
                (variant_dir / ota_name).write_bytes(b"ota")
                (variant_dir / "test_manifest.json").write_text(
                    json.dumps(
                        {
                            "variant": variant,
                            "version": "1.2.3",
                            "commit": self.SOURCE_COMMIT,
                        }
                    ),
                    encoding="utf-8",
                )
                files = {
                    name: release._sha256(variant_dir / name)
                    for name in (full_name, ota_name, "test_manifest.json")
                }
                (variant_dir / "qualification.json").write_text(
                    json.dumps(
                        {
                            "result": "passed",
                            "version": "1.2.3",
                            "commit": self.SOURCE_COMMIT,
                            "variant": variant,
                            "harness_commit": self.HARNESS_COMMIT,
                            "test_suite_commit": self.TEST_SUITE_COMMIT,
                            "files": files,
                        }
                    ),
                    encoding="utf-8",
                )

            output_dir = root / "output"
            args = type(
                "Args",
                (),
                {
                    "version": "1.2.3",
                    "commit": self.SOURCE_COMMIT,
                    "source_root": source_root,
                    "output_dir": output_dir,
                    "version_file": version_file,
                    "changelog": changelog,
                    "run_url": "https://example.invalid/run",
                },
            )()
            release.bundle_release(args)

            archive_path = output_dir / "DiveCANHead-v1.2.3.zip"
            self.assertTrue(archive_path.is_file())
            self.assertTrue(
                (output_dir / "DiveCANHead-v1.2.3-SHA256SUMS.txt").is_file()
            )
            checksums = (
                output_dir / "DiveCANHead-v1.2.3-SHA256SUMS.txt"
            ).read_text(encoding="utf-8")
            with zipfile.ZipFile(archive_path) as archive:
                names = set(archive.namelist())
            self.assertIn("DiveCANHead-v1.2.3/changelog.txt", names)
            self.assertIn("DiveCANHead-v1.2.3/SHA256SUMS", names)
            for variant in release.PRODUCTION_VARIANTS:
                self.assertIn(
                    f"DiveCANHead-v1.2.3/{variant}/qualification.json", names
                )
                variant_archive_path = (
                    output_dir / f"DiveCANHead-{variant}-v1.2.3.zip"
                )
                self.assertTrue(variant_archive_path.is_file())
                self.assertIn(variant_archive_path.name, checksums)
                variant_root = f"DiveCANHead-{variant}-v1.2.3"
                with zipfile.ZipFile(variant_archive_path) as variant_archive:
                    variant_names = set(variant_archive.namelist())
                self.assertIn(f"{variant_root}/changelog.txt", variant_names)
                self.assertIn(f"{variant_root}/SHA256SUMS", variant_names)
                self.assertIn(
                    f"{variant_root}/"
                    f"DiveCANHead-{variant}-v1.2.3-full.hex",
                    variant_names,
                )
                self.assertIn(
                    f"{variant_root}/DiveCANHead-{variant}-v1.2.3-ota.bin",
                    variant_names,
                )
                for other_variant in release.PRODUCTION_VARIANTS:
                    if other_variant != variant:
                        self.assertFalse(
                            any(other_variant in name for name in variant_names)
                        )

            tampered_variant = release.PRODUCTION_VARIANTS[0]
            tampered_dir = source_root / tampered_variant
            (
                tampered_dir
                / f"DiveCANHead-{tampered_variant}-v1.2.3-full.hex"
            ).write_bytes(b"tampered")
            with self.assertRaisesRegex(
                release.ReleaseError, "qualification hash mismatch"
            ):
                release._validate_qualified_variant(
                    tampered_dir,
                    "1.2.3",
                    self.SOURCE_COMMIT,
                    tampered_variant,
                )


if __name__ == "__main__":
    unittest.main()
