#!/usr/bin/env python3
"""Validate, stage, and bundle numbered DiveCANHead firmware releases.

This script is intentionally standard-library only. GitHub Actions runs it on
the HIL runner to keep the release contract testable outside workflow YAML:

* VERSION is the product/MCUboot image version.
* changelog.txt is the sole source for the GitHub Release description.
* Every production variant contributes the full merged hex, signed OTA image,
  generated HIL manifest, and a qualification record.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from datetime import date
from pathlib import Path

PRODUCTION_VARIANTS = (
    "Poseidon_Aren",
    "AP_Aren",
    "AP_Paul",
    "eCCR_classic",
    "Sidewinder_Gabriel",
)

_VERSION_KEYS = (
    "VERSION_MAJOR",
    "VERSION_MINOR",
    "PATCHLEVEL",
    "VERSION_TWEAK",
    "EXTRAVERSION",
)
_SEMVER_RE = re.compile(
    r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-([0-9a-z]+(?:[.-][0-9a-z]+)*))?"
)
_CHANGELOG_RELEASE_RE = re.compile(
    r"^## "
    r"((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
    r"(?:-[0-9a-z]+(?:[.-][0-9a-z]+)*)?)"
    r" - ([0-9]{4}-[0-9]{2}-[0-9]{2})$"
)
_GIT_COMMIT_RE = re.compile(r"[0-9a-f]{40}")


class ReleaseError(ValueError):
    """A release input or artifact violates the release contract."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _parse_semver(raw: str) -> tuple[int, int, int, str]:
    match = _SEMVER_RE.fullmatch(raw)
    if match is None:
        raise ReleaseError(
            f"version must be SemVer without a leading 'v': {raw!r}"
        )
    major, minor, patch = (int(match.group(index)) for index in range(1, 4))
    if any(component > 255 for component in (major, minor, patch)):
        raise ReleaseError(
            "Zephyr VERSION numeric components must each be in the range 0..255"
        )
    return major, minor, patch, match.group(4) or ""


def _validate_commit(raw: str, label: str) -> str:
    if _GIT_COMMIT_RE.fullmatch(raw) is None:
        raise ReleaseError(f"{label} must be a full lowercase 40-character Git SHA")
    return raw


def read_version_file(path: Path) -> str:
    values: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if "=" not in stripped:
            raise ReleaseError(f"{path}:{line_number}: expected KEY = VALUE")
        key, value = (part.strip() for part in stripped.split("=", 1))
        if key not in _VERSION_KEYS:
            raise ReleaseError(f"{path}:{line_number}: unknown VERSION field {key!r}")
        if key in values:
            raise ReleaseError(f"{path}:{line_number}: duplicate VERSION field {key!r}")
        values[key] = value

    missing = [key for key in _VERSION_KEYS if key not in values]
    if missing:
        raise ReleaseError(f"{path}: missing fields: {', '.join(missing)}")

    try:
        major = int(values["VERSION_MAJOR"])
        minor = int(values["VERSION_MINOR"])
        patch = int(values["PATCHLEVEL"])
        tweak = int(values["VERSION_TWEAK"])
    except ValueError as error:
        raise ReleaseError(f"{path}: VERSION numeric fields must be integers") from error

    if tweak != 0:
        raise ReleaseError(
            f"{path}: VERSION_TWEAK must be 0 for numbered GitHub releases"
        )

    extra = values["EXTRAVERSION"]
    version = f"{major}.{minor}.{patch}"
    if extra:
        version = f"{version}-{extra}"
    parsed_major, parsed_minor, parsed_patch, parsed_extra = _parse_semver(version)
    if (major, minor, patch, extra) != (
        parsed_major,
        parsed_minor,
        parsed_patch,
        parsed_extra,
    ):
        raise ReleaseError(f"{path}: VERSION fields do not form canonical SemVer")
    return version


def read_changelog_release(path: Path, version: str) -> str:
    _parse_semver(version)
    lines = path.read_text(encoding="utf-8").splitlines()
    if "## Unreleased" not in lines:
        raise ReleaseError(f"{path}: missing '## Unreleased' section")

    matches: list[tuple[int, str]] = []
    for index, line in enumerate(lines):
        match = _CHANGELOG_RELEASE_RE.fullmatch(line)
        if match is not None and match.group(1) == version:
            try:
                date.fromisoformat(match.group(2))
            except ValueError as error:
                raise ReleaseError(
                    f"{path}:{index + 1}: invalid release date {match.group(2)!r}"
                ) from error
            matches.append((index, match.group(2)))

    if not matches:
        raise ReleaseError(
            f"{path}: missing release heading '## {version} - YYYY-MM-DD'"
        )
    if len(matches) != 1:
        raise ReleaseError(f"{path}: release {version} appears more than once")

    start = matches[0][0] + 1
    end = len(lines)
    for index in range(start, len(lines)):
        if lines[index].startswith("## "):
            end = index
            break

    body = "\n".join(lines[start:end]).strip()
    if not any(line.lstrip().startswith("- ") for line in lines[start:end]):
        raise ReleaseError(f"{path}: release {version} contains no changelog entries")
    return f"{body}\n"


def validate_release(version: str, version_file: Path, changelog: Path) -> str:
    _parse_semver(version)
    file_version = read_version_file(version_file)
    if file_version != version:
        raise ReleaseError(
            f"requested version {version} does not match {version_file} ({file_version})"
        )
    read_changelog_release(changelog, version)
    return file_version


def _single_file(paths: list[Path], description: str) -> Path:
    if len(paths) != 1:
        rendered = ", ".join(str(path) for path in paths) or "none"
        raise ReleaseError(f"expected exactly one {description}; found {rendered}")
    if paths[0].stat().st_size == 0:
        raise ReleaseError(f"{description} is empty: {paths[0]}")
    return paths[0]


def stage_variant(args: argparse.Namespace) -> None:
    _parse_semver(args.version)
    _validate_commit(args.commit, "source commit")
    _validate_commit(args.harness_commit, "HIL harness commit")
    if args.variant not in PRODUCTION_VARIANTS:
        raise ReleaseError(f"not a production variant: {args.variant}")

    build_dir = args.build_dir.resolve(strict=True)
    full_image = _single_file(
        sorted(build_dir.glob("merged_*.hex")), "merged full-flash image"
    )
    zephyr_dir = build_dir / "Firmware" / "zephyr"
    ota_image = _single_file([zephyr_dir / "zephyr.signed.bin"], "signed OTA image")
    manifest_path = _single_file(
        [zephyr_dir / "test_manifest.json"], "HIL test manifest"
    )

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("variant") != args.variant:
        raise ReleaseError(
            f"manifest variant {manifest.get('variant')!r} != {args.variant!r}"
        )
    if manifest.get("version") != args.version:
        raise ReleaseError(
            f"manifest version {manifest.get('version')!r} != {args.version!r}"
        )
    if manifest.get("commit") != args.commit:
        raise ReleaseError(
            f"manifest commit {manifest.get('commit')!r} != {args.commit!r}"
        )

    variant_dir = args.output_root / args.variant
    variant_dir.mkdir(parents=True, exist_ok=False)
    full_name = f"DiveCANHead-{args.variant}-v{args.version}-full.hex"
    ota_name = f"DiveCANHead-{args.variant}-v{args.version}-ota.bin"
    shutil.copy2(full_image, variant_dir / full_name)
    shutil.copy2(ota_image, variant_dir / ota_name)
    shutil.copy2(manifest_path, variant_dir / "test_manifest.json")

    files = {
        full_name: _sha256(variant_dir / full_name),
        ota_name: _sha256(variant_dir / ota_name),
        "test_manifest.json": _sha256(variant_dir / "test_manifest.json"),
    }
    qualification = {
        "schema": 1,
        "result": "passed",
        "version": args.version,
        "tag": f"v{args.version}",
        "commit": args.commit,
        "variant": args.variant,
        "github_run_url": args.run_url,
        "harness_commit": args.harness_commit,
        "files": files,
    }
    (variant_dir / "qualification.json").write_text(
        json.dumps(qualification, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _validate_qualified_variant(
    variant_dir: Path, version: str, commit: str, variant: str
) -> dict:
    full_name = f"DiveCANHead-{variant}-v{version}-full.hex"
    ota_name = f"DiveCANHead-{variant}-v{version}-ota.bin"
    payload_names = {full_name, ota_name, "test_manifest.json"}
    expected_names = payload_names | {"qualification.json"}

    if not variant_dir.is_dir():
        raise ReleaseError(f"missing qualified variant directory: {variant_dir}")
    if any(path.is_dir() for path in variant_dir.iterdir()):
        raise ReleaseError(f"qualified variant directory must be flat: {variant_dir}")
    actual_names = {path.name for path in variant_dir.iterdir() if path.is_file()}
    if actual_names != expected_names:
        raise ReleaseError(
            f"{variant}: expected files {sorted(expected_names)}; "
            f"found {sorted(actual_names)}"
        )

    qualification = json.loads(
        (variant_dir / "qualification.json").read_text(encoding="utf-8")
    )
    if (
        qualification.get("result") != "passed"
        or qualification.get("version") != version
        or qualification.get("commit") != commit
        or qualification.get("variant") != variant
    ):
        raise ReleaseError(f"invalid qualification record for {variant}")
    _validate_commit(
        str(qualification.get("harness_commit", "")),
        f"{variant} HIL harness commit",
    )

    recorded_hashes = qualification.get("files")
    if not isinstance(recorded_hashes, dict) or set(recorded_hashes) != payload_names:
        raise ReleaseError(f"invalid qualification file list for {variant}")
    for name in sorted(payload_names):
        actual_hash = _sha256(variant_dir / name)
        if recorded_hashes[name] != actual_hash:
            raise ReleaseError(f"qualification hash mismatch for {variant}/{name}")

    manifest = json.loads(
        (variant_dir / "test_manifest.json").read_text(encoding="utf-8")
    )
    if (
        manifest.get("variant") != variant
        or manifest.get("version") != version
        or manifest.get("commit") != commit
    ):
        raise ReleaseError(f"staged manifest identity mismatch for {variant}")
    return qualification


def _package_readme(version: str) -> str:
    variants = "\n".join(f"- {variant}" for variant in PRODUCTION_VARIANTS)
    return f"""DiveCANHead Firmware v{version}
================================

This package contains the exact per-variant images that passed the manually
triggered GitHub Actions HIL release workflow.

For each variant:

- *-full.hex is the complete MCUBoot + application image for SWD/factory flash.
- *-ota.bin is the Zephyr zephyr.signed.bin image for UDS OTA.
- test_manifest.json is the build topology used to select and configure HIL.
- qualification.json identifies the tested commit, workflow, harness, and hashes.

Production variants:

{variants}

Do not interchange images between variants. Verify SHA256SUMS before use.
"""


def _variant_package_readme(version: str, variant: str) -> str:
    return f"""DiveCANHead {variant} Firmware v{version}
{'=' * (29 + len(variant) + len(version))}

This package contains the exact {variant} images that passed the manually
triggered GitHub Actions HIL release workflow.

- DiveCANHead-{variant}-v{version}-full.hex is the complete MCUBoot +
  application image for SWD/factory flash.
- DiveCANHead-{variant}-v{version}-ota.bin is the Zephyr zephyr.signed.bin
  image for UDS OTA.
- test_manifest.json is the build topology used to select and configure HIL.
- qualification.json identifies the tested source commit, workflow, HIL
  harness, and hashes.

This package is only for the {variant} hardware variant. Verify SHA256SUMS
before use.
"""


def _write_package_checksums(package_dir: Path) -> None:
    checksum_path = package_dir / "SHA256SUMS"
    entries = []
    for path in sorted(package_dir.rglob("*")):
        if path.is_file() and path != checksum_path:
            relative = path.relative_to(package_dir).as_posix()
            entries.append(f"{_sha256(path)}  {relative}")
    checksum_path.write_text("\n".join(entries) + "\n", encoding="utf-8")


def _write_deterministic_zip(package_dir: Path, zip_path: Path) -> None:
    with zipfile.ZipFile(
        zip_path, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for path in sorted(package_dir.rglob("*")):
            if not path.is_file():
                continue
            relative = path.relative_to(package_dir.parent).as_posix()
            info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def bundle_release(args: argparse.Namespace) -> None:
    validate_release(args.version, args.version_file, args.changelog)
    _validate_commit(args.commit, "source commit")
    source_root = args.source_root.resolve(strict=True)
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=False)
    package_dir = output_dir / f"DiveCANHead-v{args.version}"
    package_dir.mkdir()

    qualifications = {}
    source_variants = {}
    for variant in PRODUCTION_VARIANTS:
        source_variant = source_root / variant
        qualification = _validate_qualified_variant(
            source_variant, args.version, args.commit, variant
        )
        source_variants[variant] = source_variant
        qualifications[variant] = qualification

    for variant in PRODUCTION_VARIANTS:
        source_variant = source_variants[variant]
        destination = package_dir / variant
        shutil.copytree(source_variant, destination)

    shutil.copy2(args.changelog, package_dir / "changelog.txt")
    (package_dir / "README.txt").write_text(
        _package_readme(args.version), encoding="utf-8"
    )
    release_manifest = {
        "schema": 1,
        "version": args.version,
        "tag": f"v{args.version}",
        "commit": args.commit,
        "github_run_url": args.run_url,
        "variants": list(PRODUCTION_VARIANTS),
        "qualification": qualifications,
    }
    (package_dir / "release.json").write_text(
        json.dumps(release_manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    _write_package_checksums(package_dir)

    zip_paths = []
    complete_zip_path = output_dir / f"DiveCANHead-v{args.version}.zip"
    _write_deterministic_zip(package_dir, complete_zip_path)
    zip_paths.append(complete_zip_path)

    for variant in PRODUCTION_VARIANTS:
        variant_package_dir = output_dir / f"DiveCANHead-{variant}-v{args.version}"
        variant_package_dir.mkdir()
        for source_path in sorted(source_variants[variant].iterdir()):
            shutil.copy2(source_path, variant_package_dir / source_path.name)
        shutil.copy2(args.changelog, variant_package_dir / "changelog.txt")
        (variant_package_dir / "README.txt").write_text(
            _variant_package_readme(args.version, variant), encoding="utf-8"
        )
        variant_manifest = {
            "schema": 1,
            "version": args.version,
            "tag": f"v{args.version}",
            "commit": args.commit,
            "github_run_url": args.run_url,
            "variant": variant,
            "qualification": qualifications[variant],
        }
        (variant_package_dir / "release.json").write_text(
            json.dumps(variant_manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        _write_package_checksums(variant_package_dir)
        variant_zip_path = output_dir / f"{variant_package_dir.name}.zip"
        _write_deterministic_zip(variant_package_dir, variant_zip_path)
        zip_paths.append(variant_zip_path)

    (output_dir / f"DiveCANHead-v{args.version}-SHA256SUMS.txt").write_text(
        "".join(f"{_sha256(path)}  {path.name}\n" for path in zip_paths),
        encoding="utf-8",
    )
    shutil.copy2(args.changelog, output_dir / "changelog.txt")
    (output_dir / "release-notes.txt").write_text(
        read_changelog_release(args.changelog, args.version), encoding="utf-8"
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--version", required=True)
    validate_parser.add_argument("--version-file", required=True, type=Path)
    validate_parser.add_argument("--changelog", required=True, type=Path)

    extract_parser = subparsers.add_parser("extract")
    extract_parser.add_argument("--version", required=True)
    extract_parser.add_argument("--changelog", required=True, type=Path)
    extract_parser.add_argument("--output", required=True, type=Path)

    stage_parser = subparsers.add_parser("stage-variant")
    stage_parser.add_argument("--version", required=True)
    stage_parser.add_argument("--variant", required=True)
    stage_parser.add_argument("--commit", required=True)
    stage_parser.add_argument("--build-dir", required=True, type=Path)
    stage_parser.add_argument("--output-root", required=True, type=Path)
    stage_parser.add_argument("--run-url", required=True)
    stage_parser.add_argument("--harness-commit", required=True)

    bundle_parser = subparsers.add_parser("bundle")
    bundle_parser.add_argument("--version", required=True)
    bundle_parser.add_argument("--commit", required=True)
    bundle_parser.add_argument("--source-root", required=True, type=Path)
    bundle_parser.add_argument("--output-dir", required=True, type=Path)
    bundle_parser.add_argument("--version-file", required=True, type=Path)
    bundle_parser.add_argument("--changelog", required=True, type=Path)
    bundle_parser.add_argument("--run-url", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "validate":
            version = validate_release(args.version, args.version_file, args.changelog)
            print(f"release inputs valid: v{version}")
        elif args.command == "extract":
            args.output.write_text(
                read_changelog_release(args.changelog, args.version),
                encoding="utf-8",
            )
        elif args.command == "stage-variant":
            stage_variant(args)
        elif args.command == "bundle":
            bundle_release(args)
        else:
            parser.error(f"unsupported command: {args.command}")
    except (OSError, ReleaseError, json.JSONDecodeError) as error:
        print(f"release error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
