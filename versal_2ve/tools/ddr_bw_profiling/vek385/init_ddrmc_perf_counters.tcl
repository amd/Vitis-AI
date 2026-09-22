# =============================================================================
# init_ddrmc_perf_counters.tcl — VEK385 (T50), 5 DDR channels
#
# Description:
#   Initializes the DDR Memory Controller (DDRMC5E) performance counters on
#   all 5 DDR channels of the Versal 2VE (T50) device. This script must be
#   run in xsdb BEFORE mc_read_ddr_bandwidth.tcl to enable bandwidth logging.
#
# Target:
#   Versal 2VE (T50) — VEK385 RevB board
#   DPC target is selected automatically by this script using name-based filter.
#   Works in both JTAG boot (systest boardfarm) and OSPI boot (standalone) modes.
#
# Reference:
#   AMD Versal ACAP documentation — DDRMC5E DC performance monitor registers (NPI).
#
# Usage — recommended sequence:
#   XSDB (this terminal):
#     connect
#     set HW_WINDOW_MS 4        ;# optional: 4 (default), 8, or 16 ms target window
#     source init_ddrmc_perf_counters.tcl
#   Linux (other terminal): start AIE inference (e.g. ml_vart --benchmark) while
#     or before logging — counters only see traffic while the app runs.
#   XSDB again:
#     source mc_read_ddr_bandwidth.tcl
#   mc_read waits until TOTAL_BW > MIN_TOTAL_BW_CSV (default 0.05 GB/s), logs active rows,
#   then exits after STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES (default 10) polls in a row at or
#   below that threshold once traffic was seen (inference finished). It does not exit
#   immediately if the board is idle; it waits for traffic first.
#
# HW_WINDOW_MS, INTERVAL_EXP, DDR_FREQ_MHZ, and poll settings must match
# mc_read_ddr_bandwidth.tcl for the same run. You may "set HW_WINDOW_MS 8"
# or lock n with: set MANUAL_INTERVAL_EXP 1 ; set INTERVAL_EXP <n> before source init.
# On each source init, n is re-derived from HW_WINDOW_MS and DDR_FREQ_MHZ unless
# MANUAL_INTERVAL_EXP is set. mc_read uses the same rules if re-sourced alone.
#
# Initialization sequence per DDR channel:
#   1. Unlock NPI PCSR (pcsr_lock) — required before any register write
#   2. Disable on-the-fly scrub (reg_scrub_otf) — avoids interference
#   3. Stop background scrub FSM ch0 (reg_scrub_status_ch0)
#   4. Stop background scrub FSM ch1 (reg_scrub_status_ch1)
#   5. Set interval size = 2^n on dc0_perf_mon / dc1_perf_mon (n = INTERVAL_EXP)
#   6. Start monitoring on dc0_perf_mon and dc1_perf_mon
#
# Register map (offsets from DDR base address):
#   +0x00C  pcsr_lock           NPI Lock Register (write 0xF9E8D7C6 to unlock)
#   +0x8A0  reg_scrub_otf       On-the-fly scrub config (write 0x0 to disable)
#   +0xCE0  reg_scrub_status_ch0 Background scrub FSM status ch0 (write 0x2 to stop)
#   +0xCE4  reg_scrub_status_ch1 Background scrub FSM status ch1 (write 0x2 to stop)
#   +0x105C dc0_perf_mon        DC monitor channel 0 (n<<1 set interval, (n<<1)|1 start)
#   +0x10B0 dc1_perf_mon        DC monitor channel 1
#
# HW_WINDOW_MS (4 / 8 / 16) picks nearest n so 2^n DDR cycles ≈ target at DDR_FREQ_MHZ.
# Default effective poll budget: HW_WINDOW_MS + 1 ms (XSDB_LOOP_MS_ESTIMATE unless measured).
#
# DDR Channel Base Addresses (T50 / VEK385):
#   DDR0: 0xF6540000  DDR1: 0xF6630000  DDR2: 0xF6B70000
#   DDR3: 0xF6EC0000  DDR4: 0xF6FA0000
# =============================================================================

# --- experiment parameters (defaults; xsdb "set" before source overrides) -----
if {![info exists DDR_FREQ_MHZ]} { set DDR_FREQ_MHZ 1050.42 }
if {![info exists HW_WINDOW_MS]} { set HW_WINDOW_MS 4 }

if {$HW_WINDOW_MS != 4 && $HW_WINDOW_MS != 8 && $HW_WINDOW_MS != 16} {
    puts "ERROR: HW_WINDOW_MS must be 4, 8, or 16 (got $HW_WINDOW_MS)"
    return
}

proc ddr_bw_nearest_interval_exp {hw_ms freq_mhz} {
    set best_n 0
    set best_err 1.0e308
    for {set n 0} {$n <= 47} {incr n} {
        set w_ms [expr {pow(2.0, $n) / ($freq_mhz * 1000.0)}]
        set err [expr {abs($w_ms - $hw_ms)}]
        if {$err < $best_err} {
            set best_err $err
            set best_n $n
        }
    }
    return $best_n
}

if {![info exists MANUAL_INTERVAL_EXP]} {
    set INTERVAL_EXP [ddr_bw_nearest_interval_exp $HW_WINDOW_MS $DDR_FREQ_MHZ]
}

if {$INTERVAL_EXP < 0 || $INTERVAL_EXP > 47} {
    puts "ERROR: INTERVAL_EXP must be 0-47 (got $INTERVAL_EXP)"
    return
}

set interval_set [expr {($INTERVAL_EXP << 1) & 0xFF}]
set interval_start [expr {($interval_set | 1) & 0xFF}]

set window_ms [expr {pow(2, $INTERVAL_EXP) / ($DDR_FREQ_MHZ * 1000.0)}]

# Poll timing (XSDB_LOOP_MS_ESTIMATE, POLL_INTERVAL_MS) is measured and set at mc_read start.
set TARGET_EFF_POLL_MS [expr {$HW_WINDOW_MS + 1}]
set eff_poll_na 1
if {[info exists XSDB_LOOP_MS_ESTIMATE]} {
    if {![info exists MANUAL_POLL_INTERVAL_MS]} {
        set poll_calc [expr {int(round($TARGET_EFF_POLL_MS - $XSDB_LOOP_MS_ESTIMATE))}]
        if {$poll_calc < 0} { set poll_calc 0 }
        set POLL_INTERVAL_MS $poll_calc
    }
    set eff_poll [expr {$XSDB_LOOP_MS_ESTIMATE + $POLL_INTERVAL_MS}]
    set windows_per_poll [expr {$eff_poll / $window_ms}]
    set eff_poll_na 0
}

puts "------------------------------------------------------------"
puts "DDR BW init — VEK385/T50"
puts "  HW_WINDOW_MS target = ${HW_WINDOW_MS} ms"
puts "  INTERVAL_EXP n      = $INTERVAL_EXP  (2^$INTERVAL_EXP DDR cycles)"
puts "  DDR_FREQ_MHZ        = $DDR_FREQ_MHZ"
puts "  Actual HW window    = [format %.4f $window_ms] ms"
puts "  Init mwr            = [format 0x%02X $interval_set] then [format 0x%02X $interval_start]"
puts "  TARGET eff poll     = ${TARGET_EFF_POLL_MS} ms  (HW_WINDOW_MS + 1; used by mc_read)"
if {$eff_poll_na} {
    puts "  XSDB mrd + POLL     = measured when you source mc_read_ddr_bandwidth.tcl"
} else {
    puts "  XSDB loop est       = ${XSDB_LOOP_MS_ESTIMATE} ms"
    puts "  POLL_INTERVAL_MS    = ${POLL_INTERVAL_MS} ms  (after each sample)"
    puts "  Eff. poll est       = [format %.2f $eff_poll] ms"
    puts "  No. of HW windows/poll = [format %.2f $windows_per_poll]"
}
puts "------------------------------------------------------------"

target -set -filter {name =~ "*DPC*"}
puts "DPC target selected."
puts "Initializing DDRMC5E counters (VEK385/T50)..."

foreach {idx base} {
    0 0xF6540000
    1 0xF6630000
    2 0xF6B70000
    3 0xF6EC0000
    4 0xF6FA0000
} {
    puts "DDR$idx base [format 0x%08X $base]"
    mwr -force [expr {$base + 0x00C}] 0xf9e8d7c6
    mwr -force [expr {$base + 0x8A0}] 0x0
    mwr -force [expr {$base + 0xCE0}] 0x2
    mwr -force [expr {$base + 0xCE4}] 0x2
    mwr -force [expr {$base + 0x105C}] $interval_set
    mwr -force [expr {$base + 0x10B0}] $interval_set
    mwr -force [expr {$base + 0x105C}] $interval_start
    mwr -force [expr {$base + 0x10B0}] $interval_start
}

# Crypto performance counter — DDR0 crypto (Base: 0xF6BE0000)
# Uncomment to enable crypto monitoring (ddrmc5e_crypto_main_0)
# mwr -force 0xf6be000c 0xf9e8d7c6
# mwr -force 0xf6be085c 0x1
# mwr -force 0xf6be0860 0x10

puts "Done. Start AIE workload on Linux, then source mc_read_ddr_bandwidth.tcl (same HW_WINDOW_MS / INTERVAL_EXP / DDR_FREQ_MHZ / poll settings)."
