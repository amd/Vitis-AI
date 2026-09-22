# DDR bandwidth profiling — VEK385 (Versal 2VE T50)

XSDB Tcl scripts to measure **DDRMC5E** hardware bandwidth on the **VEK385 RevB** board during inference (or any DDR-heavy workload).

Configurable **4 / 8 / 16 ms** hardware windows, automatic `n` from `DDR_FREQ_MHZ`, JTAG poll calibration at `mc_read`, and idle-based auto-stop.

These are directional profiling scripts. Reported GB/s values are approximate. Each CSV value is derived from hardware performance-monitor sampling over a programmed window (`HW_WINDOW_MS` / `INTERVAL_EXP`), read periodically over JTAG. Results can vary with sample window, XSDB/`mrd` latency, poll interval, boot/storage path, and whether the workload is steady or bursty. Treat numbers as approximate, and prefer the same window, host path, and workload setup when comparing runs.

---

## Scripts

| Script | Purpose |
|--------|---------|
| `init_ddrmc_perf_counters.tcl` | One-time setup: unlock NPI, stop scrub, and program DC perf monitors on **5** DDR channels for the selected hardware window (`HW_WINDOW_MS` → `INTERVAL_EXP`). |
| `mc_read_ddr_bandwidth.tcl` | Selects DPC, measures JTAG read time once, then repeatedly reads all channel counters. Each sample reflects bandwidth over the **last completed hardware window**; rows with `TOTAL_BW` above the active threshold are written to CSV. If the board is idle at start, the script **waits** for DDR traffic (it does not exit immediately). After traffic was seen, it **stops** when inference ends (default: **10** consecutive polls at or below the idle threshold). Prints a summary on exit. |

---

## Hardware (VEK385 / T50)

| Item | Value |
|------|--------|
| Device | Versal 2VE T50 Rev B — **ve2-xc2ve3858** |
| DDR channels | **5** (DDR0–DDR4 in NPI order) |
| Default `DDR_FREQ_MHZ` | **1050.42** (must match actual DDR clock for correct GB/s) |
| XSDB target | DPC — `target -set -filter {name =~ "*DPC*"}` (JTAG or OSPI boot) |

**DDRMC5E base addresses (init / counter bases):**

| Channel | Base |
|---------|------|
| DDR0 | `0xF6540000` |
| DDR1 | `0xF6630000` |
| DDR2 | `0xF6B70000` |
| DDR3 | `0xF6EC0000` |
| DDR4 | `0xF6FA0000` |

**Read addresses used by `mc_read`:** `base + 0x1070` (READ), `base + 0x1078` (WRITE).

For **x4 AIE** studies, compare **DDR0–DDR3**; DDR4 (x1) may carry more host/CMA traffic.

---

## Prerequisites

- Board booted to Linux from **OSPI + SD** (recommended). NFS root can add extra DDR traffic and skew bandwidth comparisons; use OSPI+SD for accurate measurements.
- XSDB connected (JTAG).
- Run Tcl from this directory (or `cd` here before `source`) so CSV paths are predictable.

---

## Recommended workflow

### 1. XSDB — connect and init

```tcl
connect
# Optional — must be set before init if you change them:
# set HW_WINDOW_MS 8
# set DDR_FREQ_MHZ 1050.42

source init_ddrmc_perf_counters.tcl
```

`init` prints chosen `INTERVAL_EXP` (`n`) and actual hardware window (ms). JTAG poll timing is **measured when you `source mc_read`** (see Timing parameters).

### 2. Linux — start inference

Example:

```bash
ml_vart --app-config /etc/vai/ml_vart/json_configs/ml_vart_config.json --benchmark --runs 5000
```

Start the workload **before** or **while** logging runs. Counters only reflect traffic while DDR is active.

### 3. XSDB — start logging

```tcl
source mc_read_ddr_bandwidth.tcl
```

**You may** `source mc_read` **before** the app starts: the script waits until `TOTAL_BW > MIN_TOTAL_BW_CSV` (default **0.05 GB/s**), printing `Waiting for DDR traffic...`.

**Default stop behavior:**

1. Log CSV rows only when `TOTAL_BW > 0.05` GB/s.
2. After traffic was seen, exit after **10** consecutive polls with `TOTAL_BW ≤ 0.05` GB/s (`STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES`).

The script does **not** exit immediately on an idle board; it waits for traffic first.

**Output file** (current directory):

`output_vek385_ddr<MHz>_n<n>_hw<window>ms_poll<poll>ms.csv`

Example: `output_vek385_ddr1050MHz_n22_hw3.993ms_poll5ms.csv`

On exit, mean `TOTAL_BW` over logged rows is printed.

---

## Optional information

The sections below are **not required to run** the workflow (`connect` → `init` → `ml_vart` → `mc_read`). They are reference material for tuning hardware windows and poll timing, understanding the GB/s formula, CSV layout, and advanced `mc_read` options.

## Timing parameters

Two independent knobs (easy to confuse):

| Topic | What it controls |
|-------|------------------|
| **A. Hardware window** | Length of each DDR counter slice → **GB/s in each CSV column** |
| **B. JTAG poll spacing** | How often XSDB reads counters → **time between CSV rows** |

They are **not** the same: a ~16 ms hardware window can still produce a new CSV row only every ~73 ms if JTAG is slow.

### A. Hardware counter window (`init`)

Set before `source init_ddrmc_perf_counters.tcl`:

| Variable | Default | Description |
|----------|---------|-------------|
| `HW_WINDOW_MS` | `4` | Target window: **4**, **8**, or **16** ms only |
| `DDR_FREQ_MHZ` | `1050.42` | DDR clock for GB/s formula and picking `n` |
| `INTERVAL_EXP` (`n`) | *(auto)* | Nearest integer `n` so ~2^n/(freq×1000) ≈ `HW_WINDOW_MS` |

The monitor only supports discrete windows (~2^n), not exact 4.000 ms. At **DDR_FREQ_MHZ = 1050.42**:

| `HW_WINDOW_MS` (you set) | Typical `n` | ~Actual HW window |
|--------------------------|------------|-------------------|
| 4 | 22 | ~3.99 ms |
| 8 | 23 | ~7.99 ms |
| 16 | 24 | ~15.97 ms |

`mc_read` can recompute `n` if you re-source it without re-init; changing `HW_WINDOW_MS` usually means set it again and re-run `init`.

### B. JTAG poll interval (`mc_read`)

On each normal `source mc_read_ddr_bandwidth.tcl`, the script measures one `ddr_bw_all` and sets:

| Variable | Default | Description |
|----------|---------|-------------|
| `XSDB_LOOP_MS_ESTIMATE` | measured | ms for all `mrd` over JTAG (e.g. ~60–75 ms on VEK, 5 MCs) |
| `TARGET_EFF_POLL_MS` | `HW_WINDOW_MS + 1` | Ideal row period if JTAG were fast (4→5, 8→9, 16→17 ms) |
| `POLL_INTERVAL_MS` | auto | `max(0, TARGET_EFF_POLL_MS − XSDB_LOOP_MS_ESTIMATE)` |

**VEK385:** JTAG loop is usually **longer** than `HW_WINDOW_MS + 1`, so `POLL_INTERVAL_MS` is often **0** and row spacing ≈ measured loop. `mc_read` prints this in its banner.

**Overrides (optional):** `MANUAL_XSDB_LOOP_MS_ESTIMATE` + `XSDB_LOOP_MS_ESTIMATE`, or `MANUAL_POLL_INTERVAL_MS` + `POLL_INTERVAL_MS`, before `source mc_read`.

**`MEASURE_LOOP_TIME` (optional, usually leave alone):** Every normal `source mc_read_ddr_bandwidth.tcl` already runs the JTAG probe and sets poll timing — you do **not** set anything for that. Only use `set MEASURE_LOOP_TIME 1` if you want the same probe **without** CSV logging (timing-only). That value **sticks in the XSDB session**; if CSV never starts and you see `MEASURE_LOOP_TIME=1: calibration done`, run `unset MEASURE_LOOP_TIME` (or `set MEASURE_LOOP_TIME 0`) once, then `source mc_read` again.

---

## Bandwidth formula

Each CSV value is GB/s for the **last completed hardware window** (not a long software average):

```text
BW (GB/s) = (raw / 2^INTERVAL_EXP) × 32 × DDR_FREQ_MHZ × 1e6 × 2 / 1e9
```

`raw` from `dc0_perf_mon_1` (+0x1070) or `dc0_perf_mon_2` (+0x1078). Poll interval does **not** enter the formula.

---

## Optional XSDB variables (`mc_read`)

| Variable | Default | Purpose |
|----------|---------|---------|
| `MIN_TOTAL_BW_CSV` | `0.05` | Active traffic threshold (GB/s) |
| `STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES` | `10` | Idle polls after traffic before stop |
| `RUN_DURATION_SEC` | `0` | Wall-clock cap (0 = disabled) |
| `VERBOSE_OUTPUT` | `0` | Per-channel prints |
| `MEASURE_LOOP_TIME` | `0` (default) | Leave unset/`0` for normal CSV runs (probe runs automatically). `1` = probe then exit — only for timing-only; clear before logging if you set `1` |
| `MANUAL_XSDB_LOOP_MS_ESTIMATE` | unset | If set, skip auto probe; use your `XSDB_LOOP_MS_ESTIMATE` |
| `MANUAL_POLL_INTERVAL_MS` | unset | If set, skip auto poll math; use your `POLL_INTERVAL_MS` |
| `MC_READ_STOP_AFTER_ACTIVE_SAMPLES` | — | If set before `source`, stop after N active rows (test mode) |

---

## CSV columns

`DDR0_READ,DDR0_WRITE,DDR0_TOTAL,…,DDR4_READ,DDR4_WRITE,DDR4_TOTAL,TOTAL_READ,TOTAL_WRITE,TOTAL_BW`

All values in **GB/s**.

---

## References

- AMD Versal ACAP documentation — DDRMC5E DC performance monitors (NPI).
