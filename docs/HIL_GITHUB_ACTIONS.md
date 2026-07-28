# HIL GitHub Actions Gate

The full hardware-in-the-loop gate runs the firmware from a pull request on the
DiveCAN test bench. It builds the selected Zephyr variant, flashes the DUT, and
then runs the out-of-tree Test Rig pytest suite against that exact build's
generated manifest and OTA image.

The trusted HIL harness has two Git roots:

- the parent Test Rig repo, which owns the HAL, DUT client, manifest handling,
  flashing wrapper, Docker launchers, and hardware tooling;
- the nested `Test Rig/tests` repo, which owns `conftest.py`, test policy, and
  the `test_*.py` assertions.

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
runner or runner group to this repository. The runner service should be a
dedicated local user, for example `gha-divecan`, that can run Docker but does not
have direct access to the rig's toolchain or harness except through the mounts
listed below.

The workflow executes the firmware build, flashing, and pytest run inside a
trusted Docker image. The firmware repository workflow only checks out the PR
and invokes a trusted host-side launcher from the out-of-tree Test Rig checkout.
That launcher owns the Docker image, device allow-list, bind mounts, and trusted
in-container script path. The host runner does not run Zephyr, CMake, pytest,
OpenOCD, or PR-controlled build scripts directly on the host.

Membership in the host `docker` group is effectively host-root authority. The
preferred setup is to keep the runner user out of `docker` and expose only a
root-owned copy of the trusted launcher through sudoers. The launcher hard-codes
the image, trusted host paths, device allow-list, and bind mounts rather than
accepting them from PR-controlled workflow YAML.

On this rig the host-side workflow defaults to:

- the Zephyr SDK at `/home/aren/zephyr-sdk-1.0.1`;
- the out-of-tree Test Rig checkout at
  `/home/aren/Nextcloud/Clients/Internal/CCR_Electronics/Test Rig`;
- CellSimulator tools at
  `/home/aren/Nextcloud/Clients/Internal/CellSimulator/Firmware/tools`;
- Riden PSU tools at
  `/home/aren/Nextcloud/AI/Claude Skills/riden-psu/scripts`;
- hardware access to the RD6006P supplies, relay bank, CellSimulators, ADS1115s,
  SocketCAN interface, Pico emulator, and ST-Link.

For a hardened setup, mirror the trusted host resources under a neutral location
such as `/opt/divecan/`, make them root-owned, and expose them read-only to the
runner user. Put those host paths in `/etc/divecan-hil-runner.conf`, not in
repository variables.

The trusted Docker image defaults to `divecan-hil-runner:latest`. Override it
with `image="..."` in `/etc/divecan-hil-runner.conf` if the local image is
versioned or pulled from a registry. The image must provide:

- `/opt/divecan/zephyr-venv/bin/west`;
- `/opt/divecan/rig-venv/bin/pytest`;
- Python dependencies for the rig package: pytest, python-can, smbus2, pyserial,
  lgpio, flask, and optional bleak;
- Zephyr Python dependencies normally installed by `west packages pip --install`;
- Zephyr host build dependencies such as git, CMake, Ninja, devicetree compiler,
  and compiler support packages;
- shell tools used by the workflow: bash, find, grep, tee.

On this rig the Zephyr SDK is mounted inside the container at its original host
path, `/home/aren/zephyr-sdk-1.0.1`, because SDK host tools such as `dtc` embed
that absolute interpreter path. Keep that path stable or rebuild/patch the SDK
host tools before changing the container mount point.

The in-container script skips `west packages pip --install` by default because
the container root filesystem is read-only. Set `run_west_pip_install="1"` in
`/etc/divecan-hil-runner.conf` only when using a writable throwaway image during
image bring-up.

The Test Rig repo contains a trusted image definition at
`tools/Dockerfile.hil-runner`; build it on the bench with:

```bash
cd "/home/aren/Nextcloud/Clients/Internal/CCR_Electronics/Test Rig"
tools/build_hil_runner_image.sh divecan-hil-runner:latest
```

The Test Rig repo also contains the trusted host-side launcher at
`tools/run_hil_host_container.sh`. For the stronger no-`docker`-group model,
install that launcher as root-owned:

```bash
sudo install -m 0755 \
  "/home/aren/Nextcloud/Clients/Internal/CCR_Electronics/Test Rig/tools/run_hil_host_container.sh" \
  /usr/local/sbin/divecan-hil-runner
```

If the runner workspace is in a predictable root such as
`/home/gha-divecan/actions-runner/_work`, add that root to the launcher config:

```bash
sudo install -m 0644 /dev/stdin /etc/divecan-hil-runner.conf <<'EOF'
allowed_workspace_root="/home/gha-divecan/actions-runner/_work"
EOF
```

The same config file may override trusted host paths when they are mirrored
under `/opt/divecan/`:

```bash
host_zephyr_sdk="/opt/divecan/zephyr-sdk-1.0.1"
host_test_rig_dir="/opt/divecan/test-rig"
host_cellsim_tools_dir="/opt/divecan/cellsim-tools"
host_riden_psu_dir="/opt/divecan/riden-psu/scripts"
image="divecan-hil-runner:latest"
```

Then allow only that command through sudo for the runner user:

```bash
echo 'gha-divecan ALL=(root) NOPASSWD: /usr/local/sbin/divecan-hil-runner' |
  sudo tee /etc/sudoers.d/divecan-hil-runner
sudo chmod 0440 /etc/sudoers.d/divecan-hil-runner
```

Set the repository variable `DIVECAN_HIL_CONTAINER_RUNNER` to
`/usr/local/sbin/divecan-hil-runner` and
`DIVECAN_HIL_CONTAINER_RUNNER_USE_SUDO=1` for the sudo-wrapper model. If the
runner user is temporarily placed in the `docker` group during bring-up, leave
both variables unset and the workflow will call the Test Rig copy directly.

The workflow mounts the Test Rig, SDK, CellSimulator tools, and PSU tools
read-only. It mounts the PR workspace and runner temp directory read-write. It
sets `PYTHONDONTWRITEBYTECODE=1`, moves pytest's cache under the runner temp
directory, and sets `DIVECAN_RIG_RUNDIR` outside the harness tree so the Test Rig
checkout can stay read-only during the run.

By default the workflow fails if the Test Rig checkout is dirty, so a required
status can be traced to committed harness versions. Because `Test Rig/tests` is
a separate Git repo ignored by the parent checkout, the trusted launcher checks
both the parent Test Rig repo and the nested tests repo and logs both commits.
Set `allow_dirty_harness="1"` in `/etc/divecan-hil-runner.conf` only for
deliberate bring-up runs where that reproducibility guard is too strict.

## Branch protection

Make the `Build, flash, and run full HIL` job a required status check for the
protected branch. Also protect `.github/workflows/**` through CODEOWNERS or an
equivalent ruleset so workflow changes cannot silently alter what runs on the
bench.

The workflow uses the full HIL suite by default, including OTA swap and recovery.
Shortened runs should stay manual/ad-hoc and should not satisfy the protected
branch gate.

## Docker isolation model

The HIL job starts Docker with a limited hardware surface:

- a read-only bind mount of the Test Rig checkout;
- a writable mount for the firmware checkout and build directory;
- read-only bind mounts for trusted tool sources and the Zephyr SDK;
- `--network host` so SocketCAN `can0` is visible;
- explicit device passthrough for `/dev/gpiochip*`, `/dev/i2c-1`, known PSU
  UARTs, Pico `/dev/ttyACM*`, and USB bus nodes for ST-Link/OpenOCD;
- `--cap-drop ALL --cap-add NET_RAW`;
- `--security-opt no-new-privileges`;
- a read-only container root filesystem with writable tmpfs mounts for `/tmp`
  and `/run`.

Do not mount the Docker socket into the container. Avoid `--privileged`; if it is
needed during bring-up, treat that as a temporary diagnostic mode and do not use
the result as the protected branch gate.
