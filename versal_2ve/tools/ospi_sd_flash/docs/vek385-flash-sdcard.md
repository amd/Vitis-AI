# vek385-flash-sdcard.sh -- SD Card Flash Tool

Automates UG1787 (SD Card Setup) for VEK385 Rev-B.

## Prerequisites

```bash
# bmaptool (required for flashing)
sudo apt install bmap-tools
```

**Hardware:**
- SD card (minimum 32GB) inserted into host machine card reader
- SD card physically removed from the VEK385 Evaluation Kit

**Directory structure:**

The following files must be in the **same directory** as `vek385-flash-sdcard.sh`. The script auto-detects and copies them to the SD card if present. These are NOT in the boot_images directory.

```
scripts/                            ← script directory
  vek385-flash-sdcard.sh            ← this script
  setup_overlay.sh                  ← (optional) on-target: fpgautil + config copy
  runtime_env.sh                    ← (optional) on-target: amdxdna module + env vars
  configure_ufs.sh                  ← (optional) on-target: UFS flash check/enable
  ufsconfig_64gb                    ← (optional) UFS configuration descriptor (64GB)
  vek385-setup.service              ← (optional) systemd service for auto-run on boot

boot_images/                        ← separate, passed via --boot-images
  rootfs.wic.xz                     ← rootfs image
  rootfs.wic.bmap                   ← bmap file for sparse flash
  overlay/                          ← PL/AI Engine overlay files
    vpl_gen_fixed_pld.pdi
    pl_aiarm.dtbo
    image_processing.cfg
    x_plus_ml.xclbin
```

If the optional scripts and service file are present, the SD card script will:
- Copy `setup_overlay.sh` and `runtime_env.sh` to `/overlay/` on rootfs
- Extract env var exports to `/etc/profile.d/vek385-runtime.sh`
- Copy `configure_ufs.sh` and `ufsconfig_64gb` to `/etc/ufs_config/` on rootfs
- Install `vek385-setup.service` to `/etc/systemd/system/` and enable it

If they are not present, the SD card is still flashed with the rootfs and overlay files -- but overlay programming, runtime setup, and UFS configuration must be done manually after boot.

## Usage

```
Usage: sudo ./vek385-flash-sdcard.sh --boot-images <path> [options]

Required:
  --boot-images <path>   Path to boot_images directory

Options:
  --sd-device <dev>      SD card device (e.g., /dev/sdc)
                         (default: auto-detect removable disk)
  --rootfs-image <file>  Rootfs image filename (default: auto-detect *.wic.xz)
  --models-dir <path>    Path to compiled models to copy to /home/models/
  --log-file <path>      Log file path (default: ./vek385-flash-sdcard_<timestamp>.log)
  --skip-flash           Skip flashing, only copy overlays/models
  --dry-run              Validate paths and settings without touching hardware
  --yes                  Skip confirmation prompts
  --help                 Show this help message
```

### Examples

```bash
# Dry run -- validate everything without writing to SD card
sudo ./vek385-flash-sdcard.sh \
  --boot-images /path/to/boot_images \
  --dry-run

# Flash and copy overlays
sudo ./vek385-flash-sdcard.sh \
  --boot-images /path/to/boot_images

# Flash with explicit device and models
sudo ./vek385-flash-sdcard.sh \
  --boot-images /path/to/boot_images \
  --sd-device /dev/sdc \
  --models-dir /path/to/compiled_models

# Re-provision overlays without re-flashing (faster)
sudo ./vek385-flash-sdcard.sh \
  --boot-images /path/to/boot_images \
  --skip-flash

# Non-interactive (skip confirmation)
sudo ./vek385-flash-sdcard.sh \
  --boot-images /path/to/boot_images \
  --yes
```

## How It Works

This script operates entirely on the host machine. The SD card is physically in the host's card reader -- there is **no board interaction, no JTAG, no serial**. The host writes directly to the SD card as a block device.

### Step-by-Step Flow

```
         Host (Linux)                              SD Card
         ────────────                              ───────
                        card reader
Step 1   detect ──────────────────────────→ /dev/sdc (32GB removable)

Step 3   umount /dev/sdc1,sdc2,sdc3
         fuser -k (kill processes holding device)

Step 4   bmaptool copy rootfs.wic.xz ────→ /dev/sdc
                                             ├── sdc1  FAT32  1GB (EFI boot)
                                             ├── sdc2  ext4   1GB (storage)
                                             └── sdc3  ext4   6GB (rootfs, auto-expands on boot)

Step 5   mount /dev/sdc3 → /mnt/vek385-sd/
         cp overlay/ ────────────────────→ /mnt/vek385-sd/overlay/
         cp setup_overlay.sh ────────────→ /mnt/vek385-sd/overlay/
         cp runtime_env.sh ─────────────→ /mnt/vek385-sd/overlay/
         install profile.d env vars ─────→ /mnt/vek385-sd/etc/profile.d/
         cp configure_ufs.sh ───────────→ /mnt/vek385-sd/etc/ufs_config/
         cp ufsconfig_64gb ─────────────→ /mnt/vek385-sd/etc/ufs_config/
         install vek385-setup.service ──→ /mnt/vek385-sd/etc/systemd/system/
         cp models/ ─────────────────────→ /mnt/vek385-sd/home/models/ (optional)

Step 6   sync → umount → eject
                                           SD card ready to insert into board
```

#### Pre-flight validation (before touching hardware)

1. Check running as root (`sudo`)
2. Validate `--boot-images` exists and contains `overlay/` directory
3. Auto-detect rootfs image by globbing `*.wic.xz` in boot_images, excluding UFS images (`*.ufs.*`)
4. Derive bmap filename automatically (`rootfs.wic.xz` → `rootfs.wic.bmap`)
5. Check `bmaptool` is installed
6. If `--dry-run`, print summary and exit

#### Step 1: Detect SD card device

Scans `lsblk` for candidate devices using three methods:

| Card Reader Type | Detection Method |
|---|---|
| External USB reader (RM=1) | `lsblk` removable flag |
| External USB reader (RM=0) | `TRAN=usb` + size < 128GB |
| Built-in laptop/desktop slot | Device name starts with `mmcblk` |

- **One candidate** → shows device info (size, model), asks user to confirm
- **Multiple candidates** → numbered list, user picks by number or types path
- **None found** → shows all non-loop block devices, asks user to type device path

Always requires user confirmation, even for a single candidate. Skippable with `--yes`.

Can be bypassed entirely with `--sd-device /dev/sdX`.

#### Step 2: Safety checks

- **Size check**: refuses devices larger than 128GB (not an SD card)
- **System disk check**: refuses if any partition on the device is mounted as `/`
- **User confirmation**: *"WARNING: All data on /dev/sdc will be destroyed. Continue? [y/N]"*

#### Step 3: Unmount existing partitions

Loops through all partitions on the device (`/dev/sdc1`, `/dev/sdc2`, ...) and unmounts any that are mounted. Runs `fuser -k` to kill any processes still holding the device (prevents "device busy" errors from auto-mount daemons).

#### Step 4: Flash rootfs to SD card

```bash
bmaptool copy rootfs.wic.xz /dev/sdc
```

`bmaptool` does three things in one step:
1. Decompresses `rootfs.wic.xz` on-the-fly (xz → raw 8GB image)
2. Uses `rootfs.wic.bmap` to skip empty blocks (only ~2.4GB of actual data is written)
3. Writes directly to `/dev/sdc`, creating the partition layout

If no `.bmap` file is found, falls back to `--nobmap` mode (writes all 8GB, slower).

The resulting partition layout:

| Partition | Device | Size | Type | Mount Point | Contents |
|-----------|--------|------|------|-------------|----------|
| EFI Boot | `/dev/sdc1` | 1 GB | FAT32 | `/efi` | Kernel, bootloader config |
| Storage | `/dev/sdc2` | 1 GB | ext4 | `/root/storage` | User data |
| Root FS | `/dev/sdc3` | 6 GB (expands to fill card on first boot) | ext4 | `/` | Linux OS, apps, libraries |

After flashing, runs `partprobe` and waits for the kernel to re-read the partition table. Reports elapsed time for the flash operation.

#### Step 5: Mount and copy

Finds the ext4 rootfs partition by scanning partitions for ext4 filesystem type. Mounts it and copies:

**From boot_images:**

| File | Destination | Purpose |
|------|-------------|---------|
| `overlay/vpl_gen_fixed_pld.pdi` | `/overlay/` | FPGA bitstream (PL configuration) |
| `overlay/pl_aiarm.dtbo` | `/overlay/` | Device tree overlay for AI Engine |
| `overlay/image_processing.cfg` | `/overlay/` | XRT image processing configuration |
| `overlay/x_plus_ml.xclbin` | `/overlay/` | XRT acceleration metadata |

**From scripts directory (if present):**

| File | Destination | Purpose |
|------|-------------|---------|
| `setup_overlay.sh` | `/overlay/` | On-target: fpgautil + config copy |
| `runtime_env.sh` | `/overlay/` | On-target: kernel module + env vars |
| (extracted exports) | `/etc/profile.d/vek385-runtime.sh` | Environment variables for user login shells |
| `configure_ufs.sh` | `/etc/ufs_config/` | On-target: UFS flash check/enable |
| `ufsconfig_64gb` | `/etc/ufs_config/` | UFS configuration descriptor (64GB) |
| `vek385-setup.service` | `/etc/systemd/system/` | Systemd service (auto-runs all board setup on boot) |

**Optional:**

| Source | Destination | Purpose |
|--------|-------------|---------|
| `--models-dir` contents | `/home/models/` | Compiled AI models |

#### Step 6: Finalize

```bash
sync          # flush all pending writes to SD card
umount        # unmount /mnt/vek385-sd
eject         # safe to physically remove
```

If `eject` fails (device busy), warns the user to wait and manually remove.

## After Completion

```
  1. Insert the SD card into the VEK385 evaluation kit
  2. Ensure SW1 DIP is set to OSPI mode (0001 = ON, ON, ON, OFF)
  3. Power on the board
  4. Board auto-configures on boot (no manual steps needed)
```

## What Happens On Boot

`vek385-setup.service` runs automatically as part of `multi-user.target` (after `local-fs.target`):

1. `/overlay/setup_overlay.sh` -- programs FPGA PL + AI Engine overlay, copies config files
2. `/overlay/runtime_env.sh` -- suppresses RCU stall warnings, loads `amdxdna` kernel module
3. `/etc/ufs_config/configure_ufs.sh` -- checks if UFS flash is enabled; if not, writes the configuration descriptor via `ufs-utils`. A reboot is required after first-time UFS enablement.

Environment variables (`LD_LIBRARY_PATH`, `XRT_*`, etc.) are set via `/etc/profile.d/vek385-runtime.sh`, which is sourced automatically on every user login (SSH or serial). No manual `source` needed.

**Verify after boot:**
```bash
systemctl status vek385-setup.service   # did all steps run?
echo $LD_LIBRARY_PATH                   # env vars set?
lsmod | grep amdxdna                    # module loaded?
cat /etc/ufs_config/configure_ufs.log   # UFS config result?
```

**Manual re-run (if needed):**
```bash
sudo systemctl restart vek385-setup.service
```

## Error Handling

| Scenario | Behavior |
|---|---|
| Not running as root | Exits immediately with hint |
| bmaptool not installed | Exits with `apt install` hint |
| Device > 128GB | Refuses to flash (not an SD card) |
| Device mounted as `/` | Refuses to flash (system disk) |
| bmaptool copy fails | Checks exit code (`$BMAP_RC`), points to log file |
| No partitions after flash | Exits with error |
| No ext4 partition found | Warning, falls back to last partition |
| Mount fails | Exits with error |
| Empty models directory | Warning, skips copy |
| Eject fails | Warning with "device may be busy" hint |
| Ctrl-C during execution | Cleanup trap unmounts and removes mount point |
| Failure after mount | EXIT trap unmounts automatically |
| Device busy during flash | `fuser -k` kills holding processes before flash |

## Log File

A timestamped log file is created in the current directory:

```
./vek385-flash-sdcard_20260410_143022.log
```

Contains partition detection, flash duration, and copy results. Override with `--log-file <path>`.

## Special Options

### `--skip-flash`

Skips the bmaptool flash step and jumps straight to mounting and copying overlays. Useful when:
- SD card was already flashed and you just need to update overlays or add models
- Re-provisioning after a model recompile

### `--yes`

Skips all confirmation prompts (device selection and destructive operation warning). Useful for scripted/automated workflows.

### `--dry-run`

Validates all paths, detects SD card, checks dependencies, then exits without touching hardware. Useful to verify setup before committing to a flash.
