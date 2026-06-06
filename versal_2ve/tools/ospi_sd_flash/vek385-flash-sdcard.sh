#!/bin/bash
#
# vek385-flash-sdcard.sh -- Flash rootfs and copy overlays to SD card
#
# This script automates UG1787 (SD Card Setup):
#   1. Detects the SD card device
#   2. Flashes rootfs.wic.xz using bmaptool
#   3. Mounts the storage partition and copies overlays
#   4. Optionally copies compiled models
#   5. Unmounts and ejects the SD card
#
# Prerequisites:
#   - SD card inserted into host machine card reader
#   - bmaptool installed (sudo apt install bmap-tools)
#
# Usage:
#   sudo ./vek385-flash-sdcard.sh --boot-images <path> [options]
#
# Required:
#   --boot-images <path>   Path to boot_images directory
#
# Options:
#   --sd-device <dev>      SD card device (e.g., /dev/sdc)
#   --rootfs-image <file>  Rootfs image filename (default: auto-detect *.wic.xz)
#   --models-dir <path>    Path to compiled models to copy
#   --log-file <path>      Log file path (default: ./vek385-flash-sdcard_<timestamp>.log)
#   --skip-flash           Skip flashing, only copy overlays
#   --dry-run              Validate paths and settings without touching hardware
#   --yes                  Skip confirmation prompts
#   --help                 Show this help message
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
BOOT_IMAGES=""
SD_DEVICE=""
ROOTFS_IMAGE=""
MODELS_DIR=""
SKIP_FLASH=false
DRY_RUN=false
YES=false
MOUNT_POINT="/mnt/vek385-sd"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$(pwd)/vek385-flash-sdcard_${TIMESTAMP}.log"

# Track mount state for cleanup
MOUNTED=false

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() {
    cat <<'USAGE'
Usage: sudo ./vek385-flash-sdcard.sh --boot-images <path> [options]

Automate SD card preparation for VEK385 Rev-B (UG1787)

Required:
  --boot-images <path>   Path to boot_images directory

Options:
  --sd-device <dev>      SD card device (e.g., /dev/sdc)
                         (default: auto-detect removable disk)
  --rootfs-image <file>  Rootfs image filename (default: auto-detect *.wic.xz)
  --models-dir <path>    Path to compiled models to copy to SD card
  --log-file <path>      Log file path (default: ./vek385-flash-sdcard_<timestamp>.log)
  --skip-flash           Skip flashing, only copy overlays/models
  --dry-run              Validate paths and settings without touching hardware
  --yes                  Skip confirmation prompts
  --help                 Show this help message
USAGE
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --boot-images)   BOOT_IMAGES="$2"; shift 2 ;;
        --sd-device)     SD_DEVICE="$2"; shift 2 ;;
        --rootfs-image)  ROOTFS_IMAGE="$2"; shift 2 ;;
        --models-dir)    MODELS_DIR="$2"; shift 2 ;;
        --log-file)      LOG_FILE="$2"; shift 2 ;;
        --skip-flash)    SKIP_FLASH=true; shift ;;
        --dry-run)       DRY_RUN=true; shift ;;
        --yes)           YES=true; shift ;;
        --help)          usage ;;
        *)               echo "ERROR: Unknown option: $1"; usage ;;
    esac
done

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
step() {
    local msg="$1"
    echo "" | tee -a "$LOG_FILE"
    echo "========================================================================" | tee -a "$LOG_FILE"
    echo "  $msg" | tee -a "$LOG_FILE"
    echo "========================================================================" | tee -a "$LOG_FILE"
}

info() {
    echo "  [INFO]  $1" | tee -a "$LOG_FILE"
}

warn() {
    echo "  [WARN]  $1" | tee -a "$LOG_FILE"
}

error_exit() {
    echo "" | tee -a "$LOG_FILE"
    echo "  [ERROR] $1" | tee -a "$LOG_FILE"
    echo "" | tee -a "$LOG_FILE"
    exit 1
}

confirm() {
    if $YES; then
        return 0
    fi
    echo ""
    echo -n "  $1 [y/N] "
    read -r answer
    if [[ "$answer" != "y" && "$answer" != "Y" ]]; then
        echo "  Aborted."
        exit 0
    fi
}

# ---------------------------------------------------------------------------
# Cleanup trap
# ---------------------------------------------------------------------------
cleanup() {
    if $MOUNTED; then
        info "Cleaning up: unmounting $MOUNT_POINT..."
        umount "$MOUNT_POINT" 2>/dev/null || true
        rmdir "$MOUNT_POINT" 2>/dev/null || true
        MOUNTED=false
    fi
}

trap cleanup EXIT
trap 'echo ""; warn "Interrupted by user."; exit 130' INT
trap 'warn "Terminated."; exit 143' TERM

# ---------------------------------------------------------------------------
# Initialize log file
# ---------------------------------------------------------------------------
: > "$LOG_FILE"
info "Logging to $LOG_FILE"

# ---------------------------------------------------------------------------
# Root check
# ---------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    error_exit "This script must be run as root (sudo)."
fi

# ---------------------------------------------------------------------------
# Validate required arguments
# ---------------------------------------------------------------------------
if [[ -z "$BOOT_IMAGES" ]]; then
    echo "ERROR: Boot images directory is required."
    echo "       Use --boot-images <path>."
    echo ""
    usage
fi

if [[ ! -d "$BOOT_IMAGES" ]]; then
    error_exit "Boot images directory not found: $BOOT_IMAGES"
fi

# Normalize to absolute path
BOOT_IMAGES="$(cd "$BOOT_IMAGES" && pwd)"

# Auto-detect rootfs image if not specified
if [[ -z "$ROOTFS_IMAGE" ]]; then
    CANDIDATES=("$BOOT_IMAGES"/*.wic.xz)
    # Filter out UFS images -- we want the SD card image
    SD_CANDIDATES=()
    for f in "${CANDIDATES[@]}"; do
        [[ -f "$f" ]] || continue
        # Skip UFS-specific images
        if [[ "$(basename "$f")" != *".ufs."* ]]; then
            SD_CANDIDATES+=("$(basename "$f")")
        fi
    done

    if [[ ${#SD_CANDIDATES[@]} -eq 1 ]]; then
        ROOTFS_IMAGE="${SD_CANDIDATES[0]}"
        info "Auto-detected rootfs image: $ROOTFS_IMAGE"
    elif [[ ${#SD_CANDIDATES[@]} -gt 1 ]]; then
        echo "ERROR: Multiple *.wic.xz images found in $BOOT_IMAGES:"
        for f in "${SD_CANDIDATES[@]}"; do echo "         $f"; done
        echo "       Use --rootfs-image <file> to select one."
        exit 1
    else
        error_exit "No *.wic.xz image found in $BOOT_IMAGES. Use --rootfs-image <file>."
    fi
fi

if [[ ! -f "$BOOT_IMAGES/$ROOTFS_IMAGE" ]]; then
    error_exit "$ROOTFS_IMAGE not found in $BOOT_IMAGES"
fi

# Derive bmap filename from rootfs image
# rootfs.wic.xz -> rootfs.wic.bmap
ROOTFS_BMAP="${ROOTFS_IMAGE%.xz}.bmap"

if [[ ! -d "$BOOT_IMAGES/overlay" ]]; then
    error_exit "overlay/ directory not found in $BOOT_IMAGES"
fi

# ---------------------------------------------------------------------------
# Check dependencies
# ---------------------------------------------------------------------------
if ! $SKIP_FLASH; then
    if ! command -v bmaptool &>/dev/null; then
        error_exit "bmaptool not found. Install with: sudo apt install bmap-tools"
    fi
fi

# ---------------------------------------------------------------------------
# Detect / validate SD card device
# ---------------------------------------------------------------------------
step "Detecting SD card device"

detect_sd_candidates() {
    # Find candidate SD card devices:
    #   1. Removable disks (RM=1) -- standard USB card readers
    #   2. USB-attached disks <128GB with RM=0 -- some readers report non-removable
    #   3. MMC block devices (mmcblk*) -- built-in SD card slots on laptops/desktops
    # Excludes NVMe, loop, and system disks.
    local candidates=()

    while IFS= read -r line; do
        local dev rm type tran
        dev=$(echo "$line" | awk '{print $1}')
        rm=$(echo "$line" | awk '{print $3}')
        type=$(echo "$line" | awk '{print $6}')
        tran=$(echo "$line" | awk '{print $7}')

        [[ "$type" != "disk" ]] && continue

        if [[ "$rm" == "1" ]]; then
            candidates+=("$dev")
        elif [[ "$dev" == mmcblk* ]]; then
            # Built-in SD card reader (PCI/SDHCI) -- always a candidate
            candidates+=("$dev")
        elif [[ "$tran" == "usb" ]]; then
            # USB disk reporting RM=0 -- include if <128GB
            local dev_size
            dev_size=$(blockdev --getsize64 "/dev/$dev" 2>/dev/null || echo 0)
            if [[ $dev_size -gt 0 && $dev_size -le 137438953472 ]]; then
                candidates+=("$dev")
            fi
        fi
    done < <(lsblk -ndo NAME,MAJ:MIN,RM,SIZE,RO,TYPE,TRAN 2>/dev/null)

    printf '%s\n' "${candidates[@]}"
}

if [[ -z "$SD_DEVICE" ]]; then
    mapfile -t CANDIDATES < <(detect_sd_candidates)

    if [[ ${#CANDIDATES[@]} -eq 0 ]]; then
        echo ""
        echo "  No SD card candidates detected."
        echo ""
        echo "  Available block devices:"
        lsblk -do NAME,SIZE,RM,TYPE,TRAN,MODEL 2>/dev/null | grep -v loop | sed 's/^/    /'
        echo ""
        echo -n "  Enter SD card device (e.g., /dev/sdc): "
        read -r SD_DEVICE
    elif [[ ${#CANDIDATES[@]} -eq 1 ]]; then
        SD_DEVICE="/dev/${CANDIDATES[0]}"
        SD_MODEL=$(lsblk -ndo MODEL "$SD_DEVICE" 2>/dev/null || echo "unknown")
        SD_SIZE=$(lsblk -ndo SIZE "$SD_DEVICE" 2>/dev/null || echo "unknown")
        echo ""
        echo "  Detected SD card candidate:"
        echo "    $SD_DEVICE  ${SD_SIZE}  ${SD_MODEL}"
        echo ""
        if ! $YES; then
            echo -n "  Use this device? [y/N] "
            read -r answer
            if [[ "$answer" != "y" && "$answer" != "Y" ]]; then
                echo ""
                echo -n "  Enter SD card device (e.g., /dev/sdc): "
                read -r SD_DEVICE
            fi
        fi
    else
        echo ""
        echo "  Multiple SD card candidates detected:"
        for i in "${!CANDIDATES[@]}"; do
            dev="/dev/${CANDIDATES[$i]}"
            model=$(lsblk -ndo MODEL "$dev" 2>/dev/null || echo "unknown")
            size=$(lsblk -ndo SIZE "$dev" 2>/dev/null || echo "?")
            rm=$(lsblk -ndo RM "$dev" 2>/dev/null || echo "?")
            echo "    $((i+1))) $dev  ${size}  ${model}  (removable=$rm)"
        done
        echo ""
        echo -n "  Enter device path or number: "
        read -r choice
        if [[ "$choice" =~ ^[0-9]+$ && "$choice" -ge 1 && "$choice" -le ${#CANDIDATES[@]} ]]; then
            SD_DEVICE="/dev/${CANDIDATES[$((choice-1))]}"
        else
            SD_DEVICE="$choice"
        fi
    fi
fi

if [[ ! -b "$SD_DEVICE" ]]; then
    error_exit "$SD_DEVICE is not a valid block device."
fi

# Safety check: refuse system disks
SD_SIZE_BYTES=$(blockdev --getsize64 "$SD_DEVICE" 2>/dev/null || echo 0)
SD_SIZE_GB=$((SD_SIZE_BYTES / 1073741824))

if [[ $SD_SIZE_GB -gt 128 ]]; then
    error_exit "Device $SD_DEVICE is ${SD_SIZE_GB}GB -- too large to be an SD card. Refusing to flash."
fi

# Check if any partition is mounted as /
if mount | grep "^${SD_DEVICE}" | grep -q " / "; then
    error_exit "$SD_DEVICE appears to be the system disk. Refusing to flash."
fi

SD_MODEL=$(lsblk -ndo MODEL "$SD_DEVICE" 2>/dev/null || echo "unknown")

# ---------------------------------------------------------------------------
# Print configuration summary
# ---------------------------------------------------------------------------
echo ""
echo "========================================================================"
echo "  VEK385 SD Card Flash Tool"
echo "  Automates UG1787 (SD Card Setup)"
echo "========================================================================"
echo ""
echo "  Boot images:   $BOOT_IMAGES"
echo "  Rootfs image:  $ROOTFS_IMAGE"
echo "  Bmap file:     $ROOTFS_BMAP ($([ -f "$BOOT_IMAGES/$ROOTFS_BMAP" ] && echo 'found' || echo 'not found'))"
echo "  SD device:     $SD_DEVICE"
echo "  Device size:   ${SD_SIZE_GB}GB"
echo "  Device model:  $SD_MODEL"
echo "  Log file:      $LOG_FILE"
if [[ -n "$MODELS_DIR" ]]; then
    echo "  Models:        $MODELS_DIR"
fi
if $DRY_RUN; then
    echo "  Mode:          DRY RUN (no hardware operations)"
fi

echo ""
lsblk "$SD_DEVICE" 2>/dev/null | sed 's/^/    /'

# ---------------------------------------------------------------------------
# Dry run -- validate only
# ---------------------------------------------------------------------------
if $DRY_RUN; then
    step "Dry run complete -- all paths and settings validated"
    echo ""
    echo "  All prerequisites satisfied. Re-run without --dry-run to flash."
    echo ""
    exit 0
fi

if ! $SKIP_FLASH; then
    confirm "WARNING: All data on $SD_DEVICE will be destroyed. Continue?"
fi

# ---------------------------------------------------------------------------
# Unmount existing partitions
# ---------------------------------------------------------------------------
step "Unmounting existing partitions on $SD_DEVICE"

for part in "${SD_DEVICE}"*; do
    if mountpoint -q "$part" 2>/dev/null || mount | grep -q "^$part "; then
        info "Unmounting $part"
        umount "$part" 2>/dev/null || true
    fi
done

# Kill any processes still holding the device to prevent "device busy" errors
fuser -k "$SD_DEVICE"* 2>/dev/null || true
info "All partitions unmounted."

# ---------------------------------------------------------------------------
# Flash rootfs
# ---------------------------------------------------------------------------
if ! $SKIP_FLASH; then
    step "Flashing $ROOTFS_IMAGE to $SD_DEVICE"

    FLASH_START=$(date +%s)

    if [[ -f "$BOOT_IMAGES/$ROOTFS_BMAP" ]]; then
        info "Using bmaptool with bmap file for faster flashing..."
        bmaptool copy "$BOOT_IMAGES/$ROOTFS_IMAGE" "$SD_DEVICE" 2>&1
        BMAP_RC=$?
    else
        warn "No bmap file ($ROOTFS_BMAP) found, flashing without block map (slower)..."
        bmaptool copy --nobmap "$BOOT_IMAGES/$ROOTFS_IMAGE" "$SD_DEVICE" 2>&1
        BMAP_RC=$?
    fi

    if [[ $BMAP_RC -ne 0 ]]; then
        error_exit "bmaptool copy failed (exit code $BMAP_RC). Check log: $LOG_FILE"
    fi

    FLASH_END=$(date +%s)
    FLASH_DURATION=$((FLASH_END - FLASH_START))
    info "Flash complete in ${FLASH_DURATION}s."

    # Wait for kernel to re-read partition table
    info "Waiting for partitions to appear..."
    sleep 2
    partprobe "$SD_DEVICE" 2>/dev/null || true
    sleep 2
else
    info "Skipping flash (--skip-flash)."
fi

# ---------------------------------------------------------------------------
# Detect storage partition
# ---------------------------------------------------------------------------
step "Detecting SD card partitions"

lsblk -f "$SD_DEVICE" 2>/dev/null | sed 's/^/    /' | tee -a "$LOG_FILE"

PARTITIONS=($(lsblk -nlo NAME "$SD_DEVICE" | grep -v "^$(basename "$SD_DEVICE")$"))

if [[ ${#PARTITIONS[@]} -eq 0 ]]; then
    error_exit "No partitions found on $SD_DEVICE after flashing."
fi

# Find the ext4 rootfs partition (the one we can write overlays to)
STORAGE_PART=""
for ((i=${#PARTITIONS[@]}-1; i>=0; i--)); do
    PART_DEV="/dev/${PARTITIONS[$i]}"
    PART_FSTYPE=$(lsblk -ndo FSTYPE "$PART_DEV" 2>/dev/null || echo "")

    if [[ "$PART_FSTYPE" == "ext4" || "$PART_FSTYPE" == "ext3" || "$PART_FSTYPE" == "ext2" ]]; then
        STORAGE_PART="$PART_DEV"
        info "Found ext4 partition: $STORAGE_PART"
        break
    fi
done

if [[ -z "$STORAGE_PART" ]]; then
    warn "No ext4 partition found. Falling back to last partition."
    STORAGE_PART="/dev/${PARTITIONS[-1]}"
    FALLBACK_FSTYPE=$(lsblk -ndo FSTYPE "$STORAGE_PART" 2>/dev/null || echo "unknown")
    warn "Last partition $STORAGE_PART has filesystem: $FALLBACK_FSTYPE"
fi

info "Storage partition: $STORAGE_PART"

# ---------------------------------------------------------------------------
# Mount and copy overlays
# ---------------------------------------------------------------------------
step "Copying overlays to SD card"

mkdir -p "$MOUNT_POINT"
if ! mount "$STORAGE_PART" "$MOUNT_POINT"; then
    error_exit "Failed to mount $STORAGE_PART at $MOUNT_POINT"
fi
MOUNTED=true
info "Mounted $STORAGE_PART at $MOUNT_POINT"

# Copy overlay directory
info "Copying overlay files..."
cp -r "$BOOT_IMAGES/overlay" "$MOUNT_POINT/"
OVERLAY_FILES=$(ls "$BOOT_IMAGES/overlay/" | tr '\n' ' ')
info "Overlay files copied: $OVERLAY_FILES"

# Copy setup scripts from scripts directory if available
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
for setup_script in setup_overlay.sh runtime_env.sh; do
    if [[ -f "$SCRIPT_DIR/$setup_script" ]]; then
        cp "$SCRIPT_DIR/$setup_script" "$MOUNT_POINT/overlay/"
        chmod +x "$MOUNT_POINT/overlay/$setup_script"
        info "Copied $setup_script to /overlay/"
    fi
done

# Install runtime environment variables into /etc/profile.d/ so they are
# available in every user login shell (SSH, serial, etc.) without needing
# to manually source anything.
if [[ -f "$SCRIPT_DIR/runtime_env.sh" ]]; then
    mkdir -p "$MOUNT_POINT/etc/profile.d"
    # Extract only the export lines into a profile.d script
    grep '^export ' "$SCRIPT_DIR/runtime_env.sh" > "$MOUNT_POINT/etc/profile.d/vek385-runtime.sh"
    chmod 644 "$MOUNT_POINT/etc/profile.d/vek385-runtime.sh"
    info "Installed /etc/profile.d/vek385-runtime.sh (env vars available on login)"
fi

# Install UFS configuration files
if [[ -f "$SCRIPT_DIR/configure_ufs.sh" && -f "$SCRIPT_DIR/ufsconfig_64gb" ]]; then
    mkdir -p "$MOUNT_POINT/etc/ufs_config"
    cp "$SCRIPT_DIR/configure_ufs.sh" "$MOUNT_POINT/etc/ufs_config/"
    cp "$SCRIPT_DIR/ufsconfig_64gb" "$MOUNT_POINT/etc/ufs_config/"
    chmod +x "$MOUNT_POINT/etc/ufs_config/configure_ufs.sh"
    info "Copied UFS config files to /etc/ufs_config/"
elif [[ -f "$SCRIPT_DIR/configure_ufs.sh" || -f "$SCRIPT_DIR/ufsconfig_64gb" ]]; then
    warn "UFS config incomplete: both configure_ufs.sh and ufsconfig_64gb are required."
fi

# Install systemd service to auto-run board setup on boot
SERVICE_FILE="vek385-setup.service"
if [[ -f "$SCRIPT_DIR/$SERVICE_FILE" ]]; then
    cp "$SCRIPT_DIR/$SERVICE_FILE" "$MOUNT_POINT/etc/systemd/system/"
    mkdir -p "$MOUNT_POINT/etc/systemd/system/multi-user.target.wants"
    ln -sf "/etc/systemd/system/$SERVICE_FILE" \
           "$MOUNT_POINT/etc/systemd/system/multi-user.target.wants/$SERVICE_FILE"
    info "Installed $SERVICE_FILE (board setup will run automatically on boot)"
else
    warn "$SERVICE_FILE not found. Board setup will NOT run automatically on boot."
fi

# Copy models if specified
if [[ -n "$MODELS_DIR" ]]; then
    if [[ -d "$MODELS_DIR" ]]; then
        step "Copying models to /home/models/"
        mkdir -p "$MOUNT_POINT/home/models"
        if ls "$MODELS_DIR"/* &>/dev/null; then
            cp -r "$MODELS_DIR"/* "$MOUNT_POINT/home/models/"
            info "Models copied to /home/models/"
        else
            warn "Models directory $MODELS_DIR is empty, skipping."
        fi
    else
        warn "Models directory $MODELS_DIR not found, skipping."
    fi
fi

# Show what's on the SD card
echo ""
echo "  SD card contents ($STORAGE_PART):"
ls -la "$MOUNT_POINT/" | sed 's/^/    /'

# Show free space
AVAIL=$(df -h "$MOUNT_POINT" | tail -1 | awk '{print $4}')
info "Free space on partition: $AVAIL"

# ---------------------------------------------------------------------------
# Sync, unmount, eject
# ---------------------------------------------------------------------------
step "Finalizing SD card"

info "Syncing..."
sync

info "Unmounting $MOUNT_POINT..."
umount "$MOUNT_POINT"
MOUNTED=false
rmdir "$MOUNT_POINT" 2>/dev/null || true

info "Ejecting $SD_DEVICE..."
if ! eject "$SD_DEVICE" 2>&1; then
    warn "Could not eject $SD_DEVICE -- it may still be busy."
    warn "Wait a moment and manually remove the SD card."
else
    info "SD card ejected."
fi

step "SD card is ready!"

echo ""
echo "  Log file: $LOG_FILE"
echo ""
echo "  Next steps:"
echo "    1. Insert the SD card into the VEK385 board"
echo "    2. Ensure SW1 DIP is set to OSPI mode (0001 = ON, ON, ON, OFF)"
echo "    3. Power on the board"
echo ""

exit 0
