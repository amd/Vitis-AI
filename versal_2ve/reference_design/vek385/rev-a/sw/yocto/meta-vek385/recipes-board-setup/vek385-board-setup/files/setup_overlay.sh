#!/bin/bash

# Copy overlay files from FAT32 boot partition (Windows flash path).
# When the SD card is prepared from Windows, overlay files are placed on
# the FAT32 boot partition because Windows cannot write to ext4.
# This block detects the boot partition, mounts it, copies staged files
# to /overlay/ on the ext4 rootfs, then unmounts. Files are NOT removed
# from FAT32 — they serve as a persistent source across reboots.
# On Linux-flashed SD cards, FAT32 has no overlay/ directory and the
# copy block is a no-op.

# Detect the FAT32 boot partition (partition 1 of the root disk).
# The SD card may appear as /dev/sda (USB) or /dev/mmcblk0 (native MMC).
detect_boot_partition() {
    local root_dev disk_dev

    # Primary: findmnt resolves /dev/root to actual device
    root_dev=$(findmnt -n -o SOURCE / 2>/dev/null)

    # Fallback: find the only disk in /proc/partitions
    if [ -z "$root_dev" ] || [ "$root_dev" = "/dev/root" ]; then
        local disk
        disk=$(awk '$4 ~ /^(sd[a-z]|mmcblk[0-9]+)$/ {print $4; exit}' /proc/partitions)
        if [ -z "$disk" ]; then return 1; fi
        if echo "$disk" | grep -qE 'mmcblk|nvme'; then
            echo "/dev/${disk}p1"
        else
            echo "/dev/${disk}1"
        fi
        return
    fi

    # Strip partition number to get the base disk device
    # /dev/sda3 → /dev/sda    /dev/mmcblk0p3 → /dev/mmcblk0    /dev/nvme0n1p3 → /dev/nvme0n1
    if [[ "$root_dev" =~ (mmcblk[0-9]+)p[0-9]+$ ]]; then
        disk_dev="/dev/${BASH_REMATCH[1]}"
    elif [[ "$root_dev" =~ (nvme[0-9]+n[0-9]+)p[0-9]+$ ]]; then
        disk_dev="/dev/${BASH_REMATCH[1]}"
    else
        disk_dev=$(echo "$root_dev" | sed -E 's/[0-9]+$//')
    fi

    # Construct partition 1 (boot partition)
    if echo "$disk_dev" | grep -qE 'mmcblk|nvme'; then
        echo "${disk_dev}p1"
    else
        echo "${disk_dev}1"
    fi
}

FAT32_DEV=$(detect_boot_partition)
FAT32_MNT="/mnt/fat32_boot"

if [ -n "$FAT32_DEV" ] && [ -b "$FAT32_DEV" ]; then
    mkdir -p "$FAT32_MNT"
    if mount "$FAT32_DEV" "$FAT32_MNT"; then
        if [ -d "$FAT32_MNT/overlay" ]; then
            echo "----copying overlays from FAT32 staging area ($FAT32_DEV)----"
            mkdir -p /overlay
            if ! cp -r "$FAT32_MNT/overlay/"* /overlay/; then
                echo "[ERROR] Failed to copy overlay files from FAT32"
            fi
            if [ -d "$FAT32_MNT/models" ]; then
                mkdir -p /home/models
                cp -r "$FAT32_MNT/models/"* /home/models/
                echo "  models copied to /home/models/"
            fi
            echo "  overlay staging complete"
        fi
        umount "$FAT32_MNT" 2>/dev/null
    else
        echo "[WARN] Could not mount $FAT32_DEV -- skipping FAT32 overlay check"
    fi
    rmdir "$FAT32_MNT" 2>/dev/null
fi

if [ -f ./x_plus_ml.pdi ] && [ -f ./x_plus_ml.dtbo ]; then
    echo "----load Overlay xclbins----"
    fpgautil -b x_plus_ml.pdi -o x_plus_ml.dtbo
else
    echo "[WARN] x_plus_ml.pdi or x_plus_ml.dtbo not found in /overlay -- skipping Overlay programming"
fi

if [ -f ./x_plus_ml.xclbin ] && [ -f ./image_processing.cfg ]; then
    echo "----copy PL cfg and xclbin to rootfs----"
    mkdir -p /run/media/mmcblk0p1/
    cp ./x_plus_ml.xclbin /run/media/mmcblk0p1/
    cp ./image_processing.cfg /run/media/mmcblk0p1/
else
    echo "[WARN] x_plus_ml.xclbin or image_processing.cfg not found in /overlay -- skipping copy"
fi
