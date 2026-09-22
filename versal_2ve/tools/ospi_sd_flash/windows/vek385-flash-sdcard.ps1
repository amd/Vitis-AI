#Requires -Version 5.1
<#
.SYNOPSIS
    Flash SD card for VEK385 Rev-B (Windows 11, native -- no WSL2)

.DESCRIPTION
    Automates UG1787 (SD Card Setup):
      1. Detects the SD card device
      2. Decompresses rootfs.wic.xz using Windows built-in tar
      3. Writes the raw .wic image to the SD card via .NET FileStream
      4. Copies overlay files and on-target scripts to the FAT32 partition
      5. Optionally copies compiled models

    All operations are native Windows -- no WSL2, no usbipd, no third-party
    tools. Overlay files are staged on the FAT32 boot partition; a modified
    setup_overlay.sh (baked into the rootfs via Yocto recipe) migrates them
    to the ext4 rootfs on first boot.

    REQUIREMENTS
      - Run as Administrator (required for raw disk access)
      - Windows 11 (built-in tar with xz support)

.PARAMETER BootImages
    Path to boot_images directory (must contain *.wic.xz and overlay/).

.PARAMETER SdDisk
    Windows disk number of the SD card (e.g. 2). Default: auto-detect.

.PARAMETER RootfsImage
    Rootfs image filename inside BootImages. Default: auto-detect *.wic.xz.

.PARAMETER ModelsDir
    Path to compiled models directory to copy to FAT32 staging area.

.PARAMETER LogFile
    Log file path. Default: .\vek385-flash-sdcard_<timestamp>.log

.PARAMETER SkipFlash
    Skip flashing; only copy overlays and models.

.PARAMETER DryRun
    Validate paths and settings without touching hardware.

.PARAMETER Yes
    Skip confirmation prompts.

.EXAMPLE
    .\vek385-flash-sdcard.ps1 -BootImages "C:\images\boot_images"

.EXAMPLE
    .\vek385-flash-sdcard.ps1 -BootImages "C:\images\boot_images" -SdDisk 2 -Yes
#>

[CmdletBinding()]
param(
    [string]$BootImages   = "",
    [string]$VivadoDir    = "",
    [int]   $SdDisk       = -1,
    [string]$RootfsImage  = "",
    [string]$ModelsDir    = "",
    [string]$LogFile      = "",
    [switch]$SkipFlash,
    [switch]$DryRun,
    [switch]$Yes,
    [Alias("h")][switch]$Help
)

if ($Help) {
    Write-Host ""
    Write-Host "Usage: .\vek385-flash-sdcard.ps1 -BootImages <path> [options]"
    Write-Host ""
    Write-Host "Flash SD card for VEK385 Rev-B (Windows 11 -- Native, no WSL2)"
    Write-Host "Automates UG1787 (SD Card Setup)"
    Write-Host ""
    Write-Host "Required:"
    Write-Host "  -BootImages <path>    Path to boot_images directory"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -VivadoDir <path>     Vivado Lab install (for Python xz decompression)"
    Write-Host "  -SdDisk <n>           SD card disk number (default: auto-detect)"
    Write-Host "  -RootfsImage <file>   Rootfs image filename (default: auto-detect *.wic.xz)"
    Write-Host "  -ModelsDir <path>     Path to compiled models to copy to SD card"
    Write-Host "  -LogFile <path>       Log file path (default: auto-generated with timestamp)"
    Write-Host "  -SkipFlash            Skip flashing, only copy overlays and models"
    Write-Host "  -DryRun               Validate paths and settings without touching hardware"
    Write-Host "  -Yes                  Skip confirmation prompts"
    Write-Host "  -Help, -h             Show this help message"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\vek385-flash-sdcard.ps1 -BootImages `"C:\images\boot_images`""
    Write-Host "  .\vek385-flash-sdcard.ps1 -BootImages `"C:\images\boot_images`" -DryRun"
    Write-Host "  .\vek385-flash-sdcard.ps1 -BootImages `"C:\images\boot_images`" -SdDisk 2 -Yes"
    Write-Host "  .\vek385-flash-sdcard.ps1 -BootImages `"C:\images\boot_images`" -SkipFlash"
    Write-Host ""
    exit 0
}

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
$script:BlockSize     = 4MB
$script:MaxSdSizeGB   = 128

# ---------------------------------------------------------------------------
# Raw disk I/O helper (compiled once, cached for session)
# Provides: OpenDisk, LockVolume, DismountVolume, DeleteDriveLayout,
#           UpdateProperties, UnlockAll
# ---------------------------------------------------------------------------
if (-not ([System.Management.Automation.PSTypeName]'RawDiskIO').Type) {
    Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class RawDiskIO {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFileW(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeviceIoControl(
        IntPtr hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, uint nInBufferSize,
        IntPtr lpOutBuffer, uint nOutBufferSize,
        out uint lpBytesReturned, IntPtr lpOverlapped);

    const uint GENERIC_READ          = 0x80000000;
    const uint GENERIC_WRITE         = 0x40000000;
    const uint FILE_SHARE_READ       = 0x00000001;
    const uint FILE_SHARE_WRITE      = 0x00000002;
    const uint OPEN_EXISTING         = 3;
    const uint FILE_FLAG_NO_BUFFERING   = 0x20000000;
    const uint FILE_FLAG_WRITE_THROUGH  = 0x80000000;
    const uint FSCTL_LOCK_VOLUME        = 0x00090018;
    const uint FSCTL_DISMOUNT_VOLUME    = 0x00090020;
    const uint FSCTL_ALLOW_EXTENDED_DASD_IO = 0x00090083;
    const uint IOCTL_DISK_DELETE_DRIVE_LAYOUT = 0x00040010;
    const uint IOCTL_DISK_UPDATE_PROPERTIES   = 0x00070140;

    static List<IntPtr> openHandles = new List<IntPtr>();

    // Open a volume, lock it, and dismount it. Returns true if successful.
    public static bool LockAndDismountVolume(string volumePath) {
        IntPtr h = CreateFileW(volumePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) return false;

        uint br;
        bool locked = false;
        for (int i = 0; i < 20; i++) {
            if (DeviceIoControl(h, FSCTL_LOCK_VOLUME, IntPtr.Zero, 0,
                IntPtr.Zero, 0, out br, IntPtr.Zero)) { locked = true; break; }
            System.Threading.Thread.Sleep(500);
        }
        if (!locked) { CloseHandle(h); return false; }

        DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, IntPtr.Zero, 0,
            IntPtr.Zero, 0, out br, IntPtr.Zero);
        openHandles.Add(h);
        return true;
    }

    // Open physical drive for raw read+write with no buffering.
    public static FileStream OpenDiskForWrite(string physDrivePath) {
        IntPtr h = CreateFileW(physDrivePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) {
            int err = Marshal.GetLastWin32Error();
            throw new IOException("Cannot open " + physDrivePath + " (Win32 error " + err + ")");
        }
        // Allow writes beyond reported disk geometry
        uint br;
        DeviceIoControl(h, FSCTL_ALLOW_EXTENDED_DASD_IO, IntPtr.Zero, 0,
            IntPtr.Zero, 0, out br, IntPtr.Zero);
        var sh = new Microsoft.Win32.SafeHandles.SafeFileHandle(h, true);
        return new FileStream(sh, FileAccess.ReadWrite, 4194304);
    }

    // Delete drive layout (clears partition table without triggering re-enumeration)
    public static bool DeleteDriveLayout(string physDrivePath) {
        IntPtr h = CreateFileW(physDrivePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) return false;
        uint br;
        bool ok = DeviceIoControl(h, IOCTL_DISK_DELETE_DRIVE_LAYOUT, IntPtr.Zero, 0,
            IntPtr.Zero, 0, out br, IntPtr.Zero);
        CloseHandle(h);
        return ok;
    }

    // Tell Windows to re-read partition table after write
    public static bool UpdateProperties(string physDrivePath) {
        IntPtr h = CreateFileW(physDrivePath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
        if (h == IntPtr.Zero || h == new IntPtr(-1)) return false;
        uint br;
        bool ok = DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, IntPtr.Zero, 0,
            IntPtr.Zero, 0, out br, IntPtr.Zero);
        CloseHandle(h);
        return ok;
    }

    // Close all locked volume handles
    public static void UnlockAll() {
        foreach (var h in openHandles) { try { CloseHandle(h); } catch {} }
        openHandles.Clear();
    }
}
"@ -ErrorAction Stop
}
$script:Spin          = @('|','/','-','\')

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
if ($LogFile -eq "") {
    $LogFile = Join-Path (Get-Location) "vek385-flash-sdcard_${Timestamp}.log"
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "========================================================================"
    Write-Host "  $msg"
    Write-Host "========================================================================"
}
function Write-Info([string]$msg) {
    Write-Host "  [INFO]  $msg"
    Add-Content -Path $LogFile -Value "[INFO]  $msg" -Encoding UTF8
}
function Write-Warn([string]$msg) {
    Write-Host "  [WARN]  $msg"
    Add-Content -Path $LogFile -Value "[WARN]  $msg" -Encoding UTF8
}
function Exit-Error([string]$msg) {
    Write-Host ""
    Write-Host "  [ERROR] $msg"
    Write-Host ""
    Add-Content -Path $LogFile -Value "[ERROR] $msg" -Encoding UTF8
    Invoke-Cleanup
    exit 1
}
function Invoke-Confirm([string]$prompt) {
    if ($Yes) { return }
    Write-Host ""
    Write-Host -NoNewline "  $prompt [y/N] "
    $ans = Read-Host
    if ($ans -ne "y" -and $ans -ne "Y") { Write-Host "  Aborted."; exit 0 }
}

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
$script:TempWicFile = ""
$script:SavedDriveLetters = @()
$script:FlashCompleted = $false

function Invoke-Cleanup {
    # Release any locked volumes (prevents disk from being stuck)
    try { [RawDiskIO]::UnlockAll() } catch {}

    # Remove temp decompressed .wic file
    if ($script:TempWicFile -ne "" -and (Test-Path $script:TempWicFile)) {
        Write-Warn "Cleaning up temp file: $($script:TempWicFile)"
        try { Remove-Item $script:TempWicFile -Force } catch {}
        $script:TempWicFile = ""
    }

    # Restore drive letters only if the raw write hasn't completed
    # (after write, old partitions are gone -- restoring would target wrong partitions)
    if (-not $script:FlashCompleted -and $script:SavedDriveLetters.Count -gt 0) {
        Write-Warn "Restoring drive letters..."
        Start-Sleep -Seconds 1
        foreach ($dl in $script:SavedDriveLetters) {
            try {
                Add-PartitionAccessPath -DiskNumber $dl.DiskNumber -PartitionNumber $dl.PartitionNumber `
                    -AccessPath $dl.DriveLetter -ErrorAction SilentlyContinue
                Write-Warn "  Restored $($dl.DriveLetter) to partition $($dl.PartitionNumber)"
            } catch {}
        }
        $script:SavedDriveLetters = @()
    }
}

$null = Register-EngineEvent -SourceIdentifier ([System.Management.Automation.PsEngineEvent]::Exiting) `
        -Action { Invoke-Cleanup }

# ---------------------------------------------------------------------------
# Initialize log
# ---------------------------------------------------------------------------
"VEK385 SD Card Flash Log - $(Get-Date)" | Set-Content -Path $LogFile -Encoding UTF8

# ---------------------------------------------------------------------------
# Root / Admin check
# ---------------------------------------------------------------------------
$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "ERROR: This script must be run as Administrator."
    Write-Host "       Right-click PowerShell -> Run as Administrator."
    exit 1
}

# ---------------------------------------------------------------------------
# Validate arguments
# ---------------------------------------------------------------------------
$BootImages = $BootImages.Trim().Trim('"').Trim("'").TrimEnd('\','/')

if ($BootImages -eq "") {
    Write-Host "ERROR: -BootImages <path> required."
    Write-Host "Usage: .\vek385-flash-sdcard.ps1 -BootImages <path> [options]"
    Write-Host "  -SdDisk <n>         SD card disk number"
    Write-Host "  -RootfsImage <file> Rootfs image filename"
    Write-Host "  -ModelsDir <path>   Models directory"
    Write-Host "  -SkipFlash          Skip flashing"
    Write-Host "  -DryRun             Validate only"
    Write-Host "  -Yes                No prompts"
    exit 1
}

if (-not (Test-Path $BootImages -PathType Container)) {
    Write-Host "ERROR: Boot images directory not found: $BootImages"; exit 1
}
$BootImages = (Resolve-Path $BootImages).Path

if ($RootfsImage -eq "") {
    $cands = @(Get-ChildItem -Path $BootImages -Filter "*.wic.xz" -File |
               Where-Object { $_.Name -notmatch '\.ufs\.' })
    if     ($cands.Count -eq 1) { $RootfsImage = $cands[0].Name; Write-Info "Auto-detected rootfs image: $RootfsImage" }
    elseif ($cands.Count -gt 1) { Write-Host "ERROR: Multiple *.wic.xz in $BootImages -- use -RootfsImage."; exit 1 }
    else                        { Write-Host "ERROR: No *.wic.xz found in $BootImages."; exit 1 }
}

$RootfsImagePath = Join-Path $BootImages $RootfsImage
if (-not (Test-Path $RootfsImagePath)) { Write-Host "ERROR: $RootfsImage not found."; exit 1 }

$OverlayDir = Join-Path $BootImages "overlay"
if (-not (Test-Path $OverlayDir -PathType Container)) {
    Write-Host "ERROR: overlay/ not found in $BootImages"; exit 1
}

# Find Python for xz decompression (.wic.xz is a plain xz file, not a tar archive)
$script:PythonExe = ""
if (-not $SkipFlash -and -not $DryRun) {
    # Check Vivado's bundled Python first (always reliable, no Store stubs)
    $VivadoDir = $VivadoDir.Trim().Trim('"').Trim("'").TrimEnd('\','/')
    if ($VivadoDir -eq "" -and $env:VIVADO_INSTALL_DIR) {
        $VivadoDir = $env:VIVADO_INSTALL_DIR.Trim().Trim('"').Trim("'").TrimEnd('\','/')
    }
    if ($VivadoDir -ne "") {
        $vivPythons = @(Get-ChildItem -Path "$VivadoDir\tps\win64" -Filter "python-*" -Directory -ErrorAction SilentlyContinue)
        foreach ($pd in $vivPythons) {
            $candidate = Join-Path $pd.FullName "python.exe"
            if (Test-Path $candidate) { $script:PythonExe = $candidate; break }
        }
    }

    # Fall back to system Python (skip Windows Store stubs in WindowsApps)
    if ($script:PythonExe -eq "") {
        $sysPython = Get-Command "python" -ErrorAction SilentlyContinue
        if ($sysPython -and $sysPython.Source -notmatch 'WindowsApps') {
            $lzmaCheck = & $sysPython.Source -c "import lzma; print('ok')" 2>&1
            if ($lzmaCheck -eq "ok") { $script:PythonExe = $sysPython.Source }
        }
    }

    if ($script:PythonExe -eq "") {
        Write-Host "ERROR: Python with lzma support required for .wic.xz decompression."
        Write-Host "       Options:"
        Write-Host "         - Use -VivadoDir to point to Vivado (bundles Python)"
        Write-Host "         - Set VIVADO_INSTALL_DIR environment variable"
        Write-Host "         - Install Python 3.8+ and add to PATH"
        exit 1
    }
    Write-Info "Python for decompression: $($script:PythonExe)"
}

# Check temp disk space (~8GB needed for decompressed .wic)
if (-not $SkipFlash) {
    $tempDrive = [System.IO.Path]::GetTempPath().Substring(0,2)
    $tempFree  = (Get-PSDrive -Name $tempDrive.TrimEnd(':') -ErrorAction SilentlyContinue).Free
    if ($tempFree -and $tempFree -lt 9GB) {
        Write-Warn "Less than 9 GB free on $tempDrive ($([Math]::Round($tempFree/1GB,1)) GB)."
        Write-Warn "Decompression needs ~8 GB temp space."
    }
}

# ---------------------------------------------------------------------------
# Detect SD card
# ---------------------------------------------------------------------------
Write-Step "Detecting SD card device"

function Get-SdCandidates {
    @(Get-Disk -ErrorAction SilentlyContinue | Where-Object {
        $bus  = $_.BusType -in @('USB','SD','MMC','SDIO')
        $size = $_.Size -le ($script:MaxSdSizeGB * 1GB) -and $_.Size -gt 0
        $rem  = $null; try { $rem = $_.IsRemovable } catch {}
        ($bus -or $rem -eq $true) -and $size
    })
}

function Format-DiskLine([CimInstance]$d) {
    $gb    = [Math]::Round($d.Size / 1GB, 1)
    $model = if ($d.FriendlyName) { $d.FriendlyName } else { "unknown" }
    $parts = @(Get-Partition -DiskNumber $d.Number -ErrorAction SilentlyContinue)
    $drives = @()
    foreach ($p in $parts) {
        $vol = $p | Get-Volume -ErrorAction SilentlyContinue
        if ($vol -and $vol.DriveLetter) {
            $label = if ($vol.FileSystemLabel) { "$($vol.DriveLetter): [$($vol.FileSystemLabel)]" } else { "$($vol.DriveLetter):" }
            $drives += $label
        }
    }
    $driveStr = if ($drives.Count -gt 0) { "  Drives: $($drives -join ', ')" } else { "" }
    "Disk $($d.Number)  $gb GB  $model  (Bus: $($d.BusType))$driveStr"
}

if ($SdDisk -lt 0) {
    $candidates = @(Get-SdCandidates)

    if ($candidates.Count -eq 0) {
        Write-Host ""; Write-Host "  No SD card candidates detected."; Write-Host ""
        Get-Disk | Format-Table Number,FriendlyName,@{L='Size(GB)';E={[Math]::Round($_.Size/1GB,1)}},BusType -AutoSize |
            Out-String | ForEach-Object { Write-Host "  $_" }
        Write-Host -NoNewline "  Enter SD card disk number: "
        $SdDisk = [int](Read-Host)
    } elseif ($candidates.Count -eq 1) {
        $d = $candidates[0]
        Write-Host ""; Write-Host "  Detected SD card:"; Write-Host "    $(Format-DiskLine $d)"; Write-Host ""
        if (-not $Yes) {
            Write-Host -NoNewline "  Use this disk? [y/N] "
            $ans = Read-Host
            if ($ans -ne "y" -and $ans -ne "Y") {
                Write-Host -NoNewline "  Enter disk number: "; $SdDisk = [int](Read-Host)
            } else { $SdDisk = $d.Number }
        } else { $SdDisk = $d.Number }
    } else {
        Write-Host ""; Write-Host "  Multiple SD card candidates:"
        for ($i = 0; $i -lt $candidates.Count; $i++) {
            Write-Host "    $($i+1)) $(Format-DiskLine $candidates[$i])"
        }
        Write-Host ""; Write-Host -NoNewline "  Enter disk number or list index: "
        $choice = Read-Host
        if ($choice -match '^\d+$') {
            $ci = [int]$choice
            $SdDisk = if ($ci -ge 1 -and $ci -le $candidates.Count) { $candidates[$ci-1].Number } else { $ci }
        } else { Exit-Error "Invalid selection: $choice" }
    }
}

try { $sdDiskObj = Get-Disk -Number $SdDisk -ErrorAction Stop }
catch { Exit-Error "Disk $SdDisk not found: $_" }

$sdSizeGB = [Math]::Round($sdDiskObj.Size / 1GB, 1)
if ($sdDiskObj.Size -gt ($script:MaxSdSizeGB * 1GB)) {
    Exit-Error "Disk $SdDisk is ${sdSizeGB} GB -- too large. Refusing."
}

$sysDisk = @(Get-Disk | Where-Object {
    $b = $null; try { $b = $_.IsBoot   } catch {}
    $s = $null; try { $s = $_.IsSystem } catch {}
    $b -eq $true -or $s -eq $true
})
if ($sysDisk | Where-Object { $_.Number -eq $SdDisk }) {
    Exit-Error "Disk $SdDisk is the system disk. Refusing."
}

$sdModel = if ($sdDiskObj.FriendlyName) { $sdDiskObj.FriendlyName } else { "unknown" }

# Detect USB version by walking PnP device tree to find the USB controller type
$sdUsbVersion = ""
try {
    $diskWmi = Get-WmiObject -Query "SELECT PNPDeviceID FROM Win32_DiskDrive WHERE Index=$SdDisk" -ErrorAction Stop
    $parentId = (Get-PnpDeviceProperty -InstanceId $diskWmi.PNPDeviceID `
                    -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data
    for ($walk = 0; $walk -lt 10 -and $parentId; $walk++) {
        if ($parentId -imatch 'XHCI|USB3') { $sdUsbVersion = "USB 3.x"; break }
        if ($parentId -imatch 'EHCI|USB\\ROOT') { $sdUsbVersion = "USB 2.0"; break }
        $parentId = (Get-PnpDeviceProperty -InstanceId $parentId `
                        -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data
    }
} catch {}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
$usbInfo = if ($sdUsbVersion) { "  ($sdUsbVersion)" } else { "" }

Write-Host ""
Write-Host "========================================================================"
Write-Host "  VEK385 SD Card Flash Tool  (Windows 11 -- Native)"
Write-Host "  Automates UG1787 (SD Card Setup)"
Write-Host "========================================================================"
Write-Host ""
Write-Host "  Boot images:   $BootImages"
Write-Host "  Rootfs image:  $RootfsImage"
Write-Host "  SD disk:       Disk $SdDisk  ($sdSizeGB GB)  $sdModel$usbInfo"
Write-Host "  Log file:      $LogFile"
if ($ModelsDir -ne "") { Write-Host "  Models:        $ModelsDir" }
if ($DryRun)           { Write-Host "  Mode:          DRY RUN" }
Write-Host ""
Write-Host "  Method:        Native Windows (Python xz + raw disk write + FAT32 copy)"
Write-Host "  Dependencies:  None (no WSL2, no usbipd)"

if ($DryRun) {
    Write-Step "Dry run complete -- all paths validated"
    Write-Host ""; Write-Host "  Re-run without -DryRun to flash."; Write-Host ""
    exit 0
}

if (-not $SkipFlash) {
    Invoke-Confirm "WARNING: All data on Disk $SdDisk ($sdModel, $sdSizeGB GB) will be destroyed. Continue?"
}

# ===========================================================================
# Main execution wrapped in try/finally for reliable Ctrl-C cleanup
# ===========================================================================
try {

# ---------------------------------------------------------------------------
# Step 1: Dismount all partitions on the SD card
# ---------------------------------------------------------------------------
Write-Step "Step 1: Preparing Disk $SdDisk"

$parts = @(Get-Partition -DiskNumber $SdDisk -ErrorAction SilentlyContinue)
foreach ($p in $parts) {
    $vol = $p | Get-Volume -ErrorAction SilentlyContinue
    if ($vol -and $vol.DriveLetter) {
        $script:SavedDriveLetters += @{ DiskNumber=$SdDisk; PartitionNumber=$p.PartitionNumber; DriveLetter="$($vol.DriveLetter):" }
        Write-Info "Removing drive letter $($vol.DriveLetter): from partition $($p.PartitionNumber)"
        Remove-PartitionAccessPath -DiskNumber $SdDisk -PartitionNumber $p.PartitionNumber `
            -AccessPath "$($vol.DriveLetter):" -ErrorAction SilentlyContinue
    }
}

Write-Info "Drive letters removed. Disk stays online for raw write."

if (-not $SkipFlash) {
    # ---------------------------------------------------------------------------
    # Step 2: Decompress .wic.xz
    # ---------------------------------------------------------------------------
    Write-Step "Step 2: Decompressing $RootfsImage"

    $script:TempWicFile = Join-Path ([System.IO.Path]::GetTempPath()) "vek385_rootfs_${Timestamp}.wic"
    Write-Info "Decompressing to: $($script:TempWicFile)"
    Write-Info "This may take 1-2 minutes..."

    $decompStart = Get-Date
    $si = 0

    # Use Python lzma to decompress .xz (.wic.xz is a plain xz file, not a tar archive)
    $pyScript = @"
import lzma, sys, os
src = sys.argv[1]
dst = sys.argv[2]
with lzma.open(src) as f_in:
    with open(dst, 'wb') as f_out:
        while True:
            chunk = f_in.read(4 * 1024 * 1024)
            if not chunk:
                break
            f_out.write(chunk)
"@
    $pyScriptFile = Join-Path ([System.IO.Path]::GetTempPath()) "xz_decompress_${Timestamp}.py"
    [System.IO.File]::WriteAllText($pyScriptFile, $pyScript, [System.Text.Encoding]::ASCII)

    $pyProc = Start-Process -FilePath $script:PythonExe `
        -ArgumentList "`"$pyScriptFile`" `"$RootfsImagePath`" `"$($script:TempWicFile)`"" `
        -RedirectStandardError "$($script:TempWicFile).err" `
        -NoNewWindow -PassThru

    while (-not $pyProc.WaitForExit(1000)) {
        $elapsed = [int]((Get-Date) - $decompStart).TotalSeconds
        if (Test-Path $script:TempWicFile) {
            $currentSize = (Get-Item $script:TempWicFile -ErrorAction SilentlyContinue).Length
            $pct = if ($currentSize -gt 0) { [Math]::Min(95, [int]($currentSize / 8.5GB * 100)) } else { 0 }
            Write-Host "`r  Decompressing  $($script:Spin[$si % 4])  $([Math]::Round($currentSize/1GB,1)) GB  ($elapsed`s)" -NoNewline
        } else {
            Write-Host "`r  Decompressing  $($script:Spin[$si % 4])  ($elapsed`s)" -NoNewline
        }
        $si++
    }
    Write-Host ""

    # Ensure exit code is populated (WaitForExit(ms) can return before async events flush)
    $pyProc.WaitForExit(5000) | Out-Null
    $pyExit = if ($null -ne $pyProc.ExitCode) { $pyProc.ExitCode } else { 0 }
    $pyErr  = ""
    if (Test-Path "$($script:TempWicFile).err") {
        $pyErr = Get-Content "$($script:TempWicFile).err" -Raw -ErrorAction SilentlyContinue
        Remove-Item "$($script:TempWicFile).err" -Force -ErrorAction SilentlyContinue
    }
    Remove-Item $pyScriptFile -Force -ErrorAction SilentlyContinue

    if ($pyExit -ne 0) {
        Exit-Error "xz decompression failed (exit $pyExit): $pyErr"
    }

    $wicSize = (Get-Item $script:TempWicFile).Length
    if ($wicSize -lt 1GB) {
        Exit-Error "Decompressed image is only $([Math]::Round($wicSize/1MB,1)) MB -- expected ~8 GB. Corrupt image?"
    }

    $decompDur = [int]((Get-Date) - $decompStart).TotalSeconds
    Write-Info "Decompressed: $([Math]::Round($wicSize/1GB,1)) GB in ${decompDur}s"

    # ---------------------------------------------------------------------------
    # Step 3: Write raw image to SD card
    # ---------------------------------------------------------------------------
    Write-Step "Step 3: Writing image to Disk $SdDisk"

    $diskPath = "\\.\PhysicalDrive$SdDisk"

    # Lock and dismount all volumes on the disk to prevent Windows interference
    Write-Info "Locking volumes on Disk $SdDisk..."
    Get-Partition -DiskNumber $SdDisk -ErrorAction SilentlyContinue | ForEach-Object {
        foreach ($ap in $_.AccessPaths) {
            if ($ap -and $ap -ne "") {
                $volPath = $ap.TrimEnd('\')
                $locked = [RawDiskIO]::LockAndDismountVolume($volPath)
                if ($locked) { Write-Info "  Locked: $volPath" }
            }
        }
    }

    # Delete drive layout to clear partition table without triggering re-enumeration
    Write-Info "Clearing partition table..."
    [RawDiskIO]::DeleteDriveLayout($diskPath) | Out-Null
    Start-Sleep -Seconds 1

    Write-Info "Opening $diskPath for raw write..."

    $flashStart = Get-Date
    $srcStream  = $null
    $dstStream  = $null
    $si         = 0

    try {
        $srcStream = [System.IO.File]::OpenRead($script:TempWicFile)
        $dstStream = [RawDiskIO]::OpenDiskForWrite($diskPath)

        $buffer    = [byte[]]::new($script:BlockSize)
        $written   = [long]0
        $total     = $srcStream.Length

        $estMinutes = if ($sdUsbVersion -match '3') { [Math]::Ceiling($total / 80MB / 60) } else { [Math]::Ceiling($total / 25MB / 60) }
        Write-Info "Writing $([Math]::Round($total/1GB,1)) GB (~${estMinutes} min on $( if ($sdUsbVersion) { $sdUsbVersion } else { 'USB' } ))..."

        while ($true) {
            $read = $srcStream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }

            # Pad last block to sector boundary (512 bytes) for FILE_FLAG_NO_BUFFERING
            if ($read % 512 -ne 0) {
                $padded = (([Math]::Ceiling($read / 512)) * 512)
                for ($i = $read; $i -lt $padded; $i++) { $buffer[$i] = 0 }
                $read = $padded
            }

            $dstStream.Write($buffer, 0, $read)
            $written += $read

            $pct = [Math]::Min(99, [int]($written / $total * 100))
            $gbWritten = [Math]::Round($written / 1GB, 1)
            $elapsed   = [int]((Get-Date) - $flashStart).TotalSeconds
            $speedVal  = if ($elapsed -gt 0) { [Math]::Round($written / 1MB / $elapsed, 1) } else { 0 }
            Write-Host "`r  Writing  $($script:Spin[$si % 4])  $gbWritten / $([Math]::Round($total/1GB,1)) GB  ($pct%)  $speedVal MB/s  " -NoNewline
            $si++
        }

        $dstStream.Flush()
        Write-Host ""

    } catch {
        Write-Host ""
        Exit-Error "Raw disk write failed: $_"
    } finally {
        if ($srcStream) { $srcStream.Close() }
        if ($dstStream) { $dstStream.Close() }
        [RawDiskIO]::UnlockAll()
    }

    # Raw write succeeded -- old partitions are gone, don't restore old drive letters on cleanup
    $script:FlashCompleted = $true

    $flashDur = [int]((Get-Date) - $flashStart).TotalSeconds
    Write-Info "Write complete: $([Math]::Round($written/1GB,1)) GB in ${flashDur}s ($([Math]::Round($written/1MB/$flashDur,1)) MB/s)"

    # Tell Windows to re-read the new partition table
    Write-Info "Updating disk properties..."
    [RawDiskIO]::UpdateProperties($diskPath) | Out-Null

    # Clean up temp file
    if ($script:TempWicFile -ne "" -and (Test-Path $script:TempWicFile)) {
        Write-Info "Removing temp file..."
        Remove-Item $script:TempWicFile -Force -ErrorAction SilentlyContinue
        $script:TempWicFile = ""
    }

} else {
    Write-Info "Skipping flash (-SkipFlash)."
}

# ---------------------------------------------------------------------------
# Step 4: Detect FAT32 partition and assign drive letter
# ---------------------------------------------------------------------------
Write-Step "Step 4: Detecting partitions on Disk $SdDisk"

# Bring disk online so Windows enumerates partitions
Set-Disk -Number $SdDisk -IsOffline $false -ErrorAction SilentlyContinue
Start-Sleep -Seconds 3
Update-StorageProviderCache -ErrorAction SilentlyContinue

# Wait for partitions to appear
$maxWait  = 30
$elapsed2 = 0
$fatPart  = $null
$si       = 0

while ($elapsed2 -lt $maxWait) {
    $allParts = @(Get-Partition -DiskNumber $SdDisk -ErrorAction SilentlyContinue)
    foreach ($p in $allParts) {
        $vol = $p | Get-Volume -ErrorAction SilentlyContinue
        if ($vol -and $vol.FileSystemType -eq 'FAT32') {
            $fatPart = $p
            break
        }
    }
    if ($fatPart) { break }
    Write-Host "`r  Waiting for partitions  $($script:Spin[$si % 4])  ($elapsed2`s)" -NoNewline
    $si++; Start-Sleep -Seconds 2; $elapsed2 += 2
}
Write-Host ""

if (-not $fatPart) {
    # Fallback: try first partition (often FAT32 even if not detected yet)
    $allParts = @(Get-Partition -DiskNumber $SdDisk -ErrorAction SilentlyContinue)
    if ($allParts.Count -gt 0) {
        $fatPart = $allParts[0]
        Write-Warn "No FAT32 detected; using first partition (Partition $($fatPart.PartitionNumber))."
    } else {
        Exit-Error "No partitions found on Disk $SdDisk after flashing."
    }
}

# Assign a drive letter if not already assigned
$driveLetter = ""
$vol = $fatPart | Get-Volume -ErrorAction SilentlyContinue
if ($vol -and $vol.DriveLetter) {
    $driveLetter = "$($vol.DriveLetter):"
} else {
    # Find an unused drive letter
    $usedLetters = @((Get-Volume -ErrorAction SilentlyContinue).DriveLetter | Where-Object { $_ })
    $available   = (68..90) | ForEach-Object { [char]$_ } | Where-Object { $_ -notin $usedLetters } | Select-Object -First 1
    if (-not $available) { Exit-Error "No available drive letters." }
    $driveLetter = "$($available):"
    Add-PartitionAccessPath -DiskNumber $SdDisk -PartitionNumber $fatPart.PartitionNumber `
        -AccessPath $driveLetter -ErrorAction Stop
    Start-Sleep -Seconds 2
}

Write-Info "FAT32 partition: Partition $($fatPart.PartitionNumber) -> $driveLetter"

# Suppress Windows "format?" popups for ext4 partitions by removing their drive letters
foreach ($p in @(Get-Partition -DiskNumber $SdDisk -ErrorAction SilentlyContinue)) {
    if ($p.PartitionNumber -eq $fatPart.PartitionNumber) { continue }
    $v = $p | Get-Volume -ErrorAction SilentlyContinue
    if ($v -and $v.DriveLetter) {
        Remove-PartitionAccessPath -DiskNumber $SdDisk -PartitionNumber $p.PartitionNumber `
            -AccessPath "$($v.DriveLetter):" -ErrorAction SilentlyContinue
        Write-Info "Removed drive letter from ext4 partition $($p.PartitionNumber)"
    }
}

# ---------------------------------------------------------------------------
# Step 5: Copy overlays to FAT32 partition
# ---------------------------------------------------------------------------
Write-Step "Step 5: Copying overlays to FAT32 ($driveLetter)"

$fatOverlayDir = Join-Path $driveLetter "overlay"
New-Item -ItemType Directory -Path $fatOverlayDir -Force -ErrorAction SilentlyContinue | Out-Null

# Copy overlay files from boot_images
Copy-Item -Path "$OverlayDir\*" -Destination $fatOverlayDir -Recurse -Force
$overlayFiles = (Get-ChildItem $OverlayDir).Name -join ', '
Write-Info "Overlay files: $overlayFiles"

# Copy on-target scripts from script directory (if present)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$YoctoDir  = Join-Path $ScriptDir "yocto\recipes-board-setup\vek385-board-setup\files"
foreach ($s in @("setup_overlay.sh", "runtime_env.sh")) {
    $src = Join-Path $YoctoDir $s
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $fatOverlayDir -Force
        Write-Info "Copied $s to overlay/"
    }
}

# Copy UFS config files
$cfgUfs = Join-Path $YoctoDir "configure_ufs.sh"
$ufsCfg = Join-Path $YoctoDir "ufsconfig_64gb"
if ((Test-Path $cfgUfs) -and (Test-Path $ufsCfg)) {
    $fatUfsDir = Join-Path $driveLetter "ufs_config"
    New-Item -ItemType Directory -Path $fatUfsDir -Force -ErrorAction SilentlyContinue | Out-Null
    Copy-Item -Path $cfgUfs -Destination $fatUfsDir -Force
    Copy-Item -Path $ufsCfg -Destination $fatUfsDir -Force
    Write-Info "Copied UFS config to ufs_config/"
} elseif ((Test-Path $cfgUfs) -xor (Test-Path $ufsCfg)) {
    Write-Warn "UFS config incomplete: need both configure_ufs.sh and ufsconfig_64gb."
}

# Copy systemd service file
$svcSrc = Join-Path $YoctoDir "vek385-setup.service"
if (Test-Path $svcSrc) {
    Copy-Item -Path $svcSrc -Destination (Join-Path $driveLetter "overlay") -Force
    Write-Info "Copied vek385-setup.service to overlay/"
}

# ---------------------------------------------------------------------------
# Step 6: Optional models
# ---------------------------------------------------------------------------
if ($ModelsDir -ne "") {
    $ModelsDir = $ModelsDir.Trim().Trim('"').Trim("'").TrimEnd('\','/')
    if (Test-Path $ModelsDir -PathType Container) {
        Write-Step "Step 6: Copying models"
        $fatModelsDir = Join-Path $driveLetter "models"
        New-Item -ItemType Directory -Path $fatModelsDir -Force -ErrorAction SilentlyContinue | Out-Null
        Copy-Item -Path "$ModelsDir\*" -Destination $fatModelsDir -Recurse -Force
        Write-Info "Models copied to models/"
    } else {
        Write-Warn "Models directory not found: $ModelsDir -- skipping."
    }
}

# Show FAT32 contents
Write-Host ""
Write-Host "  FAT32 partition contents ($driveLetter):"
Get-ChildItem $driveLetter -ErrorAction SilentlyContinue |
    ForEach-Object { $sz = if ($_.PSIsContainer) { "<DIR>" } else { $_.Length.ToString() }; Write-Host "    $($_.Mode)  $($sz.PadLeft(10))  $($_.Name)" }

$fatFree = (Get-PSDrive -Name $driveLetter.TrimEnd(':') -ErrorAction SilentlyContinue).Free
if ($fatFree) { Write-Info "Free space on FAT32: $([Math]::Round($fatFree/1MB,1)) MB" }

# ---------------------------------------------------------------------------
# Step 7: Finalize
# ---------------------------------------------------------------------------
Write-Step "Step 7: Finalizing SD card"

Write-Info "Syncing..."
Start-Sleep -Seconds 2
Write-Info "SD card is ready. You may remove it."

} finally {
    Invoke-Cleanup
}

Write-Step "SD card is ready!"
Write-Host ""
Write-Host "  Log file: $LogFile"
Write-Host ""
Write-Host "  Next steps:"
Write-Host "    1. Remove the SD card and insert it into the VEK385 board"
Write-Host "    2. Ensure SW1 DIP is set to OSPI mode (0001 = ON, ON, ON, OFF)"
Write-Host "    3. Power on the board"
Write-Host "    4. On first boot, overlay files will be migrated from FAT32 to rootfs automatically"
Write-Host ""

exit 0
