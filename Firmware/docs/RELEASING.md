# Numbered firmware releases

Numbered firmware releases are created only by the manually triggered
**Firmware Release (HIL qualified)** GitHub Actions workflow. The workflow runs
on the DiveCAN HIL runner, builds and fully qualifies every production variant,
bundles the exact tested images, and publishes the GitHub Release only after all
variants pass.

## Prepare a release

1. Update `Firmware/VERSION` to the intended SemVer release. Numeric components
   must be in the Zephyr-supported range 0 through 255 and
   `VERSION_TWEAK` must be zero for a numbered GitHub release.
2. Move the applicable entries from `Firmware/changelog.txt` under a unique
   numbered heading:

   ```text
   ## 1.2.3 - YYYY-MM-DD
   ```

3. Leave a new `## Unreleased` section at the top.
4. Merge those changes to `master` and wait for ordinary software CI to pass.
5. In GitHub, select **Actions → Firmware Release (HIL qualified) → Run
   workflow**, choose `master`, and enter the version without a leading `v`.

The workflow refuses non-`master` dispatches, an existing version tag, a
VERSION/changelog mismatch, an empty changelog section, or an incomplete
variant bundle.

## Release downloads

The GitHub Release provides one complete package and five smaller,
variant-specific packages:

- `DiveCANHead-v<version>.zip`: all production variants;
- `DiveCANHead-Poseidon_Aren-v<version>.zip`;
- `DiveCANHead-AP_Aren-v<version>.zip`;
- `DiveCANHead-AP_Paul-v<version>.zip`;
- `DiveCANHead-eCCR_classic-v<version>.zip`;
- `DiveCANHead-Sidewinder_Gabriel-v<version>.zip`.

Each variant ZIP is self-contained and contains:

- `*-full.hex`: MCUBoot plus the application for full SWD/factory flashing;
- `*-ota.bin`: the build's `zephyr.signed.bin` for UDS OTA;
- `test_manifest.json`: the topology used by the HIL suite;
- `qualification.json`: tested source/harness identity and file hashes;
- `release.json`, `README.txt`, `changelog.txt`, and `SHA256SUMS`.

`changelog.txt` is both the repository source for the GitHub Release
description and a file in every downloadable ZIP. Each ZIP has an internal
`SHA256SUMS`, and the release-level checksum file covers the complete ZIP plus
all five per-variant ZIPs.

## Failure behavior

The five HIL jobs are serialized because they share one physical bench. A
failure in any variant prevents the publish job from running. Successful
variant bundles and all HIL logs remain as workflow artifacts for diagnosis,
but no tag or GitHub Release is created until the complete matrix passes.
