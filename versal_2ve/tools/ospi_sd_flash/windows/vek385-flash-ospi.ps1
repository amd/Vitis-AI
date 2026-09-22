#Requires -Version 5.1
<#
.SYNOPSIS
    Automate OSPI programming on VEK385 Rev-A/Rev-B (Windows 11)

.DESCRIPTION
    Automates UG1787 (Board Setup):
      1. Connects to the board via XSDB (JTAG)
      2. Downloads BOOT.bin to board memory via XSDB
      3. Downloads OSPI image to board memory via XSDB
      4. Flashes the OSPI image via U-Boot serial commands

    XSDB is driven in batch mode (xsdb script.tcl) so stdout is fully
    captured without pseudo-console or interactive-prompt issues.

.PARAMETER VivadoDir
    Vivado/Vitis Lab installation directory (or set VIVADO_INSTALL_DIR env var).

.PARAMETER BootImages
    Path to directory containing BOOT.bin and the OSPI image.

.PARAMETER SerialPort
    Serial port for U-Boot console (e.g. COM6). Default: auto-detect.

.PARAMETER XsdbPort
    XSDB hw_server port. Default: 3121.

.PARAMETER OspiImage
    OSPI image filename inside BootImages. Default: auto-detect *ospi*.bin.

.PARAMETER BootBin
    Boot binary filename inside BootImages. Default: BOOT.bin.

.PARAMETER LogFile
    Log file path. Default: .\vek385-flash-ospi_<timestamp>.log

.PARAMETER DryRun
    Validate paths and settings without touching hardware.

.EXAMPLE
    .\vek385-flash-ospi.ps1 -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -BootImages "C:\images\boot_images"
#>

[CmdletBinding()]
param(
    [string]$VivadoDir  = "",
    [string]$BootImages = "",
    [string]$SerialPort = "",
    [string]$XsdbPort   = "3121",
    [string]$OspiImage  = "",
    [string]$BootBin    = "BOOT.bin",
    [string]$LogFile    = "",
    [switch]$DryRun,
    [switch]$ShowSerial,
    [Alias("h")][switch]$Help
)

if ($Help) {
    $helpText = @'

Usage: .\vek385-flash-ospi.ps1 -VivadoDir <path> -BootImages <path> [options]

Automate OSPI programming on VEK385 Rev-A/Rev-B [Windows 11]
Automates UG1787 [Board Setup]

Required:
  -VivadoDir <path>     Vivado/Vitis Lab installation directory
                        [or set VIVADO_INSTALL_DIR env var]
  -BootImages <path>    Path to boot_images directory

Options:
  -SerialPort <port>    Serial port for U-Boot console [default: auto-detect]
  -XsdbPort <port>      XSDB hw_server port [default: 3121]
  -OspiImage <file>     OSPI image filename [default: auto-detect *ospi*.bin]
  -BootBin <file>       Boot binary filename [default: BOOT.bin]
  -LogFile <path>       Log file path [default: auto-generated with timestamp]
  -DryRun               Validate paths and settings without touching hardware
  -ShowSerial           Echo serial port output to console
  -Help, -h             Show this help message

Examples:
  .\vek385-flash-ospi.ps1 -VivadoDir "C:\Vivado_Lab" -BootImages "C:\boot_images"
  .\vek385-flash-ospi.ps1 -VivadoDir "C:\Vivado_Lab" -BootImages "C:\boot_images" -DryRun
  .\vek385-flash-ospi.ps1 -VivadoDir "C:\Vivado_Lab" -BootImages "C:\boot_images" -OspiImage "BOOT.bin"

'@
    Write-Host $helpText
    exit 0
}

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
$script:TargetFilter  = "*xc2ve*"
$script:EraseBlock    = 0x10000
$script:FlashMax      = 0x10000000
$script:UbootPromptRx = 'versal2>\s*|=>\s*'
$script:Spin          = @('|','/','-','\')

$script:TimeoutBoot  = 120
$script:TimeoutDow   = 120
$script:TimeoutSf    = 60
$script:TimeoutFlash = 600

$script:SerialPort_obj  = $null
$script:HwServerProcess = $null
$script:TmpTcl          = ""

$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
if ($LogFile -eq "") {
    $LogFile = Join-Path (Get-Location) "vek385-flash-ospi_${Timestamp}.log"
}

# ---------------------------------------------------------------------------
# Display helpers
# ---------------------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "========================================================================"
    Write-Host "  $msg"
    Write-Host "========================================================================"
}
function Write-Info([string]$msg) { Write-Host "  [INFO]  $msg" }
function Write-Warn([string]$msg) { Write-Host "  [WARN]  $msg" }
function Write-Log([string]$msg)  { Add-Content -Path $LogFile -Value $msg -Encoding UTF8 }

function Exit-Error([string]$msg) {
    Write-Host ""
    Write-Host "  [ERROR] $msg"
    Write-Host ""
    Invoke-Cleanup
    exit 1
}

# No Write-Progress bar -- all progress is inline via Write-Host spinner

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
function Stop-HwServer {
    if ($script:HwServerProcess -and -not $script:HwServerProcess.HasExited) {
        try { $null = $script:HwServerProcess.Kill() } catch {}
        try { $null = $script:HwServerProcess.WaitForExit(3000) } catch {}
    }
    $script:HwServerProcess = $null
    Get-Process -Name hw_server -ErrorAction SilentlyContinue |
        ForEach-Object { try { $null = $_.Kill() } catch {} }
}

function Invoke-Cleanup {
    # Reset the board via XSDB before killing hw_server (leaves board in known state)
    if ($script:HwServerProcess -and -not $script:HwServerProcess.HasExited) {
        try {
            $resetTcl = Join-Path ([System.IO.Path]::GetTempPath()) "xsdb_reset.tcl"
            $resetBat = Join-Path ([System.IO.Path]::GetTempPath()) "xsdb_reset.bat"
            $resetOut = Join-Path ([System.IO.Path]::GetTempPath()) "xsdb_reset.out"
            [System.IO.File]::WriteAllText($resetTcl,
                "connect -url TCP:localhost:$XsdbPort`ntarget -set -filter {name =~ `"$($script:TargetFilter)`"}`ncatch { dev reset }`nexit`n",
                [System.Text.Encoding]::ASCII)
            [System.IO.File]::WriteAllText($resetBat,
                "@echo off`r`ncall `"$xsdbBat`" `"$resetTcl`" > `"$resetOut`" 2>&1`r`n",
                [System.Text.Encoding]::ASCII)
            $rp = [System.Diagnostics.Process]::Start(
                [System.Diagnostics.ProcessStartInfo]@{
                    FileName='cmd.exe'; Arguments="/c `"$resetBat`""
                    UseShellExecute=$false; CreateNoWindow=$true })
            $null = $rp.WaitForExit(10000)
            foreach ($f in $resetTcl,$resetBat,$resetOut) { Remove-Item $f -Force -ErrorAction SilentlyContinue | Out-Null }
            Write-Info "Board reset via XSDB."
        } catch {
            Write-Warn "Could not reset board: $_"
        }
    }
    Stop-HwServer
    if ($script:SerialPort_obj -and $script:SerialPort_obj.IsOpen) {
        try { $script:SerialPort_obj.Close() } catch {}
        $script:SerialPort_obj = $null
    }
    if ($script:TmpTcl -ne "" -and (Test-Path $script:TmpTcl)) {
        try { Remove-Item $script:TmpTcl -Force } catch {}
        $script:TmpTcl = ""
    }
    if (Test-Path $LogFile) {
        try {
            $raw   = [System.IO.File]::ReadAllText($LogFile)
            $clean = $raw -replace "`r","" -replace "\x1b\[[0-9;]*[a-zA-Z]","" -replace "\x08",""
            [System.IO.File]::WriteAllText($LogFile, $clean, [System.Text.Encoding]::UTF8)
        } catch {}
    }
}

try { [Console]::TreatControlCAsInput = $false } catch {}
$null = Register-EngineEvent -SourceIdentifier ([System.Management.Automation.PsEngineEvent]::Exiting) -Action { Invoke-Cleanup }

# ---------------------------------------------------------------------------
# Find a free TCP port for hw_server
# ---------------------------------------------------------------------------
function Find-FreePort([int]$Preferred = 3121) {
    $candidates = @($Preferred) + (3202..3240) + (3342..3389) + (4000..4100)
    foreach ($p in $candidates) {
        try {
            $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $p)
            $l.Start()
            $l.Stop()
            return $p
        } catch {}
    }
    try {
        $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
        $l.Start()
        $p = $l.LocalEndpoint.Port
        $l.Stop()
        return $p
    } catch {}
    throw "Could not find any free TCP port for hw_server"
}

# ---------------------------------------------------------------------------
# hw_server launcher
# ---------------------------------------------------------------------------
function Start-HwServer {
    param([string]$HwServerExe, [int]$Port)

    $appRoot = Split-Path (Split-Path (Split-Path $HwServerExe))
    $libDir  = Join-Path $appRoot "lib\win64.o"

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName  = $HwServerExe
    $psi.Arguments = "-s tcp::$Port"
    $psi.UseShellExecute  = $false
    $psi.CreateNoWindow   = $true
    $psi.EnvironmentVariables["PATH"] = "$libDir;" + $psi.EnvironmentVariables["PATH"]

    $proc = [System.Diagnostics.Process]::Start($psi)
    $script:HwServerProcess = $proc

    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $ready    = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 300
        try {
            $tcp = [System.Net.Sockets.TcpClient]::new()
            $tcp.Connect('127.0.0.1', $Port)
            $tcp.Close()
            $ready = $true
            break
        } catch {}
    }
    if (-not $ready) {
        try { $proc.Kill() } catch {}
        throw "hw_server did not start on port $Port within 15s"
    }
    return $proc
}

# ---------------------------------------------------------------------------
# XSDB batch runner
# ---------------------------------------------------------------------------
function Invoke-XsdbScript {
    param(
        [string]$XsdbBat,
        [string]$TclScript,
        [int]   $TimeoutSec = 120,
        [string]$Description = "XSDB",
        [System.IO.Ports.SerialPort]$KeepAlivePort = $null
    )

    $base    = [System.IO.Path]::GetTempPath()
    $stamp   = [DateTime]::UtcNow.ToString("yyyyMMddHHmmssfff")
    $tclFile = Join-Path $base "xsdb_${stamp}.tcl"
    $outFile = Join-Path $base "xsdb_${stamp}.out"
    $batFile = Join-Path $base "xsdb_${stamp}.bat"
    $script:TmpTcl = $tclFile

    [System.IO.File]::WriteAllText($tclFile, $TclScript, [System.Text.Encoding]::ASCII)

    $bat = "@echo off`r`ncall `"$XsdbBat`" `"$tclFile`" > `"$outFile`" 2>&1`r`n"
    [System.IO.File]::WriteAllText($batFile, $bat, [System.Text.Encoding]::ASCII)

    $psi = [System.Diagnostics.ProcessStartInfo]::new('cmd.exe', "/c `"$batFile`"")
    $psi.UseShellExecute  = $false
    $psi.CreateNoWindow   = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    $sw   = [System.Diagnostics.Stopwatch]::StartNew()
    $si   = 0

    while (-not $proc.WaitForExit(500)) {
        $elapsed = [int]$sw.Elapsed.TotalSeconds
        if ($elapsed -gt $TimeoutSec) {
            try { $proc.Kill() } catch {}
            foreach ($f in $tclFile,$outFile,$batFile) { try { Remove-Item $f -Force } catch {} }
            $script:TmpTcl = ""
            Exit-Error "$Description timed out after ${TimeoutSec}s."
        }
        Write-Host "`r  $Description  $($script:Spin[$si % 4])  ${elapsed}s  " -NoNewline
        $si++
        # Drain serial port to keep the connection alive during long XSDB operations
        if ($KeepAlivePort -and $KeepAlivePort.IsOpen) {
            try { $null = $KeepAlivePort.ReadExisting() } catch {}
        }
    }
    Write-Host ""
    $null = $proc.WaitForExit()
    $exitCode = $proc.ExitCode

    $output = ""
    for ($r = 0; $r -lt 10; $r++) {
        Start-Sleep -Milliseconds 200
        if (Test-Path $outFile) {
            $output = [System.IO.File]::ReadAllText($outFile)
            if ($output.Length -gt 0) { break }
        }
    }

    foreach ($f in $tclFile,$batFile,$outFile) { try { Remove-Item $f -Force } catch {} }
    $script:TmpTcl = ""

    Write-Log "=== $Description (exit $exitCode) ==="
    Write-Log $output

    if ($exitCode -ne 0) {
        # Extract specific error from TCL catch blocks (XSDB_ERROR: ...)
        $xsdbErr = ($output -split "`n") |
            Where-Object { $_ -imatch 'XSDB_ERROR:' } |
            Select-Object -First 1
        if (-not $xsdbErr) {
            $xsdbErr = ($output -split "`n") |
                Where-Object { $_ -imatch 'error|failed|cannot' } |
                Select-Object -First 1
        }
        # Identify which step failed from the last XSDB_STEP line
        $lastStep = ($output -split "`n") |
            Where-Object { $_ -imatch 'XSDB_STEP:' } |
            Select-Object -Last 1
        $stepInfo = if ($lastStep) { "  Last step: $($lastStep.Trim())" } else { "" }
        Exit-Error "$Description failed (exit $exitCode).`n           $xsdbErr`n          $stepInfo`n           See log: $LogFile"
    }

    return $output
}

# ---------------------------------------------------------------------------
# Serial port helpers
# ---------------------------------------------------------------------------
function Read-SerialUntil {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string[]]$Patterns,
        [int]$TimeoutSec = 30,
        [string]$Context = "serial",
        [scriptblock]$OnTick = $null
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    $buf      = [System.Text.StringBuilder]::new()
    $lastTick = [DateTime]::UtcNow

    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $chunk = $Port.ReadExisting()
            if ($chunk.Length -gt 0) {
                $null = $buf.Append($chunk)
                Write-Log $chunk
                $text = $buf.ToString()
                foreach ($pat in $Patterns) {
                    if ($text -match $pat) { return $text }
                }
            }
        } catch [System.TimeoutException] {}

        if ($OnTick -and ([DateTime]::UtcNow - $lastTick).TotalMilliseconds -ge 500) {
            $elapsed = ([DateTime]::UtcNow - $deadline.AddSeconds(-$TimeoutSec)).TotalSeconds
            & $OnTick ([int]$elapsed) $TimeoutSec
            $lastTick = [DateTime]::UtcNow
        }
        Start-Sleep -Milliseconds 50
    }
    throw "Timed out after ${TimeoutSec}s waiting for [$($Patterns -join '|')] on $Context"
}

function Invoke-UbootCmd {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string]$Cmd,
        [int]$TimeoutSec = 60
    )
    if ($ShowSerial) { Write-Host "  UBOOT> $Cmd" }
    $Port.WriteLine($Cmd)
    Write-Log "UBOOT> $Cmd"
    $resp = Read-SerialUntil -Port $Port -Patterns @($script:UbootPromptRx) `
        -TimeoutSec $TimeoutSec -Context "U-Boot '$Cmd'"
    if ($ShowSerial) {
        foreach ($line in ($resp -split "`n")) {
            $trimmed = $line.Trim()
            if ($trimmed -ne "" -and $trimmed -ne $Cmd -and $trimmed -notmatch '^\s*$') {
                Write-Host "  $trimmed"
            }
        }
    }
    return $resp
}

function Invoke-UbootFlashCmd {
    param(
        [System.IO.Ports.SerialPort]$Port,
        [string]$Cmd,
        [string]$Label,
        [int]$TimeoutSec = 600
    )
    $Port.WriteLine($Cmd)
    Write-Log "UBOOT> $Cmd"

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
    $buf      = [System.Text.StringBuilder]::new()
    $lastPct  = 0
    $si       = 0

    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $chunk = $Port.ReadExisting()
            if ($chunk.Length -gt 0) {
                $null = $buf.Append($chunk)
                Write-Log $chunk
                $text = $buf.ToString()

                if ($text -match $script:UbootPromptRx) {
                    return $text
                }
                if ($text -match '(?m)--\s+(\d+)%\s+complete') {
                    $pct = [int]$Matches[1]
                    if ($pct -gt $lastPct) { $lastPct = $pct }
                }
            }
        } catch [System.TimeoutException] {}

        $elapsed = [int]([DateTime]::UtcNow - $deadline.AddSeconds(-$TimeoutSec)).TotalSeconds
        Write-Host "`r  $Label  $($script:Spin[$si % 4])  $elapsed`s elapsed  " -NoNewline
        $si++
        Start-Sleep -Milliseconds 100
    }
    Write-Host ""
    throw "Timed out after ${TimeoutSec}s on U-Boot command: $Cmd"
}

# ---------------------------------------------------------------------------
# Auto-detect serial port (FT4232H port B = VEK385 console)
# ---------------------------------------------------------------------------
function Find-SerialPort {
    $consolePorts = @()
    $allFtdiPorts = @()
    try {
        foreach ($dev in (Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue)) {
            if ($dev.FriendlyName -notmatch '\(COM(\d+)\)') { continue }
            $comPort = "COM$($Matches[1])"; $comNum = [int]$Matches[1]
            $id = $dev.InstanceId
            if ($id -imatch 'VEK385') { if ($id -imatch '\\0001$' -or $id -imatch 'B\\') { return $comPort } }
            if ($id -imatch 'VID_0403.*PID_6011') {
                $allFtdiPorts += [PSCustomObject]@{ Port=$comPort; Num=$comNum; Id=$id }
                if ($id -imatch '\+[A-Z0-9]+B\\') {
                    $consolePorts += [PSCustomObject]@{ Port=$comPort; Num=$comNum }
                }
            } elseif ($id -imatch 'VID_0403') {
                $allFtdiPorts += [PSCustomObject]@{ Port=$comPort; Num=$comNum; Id=$id }
            }
        }
    } catch { Write-Warn "PnP query failed: $_" }

    if ($consolePorts.Count -gt 0) {
        $best = ($consolePorts | Sort-Object Num)[0].Port
        Write-Info "Auto-detected VEK385 console (FT4232H port B): $best"
        return $best
    }
    if ($allFtdiPorts.Count -gt 0) {
        $best = ($allFtdiPorts | Sort-Object Num)[0].Port
        Write-Warn "Using lowest FTDI port: $best  (use -SerialPort to override)"
        return $best
    }
    return ""
}

# ---------------------------------------------------------------------------
# Validate / normalize arguments
# ---------------------------------------------------------------------------
$VivadoDir  = $VivadoDir.Trim().Trim('"').Trim("'").TrimEnd('\','/')
$BootImages = $BootImages.Trim().Trim('"').Trim("'").TrimEnd('\','/')

if ($VivadoDir -eq "") {
    if ($env:VIVADO_INSTALL_DIR) {
        $VivadoDir = $env:VIVADO_INSTALL_DIR.Trim().Trim('"').Trim("'").TrimEnd('\','/')
    } else {
        Write-Host "ERROR: -VivadoDir <path> required (or set VIVADO_INSTALL_DIR)."; exit 1
    }
}

$xsdbBat = Join-Path $VivadoDir "Vitis\bin\xsdb.bat"
if (-not (Test-Path $xsdbBat)) { $xsdbBat = Join-Path $VivadoDir "bin\xsdb.bat" }
if (-not (Test-Path $xsdbBat)) {
    Write-Host "ERROR: Cannot find xsdb.bat under $VivadoDir"
    exit 1
}

$hwServerExe = Join-Path $VivadoDir "bin\unwrapped\win64.o\hw_server.exe"
if (-not (Test-Path $hwServerExe)) {
    Write-Host "ERROR: Cannot find hw_server.exe at $hwServerExe"; exit 1
}

if ($BootImages -eq "") { Write-Host "ERROR: -BootImages <path> required."; exit 1 }
if (-not (Test-Path $BootImages -PathType Container)) {
    Write-Host "ERROR: Boot images directory not found: $BootImages"; exit 1
}
$BootImages = (Resolve-Path $BootImages).Path

$bootBinPath = Join-Path $BootImages $BootBin
if (-not (Test-Path $bootBinPath)) { Write-Host "ERROR: $BootBin not found in $BootImages"; exit 1 }

if ($OspiImage -eq "") {
    $cands = @(Get-ChildItem -Path $BootImages -Filter "*ospi*.bin" -File)
    if     ($cands.Count -eq 1) { $OspiImage = $cands[0].Name; Write-Info "Auto-detected OSPI image: $OspiImage" }
    elseif ($cands.Count -gt 1) { Write-Host "ERROR: Multiple *ospi*.bin in $BootImages -- use -OspiImage."; exit 1 }
    else                        { Write-Host "ERROR: No *ospi*.bin found in $BootImages -- use -OspiImage."; exit 1 }
}
$ospiImagePath = Join-Path $BootImages $OspiImage
if (-not (Test-Path $ospiImagePath)) { Write-Host "ERROR: $OspiImage not found in $BootImages"; exit 1 }

$fileBytes    = (Get-Item $ospiImagePath).Length
if ($fileBytes -eq 0)                { Write-Host "ERROR: $OspiImage is empty."; exit 1 }
if ($fileBytes -gt $script:FlashMax) { Write-Host "ERROR: $OspiImage exceeds 256 MB flash."; exit 1 }
$flashSize    = [long]([Math]::Ceiling($fileBytes / $script:EraseBlock) * $script:EraseBlock)
$flashSizeHex = "0x{0:X}" -f $flashSize
$flashSizeMb  = "{0:F1}" -f ($flashSize / 1MB)
Write-Info "OSPI image size: $flashSizeHex [$flashSizeMb MB]"

$availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()
if ($SerialPort -eq "") {
    $SerialPort = Find-SerialPort
    if ($SerialPort -eq "") {
        $portList = if ($availablePorts.Count -gt 0) { $availablePorts -join ', ' } else { "(none)" }
        if ($DryRun) {
            Write-Warn "Cannot auto-detect serial port. Available: $portList"
            $SerialPort = "(not set -- dry-run only)"
        } else {
            Write-Host "ERROR: Cannot auto-detect serial port. Available: $portList"
            Write-Host "       Use -SerialPort COM<n>."; exit 1
        }
    } else {
        Write-Info "Auto-detected serial port: $SerialPort"
    }
}
if (-not $DryRun -and $SerialPort -notin $availablePorts) {
    Write-Host "ERROR: $SerialPort not found. Available: $($availablePorts -join ', ')"; exit 1
}

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "========================================================================"
Write-Host "  VEK385 OSPI Flash Tool  (Windows)"
Write-Host "  Automates UG1787 (Board Setup)"
Write-Host "========================================================================"
Write-Host ""
Write-Host "  Vivado:       $VivadoDir"
Write-Host "  Boot images:  $BootImages"
Write-Host "  BOOT.bin:     $BootBin"
Write-Host "  OSPI image:   $OspiImage"
Write-Host "  Image size:   $flashSizeHex [$flashSizeMb MB]"
Write-Host "  Serial port:  $SerialPort"
Write-Host "  XSDB port:    $XsdbPort"
Write-Host "  Log file:     $LogFile"
if ($DryRun) { Write-Host "  Mode:         DRY RUN (no hardware operations)" }

if ($DryRun) {
    Write-Step "Dry run complete -- all paths validated"
    Write-Host ""; Write-Host "  Re-run without -DryRun to flash."; Write-Host ""; exit 0
}

"VEK385 OSPI Flash Log - $(Get-Date)" | Set-Content -Path $LogFile -Encoding UTF8

# ---------------------------------------------------------------------------
# Pre-flight: kill stale processes from a previous crashed run
# ---------------------------------------------------------------------------
$stale = @(Get-Process | Where-Object { $_.ProcessName -imatch '^xsdb$|^hw_server$|^cs_server$|^hw_tls$' })
if ($stale.Count -gt 0) {
    Write-Warn "Killing $($stale.Count) stale XSDB/hw_server process(es)..."
    $stale | ForEach-Object {
        Write-Warn "  -> $($_.ProcessName) PID $($_.Id)"
        try { Stop-Process -Id $_.Id -Force } catch {}
    }
    Start-Sleep -Milliseconds 1500
}

$hwPort = Find-FreePort -Preferred ([int]$XsdbPort)
if ($hwPort -ne [int]$XsdbPort) {
    Write-Warn "Port $XsdbPort is reserved -- using port $hwPort instead."
    $XsdbPort = "$hwPort"
} else {
    Write-Info "hw_server port $XsdbPort is free."
}

# ---------------------------------------------------------------------------
# Step 0: User confirmation
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "  Ensure SW1 DIP switch is set to JTAG mode (0000 = all ON) and board is powered ON."
Write-Host -NoNewline "  Press Enter to continue... "
$null = Read-Host

# ===========================================================================
# Main execution wrapped in try/finally for reliable Ctrl-C cleanup
# ===========================================================================
try {

# ---------------------------------------------------------------------------
# Step 1: Open serial port
# ---------------------------------------------------------------------------
Write-Step "Step 1: Opening serial port $SerialPort"

$script:SerialPort_obj = [System.IO.Ports.SerialPort]::new($SerialPort, 115200, 'None', 8, 'One')
$script:SerialPort_obj.ReadTimeout  = 200
$script:SerialPort_obj.WriteTimeout = 5000
$script:SerialPort_obj.NewLine      = "`r`n"
try {
    $script:SerialPort_obj.Open()
} catch {
    Exit-Error "Cannot open ${SerialPort}: $_`n`n       Close any terminal (PuTTY, TeraTerm) holding the port."
}
Write-Info "Serial port opened: 115200 8N1"

# ---------------------------------------------------------------------------
# Step 2: XSDB -- connect, reset, download BOOT.bin
# ---------------------------------------------------------------------------
Write-Step "Step 2: Connecting to board and downloading $BootBin via XSDB/JTAG"

Write-Info "Starting hw_server on port $XsdbPort..."
try {
    $null = Start-HwServer -HwServerExe $hwServerExe -Port ([int]$XsdbPort)
} catch {
    Exit-Error "hw_server failed to start: $_"
}
Write-Info "hw_server running on port $XsdbPort."

$bootBinTcl = ($bootBinPath -replace '\\','/') -replace "'","\\'"

$tclConnect = @"
puts "XSDB_STEP: connect"
if {[catch {connect -url TCP:localhost:$XsdbPort} err]} {
    puts "XSDB_ERROR: connect failed: `$err"
    exit 1
}

puts "XSDB_STEP: target"
if {[catch {target -set -filter {name =~ "$($script:TargetFilter)"}} err]} {
    puts "XSDB_ERROR: target not found: `$err"
    exit 2
}

puts "XSDB_STEP: reset"
catch { dev reset }
after 1000

puts "XSDB_STEP: download_boot"
if {[catch {dev p "$bootBinTcl"} err]} {
    puts "XSDB_ERROR: BOOT.bin download failed: `$err"
    exit 3
}

puts "XSDB_STEP: boot_done"
exit 0
"@

Write-Info "Running XSDB batch (connect + reset + BOOT.bin download)..."
$xsdbOut1 = Invoke-XsdbScript -XsdbBat $xsdbBat -TclScript $tclConnect `
    -TimeoutSec ($script:TimeoutBoot + 60) -Description "XSDB connect+BOOT.bin" `
    -KeepAlivePort $script:SerialPort_obj

if ($xsdbOut1 -notmatch 'XSDB_STEP: boot_done|Boot PDI Load: Done|PLM Boot Time|Successfully downloaded') {
    Exit-Error "BOOT.bin download did not complete.`n           See log: $LogFile"
}
Write-Info "BOOT.bin downloaded successfully."

# ---------------------------------------------------------------------------
# Step 3: Wait for U-Boot prompt on serial
# ---------------------------------------------------------------------------
Write-Step "Step 3: Waiting for U-Boot prompt on serial"
$null = $script:SerialPort_obj.DiscardInBuffer()
Write-Info "Monitoring serial for U-Boot prompt (timeout: $($script:TimeoutBoot)s)..."

$deadline  = [DateTime]::UtcNow.AddSeconds($script:TimeoutBoot)
$buf       = [System.Text.StringBuilder]::new()
$gotPrompt = $false
$lastTick  = [DateTime]::UtcNow
$si        = 0

while ([DateTime]::UtcNow -lt $deadline) {
    try {
        $chunk = $script:SerialPort_obj.ReadExisting()
        if ($chunk.Length -gt 0) {
            $null = $buf.Append($chunk)
            Write-Log $chunk
            if ($ShowSerial) { Write-Host -NoNewline $chunk }
            $text = $buf.ToString()

            if ($text -match $script:UbootPromptRx) {
                Write-Info "U-Boot prompt detected."
                $gotPrompt = $true; break
            }
            if ($text -imatch 'Missing (RNG|TPM)|Cannot persist EFI|EFI variables|Hit any key to stop autoboot') {
                Write-Info "Near autoboot -- sending pre-emptive keystrokes..."
                for ($a = 0; $a -lt 50 -and -not $gotPrompt; $a++) {
                    $script:SerialPort_obj.Write(" ")
                    Start-Sleep -Milliseconds 100
                    $extra = $script:SerialPort_obj.ReadExisting()
                    if ($extra.Length -gt 0) {
                        $null = $buf.Append($extra); Write-Log $extra
                        if ($buf.ToString() -match $script:UbootPromptRx) {
                            Write-Info "U-Boot prompt (autoboot interrupted)."
                            $gotPrompt = $true
                        }
                    }
                }
                if (-not $gotPrompt) { Exit-Error "Failed to interrupt U-Boot autoboot." }
                break
            }
        }
    } catch [System.TimeoutException] {}

    if (([DateTime]::UtcNow - $lastTick).TotalMilliseconds -ge 500) {
        $e = [int]([DateTime]::UtcNow - $deadline.AddSeconds(-$script:TimeoutBoot)).TotalSeconds
        Write-Host "`r  Waiting for U-Boot  $($script:Spin[$si % 4])  ${e}s  " -NoNewline
        $si++
        $lastTick = [DateTime]::UtcNow
    }
    Start-Sleep -Milliseconds 100
}

if (-not $gotPrompt) {
    Exit-Error ("Timed out waiting for U-Boot on $SerialPort.`n" +
                "           - Board powered on?  SW1 = JTAG (0000)?  Correct COM port?")
}

# ---------------------------------------------------------------------------
# Step 4: XSDB -- download OSPI image to DDR
# ---------------------------------------------------------------------------
Write-Step "Step 4: Downloading OSPI image to DDR via XSDB"

# Close serial port before long XSDB download to prevent stale connection.
# Will reopen with a fresh connection after download completes.
Write-Info "Closing serial port for XSDB download..."
if ($script:SerialPort_obj -and $script:SerialPort_obj.IsOpen) {
    $script:SerialPort_obj.Close()
}

$ospiTcl = ($ospiImagePath -replace '\\','/') -replace "'","\\'"
$dowTimeout = [Math]::Max($script:TimeoutDow, [int]($fileBytes / 500000.0) + 60)

$tclDownload = @"
puts "XSDB_STEP: connect"
if {[catch {connect -url TCP:localhost:$XsdbPort} err]} {
    puts "XSDB_ERROR: reconnect failed: `$err"
    exit 1
}

puts "XSDB_STEP: target"
if {[catch {target -set -filter {name =~ "$($script:TargetFilter)"}} err]} {
    puts "XSDB_ERROR: target not found: `$err"
    exit 2
}

puts "XSDB_STEP: download_ospi"
if {[catch {dow -data "$ospiTcl" 0x20000000} err]} {
    puts "XSDB_ERROR: OSPI download failed: `$err"
    exit 3
}

puts "XSDB_STEP: ospi_done"
exit 0
"@

Write-Info "Downloading $OspiImage to 0x20000000 (timeout ${dowTimeout}s)..."
$xsdbOut2 = Invoke-XsdbScript -XsdbBat $xsdbBat -TclScript $tclDownload `
    -TimeoutSec $dowTimeout -Description "XSDB download OSPI"

if ($xsdbOut2 -notmatch 'XSDB_STEP: ospi_done|Successfully downloaded') {
    Exit-Error "OSPI download did not complete.`n           See log: $LogFile"
}
Write-Info "OSPI image in DDR at 0x20000000."

# ---------------------------------------------------------------------------
# Step 5-7: Flash via U-Boot
# ---------------------------------------------------------------------------
Write-Step "Step 5: Flashing OSPI via U-Boot"

# Reopen serial port with a fresh connection after XSDB download
Write-Info "Reopening serial port $SerialPort..."
$script:SerialPort_obj = [System.IO.Ports.SerialPort]::new($SerialPort, 115200, 'None', 8, 'One')
$script:SerialPort_obj.ReadTimeout  = 200
$script:SerialPort_obj.WriteTimeout = 5000
$script:SerialPort_obj.NewLine      = "`r`n"
try {
    $script:SerialPort_obj.Open()
    $null = $script:SerialPort_obj.DiscardInBuffer()
    Start-Sleep -Milliseconds 500
    # Verify U-Boot is responsive
    $script:SerialPort_obj.WriteLine("")
    Start-Sleep -Milliseconds 500
    $check = $script:SerialPort_obj.ReadExisting()
    if ($check -match $script:UbootPromptRx) {
        Write-Info "U-Boot prompt confirmed."
    } else {
        Write-Warn "U-Boot prompt not detected -- proceeding anyway."
    }
    $null = $script:SerialPort_obj.DiscardInBuffer()
} catch {
    Exit-Error "Cannot reopen ${SerialPort}: $_"
}

# sf probe
Write-Info "Probing SPI flash..."
$probeResp = Invoke-UbootCmd -Port $script:SerialPort_obj -Cmd "sf probe 0 0 0" -TimeoutSec $script:TimeoutSf
if ($probeResp -notmatch 'SF: Detected') {
    Exit-Error "SPI flash not detected.`n           sf probe output: $($probeResp.Trim())"
}
Write-Info "SPI flash detected."

# Dismiss the top progress bar -- sf erase/write use inline progress only
# sf erase/write use inline spinner below

# sf erase
Write-Info "Erasing full SPI flash (256 MB) -- this may take several minutes..."
$eraseResp = Invoke-UbootFlashCmd -Port $script:SerialPort_obj `
    -Cmd "sf erase 0x0 0x10000000" -Label "Erasing" -TimeoutSec $script:TimeoutFlash
if ($eraseResp -notmatch 'Erased:?\s*OK') {
    Exit-Error "Erase did not confirm OK.`n           Output: $($eraseResp.Trim())"
}
$nb = if ($eraseResp -match '(\d+)\s+bytes') { $Matches[1] } else { "?" }
Write-Info "Flash erased: $nb bytes."

# sf write
$estWriteSec = [Math]::Max(5, [int]($flashSize / 1MB))
Write-Info "Writing $OspiImage [$flashSizeMb MB, ~${estWriteSec}s]..."
$writeResp = Invoke-UbootFlashCmd -Port $script:SerialPort_obj `
    -Cmd "sf write 0x20000000 0x0 $flashSizeHex" -Label "Writing" -TimeoutSec $script:TimeoutFlash
if ($writeResp -notmatch 'Written:?\s*OK') {
    Exit-Error "Write did not confirm OK.`n           Output: $($writeResp.Trim())"
}
$nb = if ($writeResp -match '(\d+)\s+bytes') { $Matches[1] } else { "?" }
Write-Info "Flash written: $nb bytes."

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Step "OSPI programming complete!"
Write-Info "Cleaning up (resetting board, closing connections)..."

} finally {
    Invoke-Cleanup
}

Write-Host ""
Write-Host "  Log file: $LogFile"
Write-Host ""
Write-Host "  Next steps:"
Write-Host "    1. Power OFF the board"
Write-Host '    2. Set SW1 DIP switch to OSPI mode (0001 = ON, ON, ON, OFF)'
Write-Host "    3. Power ON the board"
Write-Host ""
Write-Host "  Then run vek385-flash-sdcard.ps1 to prepare the SD card."
Write-Host ""

exit 0
