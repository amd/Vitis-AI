# vek385-flash-ospi.ps1 -- OSPI Flash Tool (Windows)

Automates UG1787 (Board Setup) for VEK385 Rev-A/Rev-B on Windows 11.

## Prerequisites

**Software:**
- Windows 11 (PowerShell 5.1+ is built-in)
- Vivado Lab 2026.1+ (provides XSDB and hw_server)
- Run PowerShell as **Administrator**

No additional software installation is required -- no WSL2, no third-party tools.

**Note:** If PowerShell blocks script execution, run this once in your session:
```powershell
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
```

**Hardware:**
- VEK385 Rev-A or Rev-B board connected to host via USB cable (provides both JTAG and serial)
- Board DIP switch SW1 set to JTAG mode (0000 = all ON)
- Board powered on
- Close any serial terminals (PuTTY, TeraTerm) before running -- the script needs exclusive serial port access

## Usage

```
Usage: .\vek385-flash-ospi.ps1 -VivadoDir <path> -BootImages <path> [options]

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
```

### Examples

```powershell
# Show help
.\vek385-flash-ospi.ps1 -Help

# Dry run -- validate everything without touching hardware
.\vek385-flash-ospi.ps1 -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -BootImages "C:\vek385\boot_images" -DryRun

# Full run
.\vek385-flash-ospi.ps1 -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -BootImages "C:\vek385\boot_images"

# Full run with env var
$env:VIVADO_INSTALL_DIR = "C:\AMDDesignTools\2026.1\Vivado_Lab"
.\vek385-flash-ospi.ps1 -BootImages "C:\vek385\boot_images"

# Explicit serial port and specific OSPI image
.\vek385-flash-ospi.ps1 -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -BootImages "C:\vek385\boot_images" -SerialPort COM6 -OspiImage "BOOT.bin"

# Full run with serial output echoed to console (debug)
.\vek385-flash-ospi.ps1 -VivadoDir "C:\AMDDesignTools\2026.1\Vivado_Lab" -BootImages "C:\vek385\boot_images" -ShowSerial
```

## How It Works

The script manages two independent communication channels to the board:

- **XSDB (JTAG)** -- run as batch TCL scripts via xsdb.bat, used to push files into board DDR memory
- **Serial (.NET SerialPort)** -- opened directly, gives access to the U-Boot console

XSDB is driven in batch mode because Windows' `xsdb.bat` chain uses `endlocal` which destroys pipe handles inherited from PowerShell. The fix: write TCL commands to a temp file, run via a wrapper .bat that redirects output to a file, read the output after completion.

The serial port is closed before the long OSPI image download and reopened after with a fresh connection. This prevents the .NET SerialPort from going stale during the multi-minute XSDB transfer.

An explicit hw_server is launched before XSDB because WSL2/Hyper-V silently reserves TCP port ranges (commonly 3102-3201). The script probes for a free port by actually binding a TcpListener, then launches hw_server on that port.

### Step-by-Step Flow

```
         Windows Host                                VEK385 Board
         ------------                                ------------
         PowerShell launches
         hw_server.exe (free port)
              |
Step 2   XSDB batch #1 ---- JTAG/USB ---------> connect, reset, download BOOT.bin
              |                                         |
Step 3        |     .NET SerialPort (open) <---------- "versal2>" U-Boot prompt
              |     Interrupt autoboot                  |
              |     CLOSE serial port                   |
              |                                         |
Step 4   XSDB batch #2 ---- JTAG/USB ---------> download OSPI image to DDR
              |                                  (serial closed -- no stale connection)
              |     REOPEN serial port (fresh) <------ U-Boot still alive
              |                                         |
Step 5        |     SerialPort ----------------> sf probe -> sf erase -> sf write
              |                                         |
                                                  OSPI programmed
```

#### Pre-flight validation (before touching hardware)

1. Parse arguments, validate `-VivadoDir` and `-BootImages` exist
2. Locate `xsdb.bat` and `hw_server.exe` under Vivado install
3. Auto-detect OSPI image by globbing `*ospi*.bin` in boot_images directory
4. Validate image size (reject empty files or files exceeding 256 MB flash capacity). Compute write size rounded up to the 64 KB erase block boundary.
5. Auto-detect serial port using Windows PnP device query -- matches FTDI FT4232H (VID 0403, PID 6011) port B as the VEK385 console. Falls back to listing all COM ports. Override with `-SerialPort`.
6. Check serial port is available (catches open error, suggests closing PuTTY/TeraTerm)
7. Kill any stale xsdb/hw_server processes from a previous crashed run
8. Probe for a free TCP port for hw_server (avoids Hyper-V port reservation conflicts)
9. If `-DryRun`, print summary and exit

#### User prompt

Script pauses: *"Ensure SW1 DIP switch is set to JTAG mode (0000 = all ON) and board is powered ON. Press Enter to continue..."*

#### Step 1: Open serial port

Opens auto-detected serial port (or `-SerialPort` override) at 115200 baud, 8N1, via .NET `System.IO.Ports.SerialPort`. This is the U-Boot console channel. Nothing is sent yet -- the script just confirms the port can be opened. If the port is held by another program, exits with error.

#### Step 2: XSDB -- connect, reset, and download BOOT.bin

Launches hw_server on the probed-free port. Writes a TCL script to a temp file containing:

```
connect -url TCP:localhost:<port>
target -set -filter {name =~ "*xc2ve*"}
dev reset
dev p "<path>/BOOT.bin"
```

Each command is wrapped in a `catch` block with specific exit codes. If any command fails, the TCL script exits immediately with a descriptive error message.

`dev reset` is always run before `dev p` to ensure the device is in a known state. Without this, re-programming after a previous run can hang because the device may still be running U-Boot or Linux.

`dev p BOOT.bin` downloads the bootloader into board DDR via JTAG and executes it. The board runs PLM -> TF-A -> U-Boot. Boot messages start appearing on the serial console.

An inline spinner shows elapsed time during the download.

#### Step 3: Wait for U-Boot prompt on serial

Watches the serial port for:

- `versal2>` or `=>` -- U-Boot prompt (board is ready)
- `Missing TPM`, `Cannot persist EFI`, or `Hit any key to stop autoboot` -- sends pre-emptive keystrokes to interrupt autoboot before the 5-second countdown expires

Timeout: 120 seconds. If neither appears, exits with diagnostic hints (check power, DIP switch, serial port).

#### Step 4: XSDB -- download OSPI image to DDR

**Closes the serial port** before starting the download to prevent the .NET connection from going stale during the multi-minute transfer.

Writes a second TCL script:

```
connect -url TCP:localhost:<port>
target -set -filter {name =~ "*xc2ve*"}
dow -data "<path>/ospi_image.bin" 0x20000000
```

This pushes the OSPI image over JTAG/USB into board DDR at address `0x20000000`. The image is now sitting in memory -- nothing has been written to flash yet. U-Boot can see this memory region.

An inline spinner shows elapsed time during the download (~10 seconds for a 5 MB image).

After the download completes, **reopens the serial port** with a fresh connection, sends a newline to verify U-Boot is still responsive, and confirms the prompt.

#### Step 5: U-Boot -- flash OSPI from DDR

Sends three U-Boot commands via the reopened serial port:

```
versal2> sf probe 0 0 0
```
Initializes the SPI flash controller. Script checks output for `SF: Detected`. **Hard failure** if not detected.

```
versal2> sf erase 0x0 0x10000000
```
Erases the full 256 MB (0x10000000 bytes) of OSPI flash. Can take several minutes. Timeout: 600 seconds. An inline spinner shows elapsed time. **Hard failure** if `Erased: OK` not confirmed.

```
versal2> sf write 0x20000000 0x0 <image_size>
```
Copies only the actual image size (rounded up to 64 KB erase block boundary) from DDR to OSPI flash. An inline spinner shows elapsed time. **Hard failure** if `Written: OK` not confirmed.

#### Step 6: Cleanup

- Resets the board via a final XSDB batch (`dev reset`) to leave it in a known state
- Kills hw_server
- Closes serial port
- Sanitizes log file (strips carriage returns, ANSI escape sequences, and backspace characters)
- Prints next steps

## After Completion

```
  1. Power OFF the board
  2. Set SW1 DIP switch to OSPI mode (0001 = ON, ON, ON, OFF)
  3. Power ON the board

  Then run vek385-flash-sdcard.ps1 to prepare the SD card.
```

The board will now boot from OSPI flash independently -- no JTAG required.

## Error Handling

| Scenario | Behavior |
|---|---|
| Vivado/xsdb.bat not found | Exits with path suggestions |
| hw_server won't start | 15s timeout, exits with error |
| Port 3121 reserved by Hyper-V | Probes free port automatically (3202+) |
| Stale xsdb/hw_server from crashed run | Pre-flight process cleanup |
| Serial port held by PuTTY/TeraTerm | Caught on open, suggests closing the terminal |
| XSDB connect fails | Per-command TCL catch, specific exit code and error message |
| XSDB target not found | Per-command TCL catch, exits with "target not found" |
| `dev p BOOT.bin` fails (PLM error) | Exits with error. Fix: power off board for 30 seconds, retry |
| U-Boot prompt not found | 120s timeout with checklist (power, DIP, serial port) |
| Serial port stale after XSDB download | Prevented by close/reopen serial between XSDB sessions |
| `dow -data` times out | Timeout scaled by image size + margin |
| `sf probe` -- flash not detected | Hard failure, exits immediately |
| OSPI image is empty | Rejects 0-byte files before flashing |
| OSPI image exceeds 256 MB | Rejects oversized files before flashing |
| `sf erase` -- no `Erased: OK` | Hard failure, exits with output details |
| `sf write` -- no `Written: OK` | Hard failure, exits with output details |
| `sf erase/write` timeout | 600s timeout |
| Windows Firewall popup for hw_server | User clicks Allow (first run only) |
| Ctrl-C during execution | try/finally cleanup: resets board, closes serial, kills hw_server |

## Log File

A timestamped log file is created in the current directory:

```
.\vek385-flash-ospi_20260722_153330.log
```

XSDB output and serial output are both logged. The log is sanitized on exit to strip carriage returns, ANSI escape sequences, and backspace characters from serial output. Override path with `-LogFile <path>`.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| "Script is not digitally signed" | PowerShell execution policy | Run: `Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process` |
| PLM Error during BOOT.bin download | Board in bad state from previous run | Power off board for 30 seconds, then retry |
| "Cannot open COMx" | Serial terminal (PuTTY/TeraTerm) holding the port | Close the terminal before running the script |
| Windows Firewall popup for hw_server | First-time run, Windows blocks unknown .exe | Click "Allow access" |
| hw_server port conflict | WSL2/Hyper-V reserves port 3121 | Script auto-probes alternate ports (3202+) |
| U-Boot prompt not found after 120s | Board not powered, wrong DIP switch, wrong COM port | Check SW1 = JTAG (0000), verify COM port in Device Manager |
