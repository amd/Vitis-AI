# VEK385 Board Setup Service

## Overview

`vek385-setup.service` is a systemd service that automatically configures VEK385 Rev-B boards during boot. It runs multiple setup steps sequentially via `ExecStart` lines, with systemd handling ordering, error reporting, and logging.

Currently it runs three components:

1. **Overlay setup** -- programs the FPGA PL + AI Engine overlay
2. **Runtime environment** -- loads kernel modules and configures sysfs
3. **UFS configuration** -- checks and enables the onboard UFS flash

## Files

| File | Location on SD card | Purpose |
|---|---|---|
| `setup_overlay.sh` | `/overlay/` | Programs PL + AI Engine overlay via fpgautil, copies XRT config files |
| `runtime_env.sh` | `/overlay/` | Loads amdxdna kernel module, sets sysfs parameters |
| `configure_ufs.sh` | `/etc/ufs_config/` | Checks UFS state, writes config descriptor if not enabled |
| `ufsconfig_64gb` | `/etc/ufs_config/` | UFS configuration descriptor for 64GB flash |
| `vek385-setup.service` | `/etc/systemd/system/` | Systemd service that runs all setup steps on boot |
| `vek385-runtime.sh` | `/etc/profile.d/` | Environment variables for user login shells |

## Service File Explained

```ini
[Unit]
Description=VEK385 evaluation kit setup (overlay, runtime, UFS)
After=local-fs.target

[Service]
Type=oneshot
WorkingDirectory=/overlay
ExecStart=/overlay/setup_overlay.sh
ExecStart=/overlay/runtime_env.sh
ExecStart=/etc/ufs_config/configure_ufs.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

| Directive | Meaning |
|---|---|
| `After=local-fs.target` | Wait until all filesystems are mounted (ensures `/overlay/` and `/etc/ufs_config/` are available) |
| `Type=oneshot` | Run commands sequentially and exit (not a long-running daemon) |
| `WorkingDirectory=/overlay` | Set working directory for `setup_overlay.sh` and `runtime_env.sh` which use relative paths |
| `ExecStart=setup_overlay.sh` | Programs FPGA PL + AI Engine overlay, copies XRT config files |
| `ExecStart=runtime_env.sh` | Suppresses RCU stall warnings, loads amdxdna kernel module |
| `ExecStart=configure_ufs.sh` | Checks/enables UFS flash (uses absolute path since it lives outside `/overlay/`) |
| `RemainAfterExit=yes` | Keep service marked as "active" after scripts finish (for status reporting) |
| `WantedBy=multi-user.target` | Run as part of normal multi-user boot (before login prompt appears) |

If any `ExecStart` step fails, systemd stops execution and marks the service as failed. `systemctl status` shows which step failed and its exit code.

## How It Gets Installed

The `vek385-flash-sdcard.sh` script installs everything when preparing the SD card:

1. **Copies overlay scripts** (`setup_overlay.sh`, `runtime_env.sh`) to `/overlay/`
2. **Extracts env var exports** to `/etc/profile.d/vek385-runtime.sh`
3. **Copies UFS config files** (`configure_ufs.sh`, `ufsconfig_64gb`) to `/etc/ufs_config/`
4. **Copies the service file** to `/etc/systemd/system/`
5. **Creates an enable symlink** at `/etc/systemd/system/multi-user.target.wants/vek385-setup.service`

The symlink is equivalent to running `systemctl enable` on the target. Since the SD card is being prepared on the host (not the board), the symlink is created manually by the script.

### Directory structure on SD card after flashing

```
/etc/systemd/system/
  vek385-setup.service                                    <- service file

/etc/systemd/system/multi-user.target.wants/
  vek385-setup.service -> /etc/systemd/system/vek385-setup.service  <- enable symlink

/etc/profile.d/
  vek385-runtime.sh                                       <- export lines extracted from runtime_env.sh

/etc/ufs_config/
  configure_ufs.sh                                        <- UFS flash check/enable
  ufsconfig_64gb                                          <- UFS configuration descriptor

/overlay/
  setup_overlay.sh                                        <- fpgautil + config copy
  runtime_env.sh                                          <- module load + sysfs config
  vpl_gen_fixed_pld.pdi                                   <- FPGA bitstream
  pl_aiarm.dtbo                                           <- device tree overlay
  image_processing.cfg                                    <- XRT config
  x_plus_ml.xclbin                                       <- XRT metadata
```

## Boot Sequence

```
Board powers on
  -> OSPI boots (PLM -> TF-A -> U-Boot)
  -> U-Boot loads Linux from SD card
  -> systemd starts
  -> local-fs.target reached (filesystems mounted)
  -> multi-user.target pulls in vek385-setup.service
    -> /overlay/setup_overlay.sh runs
      -> fpgautil programs PL + AI Engine overlay
      -> copies image_processing.cfg and x_plus_ml.xclbin to /run/media/mmcblk0p1/
    -> /overlay/runtime_env.sh runs
      -> suppresses RCU CPU stall warnings
      -> loads amdxdna kernel module with enable_polling=0
    -> /etc/ufs_config/configure_ufs.sh runs
      -> reads UFS configuration descriptor via ufs-utils
      -> if UFS enabled: logs status, no action needed
      -> if UFS not enabled: writes ufsconfig_64gb descriptor (reboot required)
  -> login prompt appears
  -> user logs in
    -> /etc/profile.d/vek385-runtime.sh is sourced automatically
    -> LD_LIBRARY_PATH, XRT_*, DEBUG_* env vars are set
  -> board ready for AI/ML applications
```

## Component Details

### setup_overlay.sh

Programs the FPGA PL and AI Engine overlay using `fpgautil`, then copies XRT configuration files (`image_processing.cfg`, `x_plus_ml.xclbin`) to the expected runtime location.

### runtime_env.sh

Suppresses RCU CPU stall warnings via sysfs, then loads the `amdxdna` kernel module with `enable_polling=0`. Environment variable exports in this script are not used by systemd -- they are extracted to `/etc/profile.d/vek385-runtime.sh` for user login shells.

### configure_ufs.sh

Checks whether the onboard UFS flash is enabled by reading the UFS configuration descriptor via `ufs-utils`. If LU0 is already enabled, logs the current allocation sizes and exits. If not enabled, writes the `ufsconfig_64gb` descriptor to enable it. A reboot is required after first-time enablement.

All output is logged to `/etc/ufs_config/configure_ufs.log` (overwritten on each boot).

## Environment Variables

The following environment variables are extracted from `runtime_env.sh` and installed to `/etc/profile.d/vek385-runtime.sh` so they are available in every user login shell automatically:

```bash
export LD_LIBRARY_PATH=/usr/lib/python3.12/site-packages/flexmlrt/lib/:/usr/lib/python3.12/site-packages/voe/lib/
export XLNX_ENABLE_CACHE=0
export DEBUG_VAIML_PARTITION=0
export XRT_ELF_FLOW=1
export XRT_AIARM=true
export DEBUG_VAIML_PARTITION=2
```

These are sourced automatically on login -- no manual `source` command needed.

## Verifying After Boot

```bash
# Check service ran successfully (shows all ExecStart steps)
systemctl status vek385-setup.service

# Expected output:
#   Active: active (exited)
#   Process: setup_overlay.sh (code=exited, status=0/SUCCESS)
#   Process: runtime_env.sh (code=exited, status=0/SUCCESS)
#   Process: configure_ufs.sh (code=exited, status=0/SUCCESS)

# Verify module loaded
lsmod | grep amdxdna

# Verify environment variables
echo $LD_LIBRARY_PATH
echo $XRT_ELF_FLOW

# Check UFS config log
cat /etc/ufs_config/configure_ufs.log
```

## Manual Re-run

```bash
# Re-run all steps via systemd
sudo systemctl restart vek385-setup.service

# Or run individual scripts directly
cd /overlay
sudo ./setup_overlay.sh
source ./runtime_env.sh

sudo /etc/ufs_config/configure_ufs.sh
cat /etc/ufs_config/configure_ufs.log
```

Note: `source` is needed for `runtime_env.sh` when running manually so the environment variables are set in your current shell. When systemd runs it, the env vars only apply to that service process -- user shells get them via `/etc/profile.d/vek385-runtime.sh` instead.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Service shows `inactive (dead)` | Service was loaded but never triggered | Check `After=` directive and enable symlink |
| Service shows `failed` | One of the ExecStart scripts failed | Run `journalctl -u vek385-setup.service` to see which step failed |
| "Overlay already exists" warning | fpgautil overlay already programmed from a previous run | Harmless on re-run. Use `sudo fpgautil -R -n full` to remove first if needed |
| Env vars not set after login | `/etc/profile.d/vek385-runtime.sh` missing | Re-run `vek385-flash-sdcard.sh` with `--skip-flash` to re-install |
| amdxdna module not loaded | `runtime_env.sh` failed | Check `dmesg | grep amdxdna` for driver errors |
| `ufs-utils not found on PATH` | Tool not in rootfs | Verify Yocto image includes `ufs-utils` package |
| UFS BSG device not found | UFS hardware not detected by kernel | Check `dmesg | grep ufs` for driver errors |
| Could not parse bLUEnable | `ufs-utils` output format changed | Check `/etc/ufs_config/configure_ufs.log` for raw output |
| UFS enabled but not usable | Config was just written on this boot | Reboot required for UFS controller to apply new configuration |

## Adding New Components

To add a new setup step to the service:

1. Create the script and place it in the appropriate location on the SD card
2. Add a new `ExecStart=` line to `vek385-setup.service` (order matters -- scripts run sequentially)
3. Update `vek385-flash-sdcard.sh` to copy the new script during SD card preparation
4. If the script needs files, create a directory (e.g., `/etc/<component>/`) and copy them in the SD card script

## Prerequisites

The following files must be in the **same directory as `vek385-flash-sdcard.sh`** (not in boot_images):

- `setup_overlay.sh`
- `runtime_env.sh`
- `vek385-setup.service`
- `configure_ufs.sh`
- `ufsconfig_64gb`
