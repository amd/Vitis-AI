# vek385-flash-ospi.exp -- OSPI Flash Tool

Automates UG1787 (Board Setup) for VEK385 Rev-B.

## Prerequisites

```bash
# expect (required)
sudo apt install expect

# Vivado 2025.2+ (required -- provides XSDB)
# Must already be installed. Note the install path.

# fuser (usually pre-installed, part of psmisc)
which fuser || sudo apt install psmisc
```

**Hardware:**
- VEK385 Rev-B board connected to host via USB cable (provides both JTAG and serial)
- Board DIP switch SW1 set to JTAG mode (0000 = all ON)
- Board powered on

## Usage

```
Usage: vek385-flash-ospi.exp --vivado-dir <path> --boot-images <path> [options]

Required:
  --vivado-dir <path>    Vivado installation directory
                         (or set VIVADO_INSTALL_DIR env var)
  --boot-images <path>   Path to boot_images directory

Options:
  --serial-port <dev>    Serial port device (default: auto-detect)
  --xsdb-port <port>     XSDB port number (default: 3121)
  --ospi-image <file>    OSPI image filename (default: auto-detect *ospi*.bin)
  --boot-bin <file>      Boot binary filename (default: BOOT.bin)
  --log-file <path>      Log file path (default: ./vek385-flash-ospi_<timestamp>.log)
  --dry-run              Validate paths and settings without touching hardware
  --help                 Show this help message
```

### Examples

```bash
# Dry run -- validate everything without touching hardware
./vek385-flash-ospi.exp \
  --vivado-dir /tools/Xilinx/Vivado/2025.2 \
  --boot-images /path/to/boot_images \
  --dry-run

# Full run with env var
export VIVADO_INSTALL_DIR=/tools/Xilinx/Vivado/2025.2
./vek385-flash-ospi.exp --boot-images /path/to/boot_images

# Explicit serial port and custom log location
./vek385-flash-ospi.exp \
  --vivado-dir /tools/Xilinx/Vivado/2025.2 \
  --boot-images /path/to/boot_images \
  --serial-port /dev/ttyUSB3 \
  --log-file /tmp/ospi-flash.log
```

## How It Works

The script manages two independent communication channels to the board:

- **XSDB (JTAG)** -- spawned as a child process, used to push files into board DDR memory
- **Serial (auto-detected)** -- opened directly, gives access to the U-Boot console

Both are orchestrated by the same expect process, switching between them at each step. XSDB and serial logging are separated to prevent interleaved output — `log_file` is disabled during XSDB operations and re-enabled for serial phases, with XSDB output written to the log via a `log_write` helper.

### Step-by-Step Flow

```
         Host (Linux)                         VEK385 Board
         ────────────                         ────────────
Step 2   xsdb launched
              │
Step 3   connect ─────── JTAG/USB ──────────→ hw_server
         dev reset ────── JTAG/USB ──────────→ reset device to clean state
         dev p BOOT.bin ─ JTAG/USB ──────────→ DDR (executes → PLM → U-Boot)
              │                                      │
Step 4        │          serial (auto-detect) ←──── "versal2>" U-Boot prompt
              │                                      │
Step 5   dow -data ospi.bin ── JTAG/USB ────→ DDR @ 0x20000000
              │                                (image sitting in memory)
              │                                      │
Step 6        │          serial ─────────────→ sf probe  (init flash controller)
              │          serial ─────────────→ sf erase  (erase full 256 MB)
              │          serial ─────────────→ sf write  (DDR → OSPI, image size only)
              │                                      │
                                               OSPI now contains bootloader
```

#### Pre-flight validation (before touching hardware)

1. Parse arguments, validate `--vivado-dir` and `--boot-images` exist
2. Locate `settings64.sh` under Vivado install (needed to put `xsdb` on PATH)
3. Auto-detect OSPI image by globbing `*ospi*.bin` in boot_images directory
4. Validate image size (reject empty files or files exceeding 256 MB flash capacity). Compute write size rounded up to the 64 KB erase block boundary.
5. Auto-detect serial port using `udevadm` -- matches `ID_MODEL=VEK385` and `ID_USB_INTERFACE_NUM=01`. Falls back to listing all USB serial ports if VEK385 not found. Override with `--serial-port`.
6. Check serial port is not locked by another process (minicom, screen) via `fuser`
7. If `--dry-run`, print summary and exit

#### User prompt

Script pauses with infinite wait: *"Set SW1 DIP to JTAG (0000 = all ON) and power on board. Press Enter."*

#### Step 1: Open serial port

Opens auto-detected serial port (or `--serial-port` override) at 115200 baud, 8N1, non-blocking. This is the U-Boot console channel. Nothing is sent yet -- the script just starts listening. If the port cannot be opened, exits with error.

#### Step 2: Launch XSDB

Runs `source settings64.sh && xsdb` in a bash shell. Waits for the `xsdb%` prompt to confirm XSDB is ready.

#### Step 3: XSDB -- connect, reset, and download BOOT.bin

Sends these XSDB commands sequentially, waiting for `xsdb%` between each:

```
xsdb% connect                                        # connect to hw_server (JTAG)
xsdb% set port 3121                                  # set debug port
xsdb% target -set -filter {name =~ "*xc2ve3858*"}    # select the Versal chip
xsdb% dev reset                                      # reset device to clean state
xsdb% dev p /path/to/BOOT.bin                        # push BOOT.bin via JTAG and execute
```

`dev reset` is always run before `dev p` to ensure the device is in a known state. Without this, re-programming after a previous run can hang because the device may still be running U-Boot or Linux.

`dev p BOOT.bin` downloads the bootloader into board DDR via JTAG and executes it. The board runs PLM → TF-A → U-Boot. Boot messages start appearing on the serial console.

Each XSDB response is checked for error patterns (`error:`, `no targets found`, `cannot connect`).

#### Step 4: Wait for U-Boot prompt on serial

Switches to the serial channel. Sends a newline and watches for:

- `versal2>` or `=>` -- U-Boot prompt (board is ready)
- `Hit any key to stop autoboot` -- sends Enter to interrupt autoboot

Timeout: 120 seconds. If neither appears, exits with diagnostic hints (check power, DIP switch, serial port).

#### Step 5: XSDB -- download OSPI image to DDR

Switches back to XSDB. Sends:

```
xsdb% dow -data /path/to/edf-ospi-versal2-vek385-sdt-full.bin 0x20000000
```

This pushes the OSPI image over JTAG/USB into board DDR at address `0x20000000`. The image is now sitting in memory -- nothing has been written to flash yet. U-Boot can see this memory region.

After the download, the script flushes any remaining XSDB output before switching to the serial channel.

#### Step 6: U-Boot -- flash OSPI from DDR

Switches to serial channel. Sends three U-Boot commands. The erase and write commands use `uboot_cmd_progress`, which suppresses serial echo and prints a progress indicator (dots every 5 seconds with a label prefix like `erasing ...` or `writing ...`) for clean terminal output during long operations.

```
versal2> sf probe 0 0 0
```
Initializes the SPI flash controller. Script checks output for `SF: Detected`. **Hard failure** if not detected -- script exits.

```
versal2> sf erase 0x0 0x10000000
```
Erases the full 256 MB (0x10000000 bytes) of OSPI flash for a clean state. Can take several minutes. Timeout: 600 seconds. **Hard failure** if `Erased: OK` not confirmed in output.

```
versal2> sf write 0x20000000 0x0 <image_size>
```
Copies only the actual image size (rounded up to 64 KB erase block boundary) from DDR address `0x20000000` to OSPI flash starting at offset `0x0`. For example, a 38.5 MB image writes 0x2690000 instead of the full 256 MB, significantly reducing flash time. Timeout: 600 seconds. **Hard failure** if `Written: OK` not confirmed in output. Extracts and reports byte count.

#### Step 7: Cleanup

- Sends `exit` to XSDB
- Closes serial port via spawn_id (avoids double-close segfault)
- Sanitizes log file (strips `\r`, ANSI escape sequences, and backspace characters from serial output)
- Prints next steps

## After Completion

```
  1. Power OFF the board
  2. Set SW1 DIP switch to OSPI mode (0001 = ON, ON, ON, OFF)
  3. Power ON the board

  Then run vek385-flash-sdcard.sh to prepare the SD card.
```

The board will now boot from OSPI flash independently -- no JTAG required.

## Error Handling

| Scenario | Behavior |
|---|---|
| XSDB won't start | 30s timeout, checks for EOF |
| XSDB connect/target fails | Parses error patterns (`error:`, `no targets found`, `cannot connect`), exits with specific diagnostic |
| `dev p BOOT.bin` fails | Exits with error (device is always reset first via `dev reset`) |
| U-Boot prompt not found | 120s timeout with checklist (power, DIP, serial port) |
| `dow -data` times out | 120s timeout |
| `sf probe` -- flash not detected | Hard failure, exits immediately |
| OSPI image is empty | Rejects 0-byte files before flashing |
| OSPI image exceeds 256 MB | Rejects oversized files before flashing |
| `sf erase` -- no `Erased: OK` | Hard failure, exits with output details |
| `sf write` -- no `Written: OK` | Hard failure, exits with output details |
| `sf erase/write` timeout | 600s timeout (flash operations can take several minutes) |
| Serial port locked | Detects via `fuser`, suggests how to close minicom/screen |
| Serial port open fails | Caught with error message |
| Ctrl-C during execution | Cleanup proc sends `dev reset` to XSDB, then closes XSDB and serial port |

## Log File

A timestamped log file is created in the current directory:

```
./vek385-flash-ospi_20260409_143022.log
```

XSDB and serial output are logged separately to prevent interleaving — XSDB commands are written directly via `log_write`, while serial output is captured via expect's `log_file`. The log is sanitized on exit to strip carriage returns, ANSI escape sequences, and backspace characters from serial output. Override path with `--log-file <path>`.
