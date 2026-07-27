# HIL GitHub Actions Gate

The full hardware-in-the-loop gate runs the firmware from a pull request on the
DiveCAN test bench. It builds the selected Zephyr variant, flashes the DUT, and
then runs the out-of-tree Test Rig pytest suite against that exact build's
generated manifest and OTA image.

## Security model

Use a normal `pull_request` workflow. Do not use `pull_request_target` for this
job, because the job intentionally builds and executes pull-request-controlled
firmware on a self-hosted runner.

The repository's GitHub Actions settings are the approval boundary:

- Public repository: set **Approval for running fork pull request workflows from
  contributors** to **Require approval for all external contributors**.
- Private repository with forks enabled: enable **Require approval for fork pull
  request workflows**.

With those settings, an external fork PR cannot start the self-hosted HIL job
until a maintainer explicitly approves the workflow run.

## Runner requirements

Register the bench runner with the custom label `divecan-hil` and restrict that
runner or runner group to this repository. The runner must have:

- the Zephyr virtualenv at `/home/aren/zephyr-venv`;
- the Zephyr SDK at `/home/aren/zephyr-sdk-1.0.1`;
- the out-of-tree Test Rig checkout at
  `/home/aren/Nextcloud/Clients/Internal/CCR_Electronics/Test Rig`;
- hardware access to the RD6006P supplies, relay bank, CellSimulators, ADS1115s,
  SocketCAN interface, Pico emulator, and ST-Link.

By default the workflow fails if the Test Rig checkout is dirty, so a required
status can be traced to a committed harness version. Set the repository variable
`DIVECAN_HIL_ALLOW_DIRTY_HARNESS=1` only for deliberate bring-up runs where that
reproducibility guard is too strict.

## Branch protection

Make the `Build, flash, and run full HIL` job a required status check for the
protected branch. Also protect `.github/workflows/**` through CODEOWNERS or an
equivalent ruleset so workflow changes cannot silently alter what runs on the
bench.

The workflow uses the full HIL suite by default, including OTA swap and recovery.
Shortened runs should stay manual/ad-hoc and should not satisfy the protected
branch gate.
