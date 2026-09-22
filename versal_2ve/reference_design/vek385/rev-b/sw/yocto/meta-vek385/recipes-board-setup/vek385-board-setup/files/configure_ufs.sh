#!/bin/bash
set -u

UFS_CONFIG_FILE="/etc/ufs_config/ufsconfig_64gb"
UFS_BSG_DEV="/dev/bsg/ufs-bsg0"
LOG_FILE="/etc/ufs_config/configure_ufs.log"

exec > >(tee "$LOG_FILE") 2>&1

echo "=========================================="
echo " UFS Device Configuration Check"
echo " Config: $UFS_CONFIG_FILE"
echo " Date:   $(date)"
echo "=========================================="
echo ""

if ! command -v ufs-utils >/dev/null 2>&1; then
    echo "[ERROR] ufs-utils not found on PATH."
    exit 1
fi

if [ ! -e "$UFS_BSG_DEV" ]; then
    echo "[ERROR] UFS BSG device '$UFS_BSG_DEV' not found."
    exit 1
fi

if [ ! -f "$UFS_CONFIG_FILE" ]; then
    echo "[ERROR] Config file '$UFS_CONFIG_FILE' not found in $(pwd)"
    exit 1
fi

echo "[INFO] Reading current UFS configuration descriptor..."
desc_output=$(ufs-utils desc -t 1 -p "$UFS_BSG_DEV" 2>&1)
rc=$?

if [ $rc -ne 0 ]; then
    echo "[ERROR] Failed to read UFS configuration descriptor (exit code $rc)."
    echo "$desc_output"
    exit $rc
fi

if ! echo "$desc_output" | grep -q "Config 0 Unit"; then
    echo "[ERROR] Unexpected ufs-utils output -- 'Config 0 Unit' section not found."
    echo "[ERROR] Cannot determine UFS state. Refusing to write configuration."
    echo ""
    echo "Raw output:"
    echo "$desc_output"
    exit 1
fi

lu0_enable=$(echo "$desc_output" | grep -A1 "Config 0 Unit" | grep "bLUEnable" | awk -F'= ' '{print $NF}' || true)

if [ -z "$lu0_enable" ]; then
    echo "[ERROR] Could not parse bLUEnable from 'Config 0 Unit' section."
    echo "[ERROR] Cannot determine UFS state. Refusing to write configuration."
    echo ""
    echo "Raw output:"
    echo "$desc_output"
    exit 1
fi

if [ "$lu0_enable" = "0x1" ]; then
    echo "[OK] UFS device is already configured (LU0 enabled)."
    echo ""

    lu0_alloc=$(echo "$desc_output" | grep -A5 "Config 0 Unit" | grep "dNumAllocUnits" | awk -F'= ' '{print $NF}' || true)
    if [ -n "$lu0_alloc" ]; then
        lu0_size_mb=$(( lu0_alloc * 4 ))
        lu0_size_gb=$(( lu0_size_mb / 1024 ))
        echo "  LU0: enabled, dNumAllocUnits=$lu0_alloc (~${lu0_size_gb} GB)"
    else
        echo "  LU0: enabled (could not parse allocation units)"
    fi

    lu1_enable=$(echo "$desc_output" | grep -A1 "Config 1 Unit" | grep "bLUEnable" | awk -F'= ' '{print $NF}' || true)
    if [ "$lu1_enable" = "0x1" ]; then
        lu1_alloc=$(echo "$desc_output" | grep -A5 "Config 1 Unit" | grep "dNumAllocUnits" | awk -F'= ' '{print $NF}' || true)
        if [ -n "$lu1_alloc" ]; then
            lu1_size_mb=$(( lu1_alloc * 4 ))
            lu1_size_gb=$(( lu1_size_mb / 1024 ))
            echo "  LU1: enabled, dNumAllocUnits=$lu1_alloc (~${lu1_size_gb} GB)"
        else
            echo "  LU1: enabled (could not parse allocation units)"
        fi
    fi

    lu2_enable=$(echo "$desc_output" | grep -A1 "Config 2 Unit" | grep "bLUEnable" | awk -F'= ' '{print $NF}' || true)
    if [ "$lu2_enable" = "0x1" ]; then
        lu2_alloc=$(echo "$desc_output" | grep -A5 "Config 2 Unit" | grep "dNumAllocUnits" | awk -F'= ' '{print $NF}' || true)
        if [ -n "$lu2_alloc" ]; then
            lu2_size_mb=$(( lu2_alloc * 4 ))
            lu2_size_gb=$(( lu2_size_mb / 1024 ))
            echo "  LU2: enabled, dNumAllocUnits=$lu2_alloc (~${lu2_size_gb} GB)"
        else
            echo "  LU2: enabled (could not parse allocation units)"
        fi
    fi

    echo ""
    echo "No action needed. Skipping configuration to avoid data loss."
    exit 0
fi

echo "[INFO] UFS device is NOT configured (LU0 bLUEnable=$lu0_enable)."
echo ""
echo "Writing UFS configuration..."
echo "  Command: ufs-utils desc -t 1 -w $UFS_CONFIG_FILE -p $UFS_BSG_DEV"
echo ""

ufs-utils desc -t 1 -w "$UFS_CONFIG_FILE" -p "$UFS_BSG_DEV"
rc=$?

if [ $rc -ne 0 ]; then
    echo "[ERROR] ufs-utils write failed with exit code $rc"
    exit $rc
fi

echo ""
echo "[OK] UFS configuration written successfully."
echo ""
echo "*** REBOOT REQUIRED for changes to take effect. ***"
echo "Run 'reboot' to apply the new UFS configuration."
