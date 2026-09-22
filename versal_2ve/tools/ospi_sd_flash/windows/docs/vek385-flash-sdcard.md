# vek385-flash-sdcard.ps1 -- SD Card Flash Tool (Windows)

Automates UG1787 (SD Card Setup) for VEK385 Rev-A/Rev-B on Windows 11.
All operations are native Windows -- no WSL2, no usbipd, no third-party tools.

## Prerequisites

**Software:**
- Windows 11 (PowerShell 5.1+ is built-in)
- Vivado Lab 2026.1+ (provides Python for .xz decompression)
- Run PowerShell as **Administrator** (required for raw disk access)

No bmaptool, no WSL2, no additional software required.

**Note:** If PowerShell blocks script execution, run this once in your session:
```powershell
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
```

**Hardware:**
- SD card (minimum 32 GB) inserted into host machine card reader
- Card reader connected via USB or built-in slot

## Usage

```
Usage: .\vek385-flash-sdcard.ps1 -BootImages <path> -VivadoDir <path> [options]

Required:
  -BootImages <path>    Path to boot_images directory
  -VivadoDir <path>     Vivado Lab installation directory
                        [or set VIVADO_INSTALL_DIR env var]

Options:
  -SdDisk <n>           SD card disk number [default: auto-detect]
  -RootfsImage <file>   Rootfs image filename [default: auto-detect *.wic.xz]
  -ModelsDir <path>     Path to compiled models to copy to SD card
  -LogFile <path>       Log file path [default: auto-generated with timestamp]
  -SkipFlash            Skip flashing, only copy overlays and models
  -DryRun               Validate paths and settings without touching hardware
  -Yes                  Skip confirmation prompts
  -Help, -h             Show this help message
```

### Examples

```powershell
# Show help
.\vek385-flash-sdcard.ps1 -Help

# Dry run -- validate everything without touching SD card
.\vek385-flash-sdcard.ps1 -BootImages "C:\vek385\boot_images" -DryRun

# Full flash
.\vek385-flash-sdcard.ps1 -BootImages "C:\vek385\boot_images" -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab"

# Flash with explicit disk and no prompts
.\vek385-flash-sdcard.ps1 -BootImages "C:\vek385\boot_images" -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -SdDisk 2 -Yes

# Re-provision overlays without re-flashing (faster)
.\vek385-flash-sdcard.ps1 -BootImages "C:\vek385\boot_images" -SkipFlash

# Flash with compiled AI models
.\vek385-flash-sdcard.ps1 -BootImages "C:\vek385\boot_images" -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -ModelsDir "C:\vek385\models"
```

## How It Works

The script operates entirely on the host machine. The SD card is in the host's card reader -- there is no board interaction, no JTAG, no serial.

**Key difference from Linux:** Windows cannot write to ext4 filesystems. Instead of copying overlay files directly to the ext4 rootfs partition (as the Linux script does), this script stages them on the FAT32 boot partition. A modified `setup_overlay.sh` (baked into the rootfs via Yocto recipe) copies them from FAT32 to ext4 on first boot.

**Decompression:** The `.wic.xz` image is a plain xz-compressed file (not a tar archive). The script uses Python's `lzma` module from the Vivado Lab installation for decompression -- no separate Python install needed.

**Raw disk write:** Uses Windows kernel32 `CreateFileW` with `FILE_FLAG_NO_BUFFERING` and `FILE_FLAG_WRITE_THROUGH` for direct disk access. All volumes on the disk are locked and dismounted via `FSCTL_LOCK_VOLUME` and `FSCTL_DISMOUNT_VOLUME` before writing to prevent Windows from interfering mid-write.

### Step-by-Step Flow

```
         Windows Host                                SD Card
         ------------                                -------
Step 1   Get-Disk -----------------------> Disk 2 (32GB USB removable, USB 2.0)
         Shows: disk number, size, model, USB version, drive letters

Step 2   Remove drive letters
         (disk stays online for raw write)

Step 3   Python lzma decompress ---------> rootfs.wic (8GB temp file)
         (Python from Vivado Lab -- no separate install)

Step 4   Lock + dismount volumes
         Delete drive layout
         FileStream write to
         \\.\PhysicalDrive2 -------------> Raw disk write (4MB blocks)
                                              +-- Partition 1  FAT32  1GB  (boot)
                                              +-- Partition 2  ext4   1GB  (storage)
                                              +-- Partition 3  ext4   6GB  (rootfs)

Step 5   Detect FAT32 partition
         Assign drive letter (e.g., E:)

Step 6   Copy overlay files to E:\overlay\

Step 7   Sync -- SD card ready for removal
```

#### Pre-flight validation

1. Check running as Administrator
2. Validate `-BootImages` exists and contains `overlay/` directory
3. Auto-detect rootfs image by globbing `*.wic.xz`, excluding UFS images (`*.ufs.*`)
4. Find Python for decompression: Vivado's bundled Python first (via `-VivadoDir` or `VIVADO_INSTALL_DIR`), then system Python (skipping Windows Store stubs in WindowsApps)
5. Check temp disk space (~8 GB needed for decompressed .wic)
6. If `-DryRun`, print summary and exit

#### Step 1: Detect SD card

Scans `Get-Disk` for candidate devices:

| Card Reader Type | Detection Method |
|---|---|
| External USB reader | BusType = USB, size < 128 GB |
| Built-in SD/MMC slot | BusType = SD, MMC, or SDIO |

Shows disk number, size, model, USB version (2.0 or 3.x), and current drive letters. Requires user confirmation unless `-Yes` is specified. Refuses devices larger than 128 GB or system disks.

#### Step 2: Prepare disk

Removes drive letters from all existing partitions on the SD card. Saves drive letter assignments for restoration if the script fails before flashing.

#### Step 3: Decompress .wic.xz

Uses Python's `lzma` module to decompress the `.wic.xz` file to a temporary `.wic` file (~8 GB). An inline spinner shows decompressed size and elapsed time. Typically takes 1-2 minutes depending on disk speed.

The Python executable is sourced from Vivado Lab's bundled installation (`tps/win64/python-3.x.x/python.exe`). No separate Python installation is needed.

#### Step 4: Write raw image to SD card

Locks all volumes on the disk via `FSCTL_LOCK_VOLUME` and dismounts them via `FSCTL_DISMOUNT_VOLUME`. Clears the existing partition table via `IOCTL_DISK_DELETE_DRIVE_LAYOUT`. This prevents Windows from re-enumerating partitions mid-write.

Writes the raw `.wic` image to `\\.\PhysicalDrive<N>` using 4 MB block I/O with an inline progress spinner showing GB written, percentage, and write speed (MB/s).

The resulting partition layout:

| Partition | Type | Size | Contents |
|---|---|---|---|
| 1 | FAT32 | ~1 GB | EFI bootloader, kernel, loader config |
| 2 | ext4 | ~1 GB | Storage |
| 3 | ext4 | ~6 GB (expands on first boot) | Linux rootfs |

Windows can only see partition 1 (FAT32). The ext4 partitions are invisible to Windows.

#### Step 5: Detect FAT32 partition

Brings the disk online, waits for Windows to enumerate the new partitions, and assigns a drive letter (D: through Z:, first available) to the FAT32 boot partition. Removes drive letters from ext4 partitions to suppress "Format this disk?" popups.

#### Step 6: Copy overlays to FAT32

Copies overlay files from `boot_images/overlay/` to `<drive>:\overlay\` on the FAT32 partition. These files are staged for migration to the ext4 rootfs on first boot.

Files copied:
- `x_plus_ml.pdi` -- FPGA bitstream
- `x_plus_ml.dtbo` -- Device tree overlay
- `x_plus_ml.xclbin` -- XRT acceleration metadata
- `image_processing.cfg` -- XRT configuration

Optional: if `-ModelsDir` is specified, copies compiled AI models to `<drive>:\models\`.

Displays FAT32 partition contents and free space after copy.

#### Step 7: Finalize

Syncs writes and reports the SD card is ready for removal. Drive letters are left intact so the SD card auto-enumerates on re-insertion.

## FAT32 Staging

Because Windows cannot write to ext4 filesystems, overlay files are staged on the FAT32 boot partition instead of being written directly to the ext4 rootfs (as the Linux script does).

**What gets staged on FAT32:**

| File | Size | Purpose |
|---|---|---|
| `overlay/x_plus_ml.pdi` | ~5 MB | FPGA bitstream |
| `overlay/x_plus_ml.dtbo` | ~8 KB | Device tree overlay |
| `overlay/x_plus_ml.xclbin` | ~448 KB | XRT metadata |
| `overlay/image_processing.cfg` | ~1 KB | XRT configuration |
| `models/*` (optional) | varies | Compiled AI models |

**How it works on boot:**

A modified `setup_overlay.sh` (baked into the rootfs via Yocto recipe) runs automatically during boot via a systemd service. It:

1. Detects the boot partition dynamically (`/dev/sda1` or `/dev/mmcblk0p1`)
2. Mounts the FAT32 partition at a temporary mount point
3. Copies overlay files from FAT32 to `/overlay/` on the ext4 rootfs
4. Copies models (if present) from FAT32 to `/home/models/`
5. Unmounts FAT32
6. Proceeds with PL programming via `fpgautil`

Files are NOT removed from FAT32 -- they persist across reboots as a permanent source. On Linux-flashed SD cards, the FAT32 has no `overlay/` directory and this step is a no-op.

**Verification on target after boot:**
```bash
systemctl status vek385-setup.service   # all steps SUCCESS?
journalctl -u vek385-setup.service      # see migration messages
ls /overlay/                             # overlay files present?
```

## After Completion

```
  1. Remove the SD card and insert it into the VEK385 board
  2. Ensure SW1 DIP is set to OSPI mode (0001 = ON, ON, ON, OFF)
  3. Power on the board
  4. On first boot, overlay files are migrated from FAT32 to rootfs automatically
```

## Error Handling

| Scenario | Behavior |
|---|---|
| Not running as Administrator | Exits immediately with hint |
| Python not found | Exits with options: -VivadoDir, env var, or install Python |
| Windows Store Python stub detected | Skipped automatically (checks for WindowsApps in path) |
| Device > 128 GB | Refuses (not an SD card) |
| System disk selected | Checks IsBoot/IsSystem, refuses |
| CrowdStrike/antivirus interference | Volume locking prevents scanner from blocking writes |
| "Device not ready" during write | Prevented by FSCTL_LOCK_VOLUME + IOCTL_DISK_DELETE_DRIVE_LAYOUT |
| Decompression failure | Checks Python exit code, cleans up temp file |
| Raw disk write interrupted (Ctrl-C) | Cleanup: unlock volumes, remove temp file, restore drive letters |
| FAT32 partition not detected | Explicit partition scan + drive letter assignment with 30s timeout |
| Drive letter not restored on failure | Saved before removal, restored in cleanup if flash did not complete |
| ext4 "Format this disk?" popup | Drive letters removed from ext4 partitions |
| Temp disk space insufficient | Pre-check: warns if < 9 GB free |
| SD card not auto-enumerating on re-insert | Fixed: no Set-Disk -IsOffline (drive letters persist) |

## Log File

A timestamped log file is created in the current directory:

```
.\vek385-flash-sdcard_20260721_155054.log
```

Contains disk detection, decompression timing, write speed, partition detection, and file copy results. Override path with `-LogFile <path>`.

## Special Options

### -SkipFlash

Skips the decompression and raw flash steps. Jumps straight to detecting partitions and copying overlays. Useful when:
- SD card was already flashed and you need to update overlay files
- Re-provisioning after a model recompile

Does not require `-VivadoDir` (no Python needed).

### -Yes

Skips all confirmation prompts (device selection and destructive operation warning). Useful for scripted/automated workflows.

### -DryRun

Validates all paths, detects SD card, checks dependencies, then exits without touching hardware.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| "Script is not digitally signed" | PowerShell execution policy | Run: `Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process` |
| "Python with lzma support required" | No Python found | Use `-VivadoDir` to point to Vivado Lab install |
| "AppInstallerPythonRedirector" popup | Windows Store Python stub | Script skips it automatically; click Cancel if prompted |
| SD card not detected | Reader not recognized | Check Device Manager, use `-SdDisk <n>` |
| "Device not ready" during write | Antivirus scanning the disk | Script handles via volume locking; retry if needed |
| Drive letter not assigned after flash | Windows slow to enumerate | Wait a few seconds; script retries for 30s |
| SD card does not auto-enumerate on re-insert | Previous script version set disk offline | Update to latest script (no offline on exit) |
| Write speed very slow (~25 MB/s) | USB 2.0 card reader | Use USB 3.0 reader for ~80 MB/s; script shows USB version |
| FAT32 shows only boot files, no rootfs | Expected -- ext4 partitions invisible to Windows | Overlay files are on FAT32; rootfs is on ext4 (not visible) |
