# =============================================================================
# mc_read_ddr_bandwidth.tcl — VEK385 (T50), 5 DDR channels
#
# Description:
#   Polls DDRMC5E DC perf counters on all 5 DDR channels (VEK385/T50) and logs CSV.
#   Typical flow: init (XSDB) → start AIE app on Linux → source this script (XSDB).
#   You may source mc_read before the app starts; it waits until DDR traffic appears.
#   Logs CSV rows with TOTAL_BW > MIN_TOTAL_BW_CSV (default 0.05 GB/s)
#   until STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES (default 10) polls in a row are at or
#   below that threshold after traffic was seen (AIE finished). RUN_DURATION_SEC optional.
#   Note: each source resets row-cap stop; use MC_READ_STOP_AFTER_ACTIVE_SAMPLES for test cap.
#   Directional profiling only: reported GB/s are approximate (HW-window sampling over JTAG), not exact bandwidth.
#
# Prerequisites:
#   source init_ddrmc_perf_counters.tcl first (same HW_WINDOW_MS / INTERVAL_EXP /
#   DDR_FREQ_MHZ / poll settings).
#
# Target:
#   Versal 2VE (T50) — VEK385 RevB; DPC selected via name filter.
#
# Reference:
#   AMD Versal ACAP documentation — DDRMC5E DC performance monitor registers (NPI).
#
# Usage (after init_ddrmc_perf_counters.tcl on same XSDB session):
#   Start AIE inference on the board (Linux terminal), then:
#     source mc_read_ddr_bandwidth.tcl
#   Optional before init: set HW_WINDOW_MS 4|8|16, set DDR_FREQ_MHZ if not default.
#   Stop: default = 10 consecutive idle polls (TOTAL_BW <= 0.05 GB/s) after traffic seen.
#
# Read registers (+0x1070 READ, +0x1078 WRITE from each channel base).
#
# Bandwidth formula:
#   BW (GB/s) = (raw / 2^INTERVAL_EXP) * 32 * DDR_FREQ_MHZ * 1e6 * 2 / 1e9
#
# CSV columns (GB/s):
#   DDR0..DDR4 READ/WRITE/TOTAL, then TOTAL_READ, TOTAL_WRITE, TOTAL_BW
#
# Output file name includes: vek385, DDR MHz, n, HW window (ms), effective poll
#   e.g. output_vek385_ddr1050MHz_n22_hw3.993ms_poll8ms.csv
# On exit, prints mean TOTAL_BW over all rows written to the CSV.
# =============================================================================

# --- experiment parameters (defaults; xsdb "set" before source overrides) -----
if {![info exists DDR_FREQ_MHZ]} { set DDR_FREQ_MHZ 1050.42 }
if {![info exists HW_WINDOW_MS]} { set HW_WINDOW_MS 4 }
if {![info exists VERBOSE_OUTPUT]} { set VERBOSE_OUTPUT 0 }
if {![info exists MEASURE_LOOP_TIME]} { set MEASURE_LOOP_TIME 0 }
# Set MANUAL_XSDB_LOOP_MS_ESTIMATE before source to skip auto JTAG timing on mc_read start.
if {![info exists RUN_DURATION_SEC]} { set RUN_DURATION_SEC 0 }
if {![info exists MIN_TOTAL_BW_CSV]} { set MIN_TOTAL_BW_CSV 0.05 }
if {![info exists STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES]} { set STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES 10 }
# Always 0 unless MC_READ_STOP_AFTER_ACTIVE_SAMPLES is set in xsdb before source (avoids stale "set 5" in session).
if {[info exists MC_READ_STOP_AFTER_ACTIVE_SAMPLES]} {
    set STOP_AFTER_ACTIVE_SAMPLES $MC_READ_STOP_AFTER_ACTIVE_SAMPLES
} else {
    set STOP_AFTER_ACTIVE_SAMPLES 0
}
if {![info exists STOP_ON_ZERO_BW]} { set STOP_ON_ZERO_BW 0 }
if {![info exists WAIT_FOR_TRAFFIC_BEFORE_STOP]} { set WAIT_FOR_TRAFFIC_BEFORE_STOP 0 }

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
    puts "ERROR: INTERVAL_EXP must be 0-47"
    return
}

set window_ms [expr {pow(2, $INTERVAL_EXP) / ($DDR_FREQ_MHZ * 1000.0)}]
set window_us [expr {pow(2, $INTERVAL_EXP) / $DDR_FREQ_MHZ}]

set TARGET_EFF_POLL_MS [expr {$HW_WINDOW_MS + 1}]

array set ddr_addrs {
    DDR0 {0xf6541070 0xf6541078}
    DDR1 {0xf6631070 0xf6631078}
    DDR2 {0xf6b71070 0xf6b71078}
    DDR3 {0xf6ec1070 0xf6ec1078}
    DDR4 {0xf6fa1070 0xf6fa1078}
}

proc ddr_bw_single {ddr_name read_addr write_addr} {
    global DDR_FREQ_MHZ INTERVAL_EXP VERBOSE_OUTPUT
    set divisor [expr {2**$INTERVAL_EXP}]

    set reg_read [mrd -force $read_addr]
    set raw_read 0
    if {[catch {
        if {![regexp {.*:\s+(.*)} $reg_read -> raw_read_hex]} {
            error "no colon-delimited value found"
        }
        set raw_read [format %d "0x$raw_read_hex"]
    } err]} {
        puts "WARNING: $ddr_name: unparsable mrd READ $read_addr: '$reg_read' ($err)"
        set raw_read 0
    }
    set bw_read [expr {double($raw_read * 32) / $divisor * $DDR_FREQ_MHZ * 1e6 * 2 / 1e9}]

    set reg_write [mrd -force $write_addr]
    set raw_write 0
    if {[catch {
        if {![regexp {.*:\s+(.*)} $reg_write -> raw_write_hex]} {
            error "no colon-delimited value found"
        }
        set raw_write [format %d "0x$raw_write_hex"]
    } err]} {
        puts "WARNING: $ddr_name: unparsable mrd WRITE $write_addr: '$reg_write' ($err)"
        set raw_write 0
    }
    set bw_write [expr {double($raw_write * 32) / $divisor * $DDR_FREQ_MHZ * 1e6 * 2 / 1e9}]

    set r [expr {double(round(100 * $bw_read)) / 100}]
    set w [expr {double(round(100 * $bw_write)) / 100}]
    set t [expr {double(round(100 * ($bw_read + $bw_write))) / 100}]
    if {$VERBOSE_OUTPUT} {
        puts "$ddr_name"
        puts "  dc0_perf_mon_1 (READ BW)  : $r GB/s"
        puts "  dc0_perf_mon_2 (WRITE BW) : $w GB/s"
        puts "  TOTAL BW                  : $t GB/s"
    }
    return [list $r $w $t]
}

proc ddr_bw_all {} {
    global ddr_addrs VERBOSE_OUTPUT
    set per_ddr_vals {}
    set total_read 0.0
    set total_write 0.0
    set total_bw 0.0
    foreach ch {DDR0 DDR1 DDR2 DDR3 DDR4} {
        set addrs $ddr_addrs($ch)
        set res [ddr_bw_single $ch [lindex $addrs 0] [lindex $addrs 1]]
        lappend per_ddr_vals [lindex $res 0] [lindex $res 1] [lindex $res 2]
        set total_read [expr {$total_read + [lindex $res 0]}]
        set total_write [expr {$total_write + [lindex $res 1]}]
        set total_bw [expr {$total_bw + [lindex $res 2]}]
    }
    if {$VERBOSE_OUTPUT} {
        puts "TOTAL ALL DDR"
        puts "  READ BW  : $total_read GB/s"
        puts "  WRITE BW : $total_write GB/s"
        puts "  TOTAL BW : $total_bw GB/s"
        puts "------------------------------------------------------------"
    }
    set tr [expr {double(round(100 * $total_read)) / 100}]
    set tw [expr {double(round(100 * $total_write)) / 100}]
    set tb [expr {double(round(100 * $total_bw)) / 100}]
    return [list $per_ddr_vals $tr $tw $tb]
}

target -set -filter {name =~ "*DPC*"}
puts "DPC target selected."

# Auto-measure JTAG time for one full counter read (all mrd), then set poll delay.
set xsdb_loop_measured 0
if {![info exists MANUAL_XSDB_LOOP_MS_ESTIMATE]} {
    set t0 [clock milliseconds]
    ddr_bw_all
    set XSDB_LOOP_MS_ESTIMATE [expr {[clock milliseconds] - $t0}]
    set xsdb_loop_measured 1
} elseif {![info exists XSDB_LOOP_MS_ESTIMATE]} {
    puts "WARNING: MANUAL_XSDB_LOOP_MS_ESTIMATE set but XSDB_LOOP_MS_ESTIMATE not provided — using TARGET_EFF_POLL_MS (${TARGET_EFF_POLL_MS} ms) as fallback"
    set XSDB_LOOP_MS_ESTIMATE $TARGET_EFF_POLL_MS
}
if {![info exists MANUAL_POLL_INTERVAL_MS]} {
    set poll_calc [expr {int(round($TARGET_EFF_POLL_MS - $XSDB_LOOP_MS_ESTIMATE))}]
    if {$poll_calc < 0} { set poll_calc 0 }
    set POLL_INTERVAL_MS $poll_calc
} elseif {![info exists POLL_INTERVAL_MS]} {
    set POLL_INTERVAL_MS 0
}

set eff_poll [expr {$XSDB_LOOP_MS_ESTIMATE + $POLL_INTERVAL_MS}]
set windows_per_poll [expr {$eff_poll / $window_ms}]

puts "------------------------------------------------------------"
puts "XSDB poll timing (updated at mc_read start)"
if {$xsdb_loop_measured} {
    puts "  Measured ddr_bw_all (all mrd over JTAG): ${XSDB_LOOP_MS_ESTIMATE} ms"
} else {
    puts "  ddr_bw_all loop estimate (not auto-measured): ${XSDB_LOOP_MS_ESTIMATE} ms"
}
puts "  XSDB_LOOP_MS_ESTIMATE                  = ${XSDB_LOOP_MS_ESTIMATE} ms"
puts "  Target period (HW_WINDOW_MS + 1)       = ${TARGET_EFF_POLL_MS} ms"
puts "  POLL_INTERVAL_MS (extra after delay)   = ${POLL_INTERVAL_MS} ms"
puts "  Effective ~interval between CSV rows   = [format %.2f $eff_poll] ms"
if {$XSDB_LOOP_MS_ESTIMATE > $TARGET_EFF_POLL_MS} {
    puts "  NOTE: JTAG read time exceeds target period — interval is dominated by mrd (~${XSDB_LOOP_MS_ESTIMATE} ms), not HW_WINDOW_MS+1."
}
puts "------------------------------------------------------------"
puts "  HW_WINDOW_MS target = ${HW_WINDOW_MS} ms"
puts "  INTERVAL_EXP n      = $INTERVAL_EXP  (~[format %.3f $window_us] us @ ${DDR_FREQ_MHZ} MHz)"
puts "  Actual HW window    = [format %.4f $window_ms] ms"
puts "  No. of HW windows/poll = [format %.2f $windows_per_poll]"
puts "------------------------------------------------------------"

if {$MEASURE_LOOP_TIME} {
    puts "MEASURE_LOOP_TIME=1: calibration done — not starting CSV logging."
    return
}

set win_ms_str [format %.3f $window_ms]
set poll_ms_str [format %.0f $eff_poll]
set ddr_mhz_str [format %.0f $DDR_FREQ_MHZ]
set output_file "output_vek385_ddr${ddr_mhz_str}MHz_n${INTERVAL_EXP}_hw${win_ms_str}ms_poll${poll_ms_str}ms.csv"
set fileId [open $output_file "w"]
puts $fileId "DDR0_READ,DDR0_WRITE,DDR0_TOTAL,DDR1_READ,DDR1_WRITE,DDR1_TOTAL,DDR2_READ,DDR2_WRITE,DDR2_TOTAL,DDR3_READ,DDR3_WRITE,DDR3_TOTAL,DDR4_READ,DDR4_WRITE,DDR4_TOTAL,TOTAL_READ,TOTAL_WRITE,TOTAL_BW"

puts "Logging -> $output_file"
puts "  HW window = ${win_ms_str} ms  |  effective poll ~ ${poll_ms_str} ms (${XSDB_LOOP_MS_ESTIMATE}+${POLL_INTERVAL_MS})"
puts "  after delay each sample = ${POLL_INTERVAL_MS} ms"
puts "  CSV rows        = TOTAL_BW > ${MIN_TOTAL_BW_CSV} GB/s only"
if {$STOP_ON_ZERO_BW || $WAIT_FOR_TRAFFIC_BEFORE_STOP} {
    puts "  stop condition  = legacy STOP_ON_ZERO_BW / WAIT_FOR_TRAFFIC"
} elseif {$STOP_AFTER_ACTIVE_SAMPLES > 0} {
    puts "  stop condition  = first ${STOP_AFTER_ACTIVE_SAMPLES} active rows (test mode)"
} else {
    puts "  stop condition  = ${STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES} consecutive polls TOTAL_BW <= ${MIN_TOTAL_BW_CSV} after traffic seen"
}
if {$RUN_DURATION_SEC > 0} {
    puts "  run duration    = ${RUN_DURATION_SEC} s max (wall-clock cap)"
}
puts "  verbose         = ${VERBOSE_OUTPUT} (0 = quiet)"
puts "============================================================"

set sample_count 0
set seen_traffic 0
set consecutive_idle 0
set sum_total_bw 0.0
set sum_active_bw 0.0
set active_row_count 0
set run_start_ms [clock milliseconds]
set run_limit_ms [expr {$RUN_DURATION_SEC * 1000}]

if {[catch {
    while {1} {
        set result [ddr_bw_all]
        set per_ddr [lindex $result 0]
        set tot_bw [lindex $result 3]
        set is_active [expr {$tot_bw > $MIN_TOTAL_BW_CSV}]

        if {$STOP_ON_ZERO_BW || $WAIT_FOR_TRAFFIC_BEFORE_STOP} {
            if {[expr {$tot_bw > 0.01}]} { set seen_traffic 1 }
            set log_row 1
            if {$WAIT_FOR_TRAFFIC_BEFORE_STOP && !$seen_traffic} {
                set log_row 0
                puts "Waiting for DDR traffic..."
            }
            if {$log_row && $is_active} {
                puts $fileId [join [concat $per_ddr [lindex $result 1] [lindex $result 2] $tot_bw] ","]
                flush $fileId
                incr sample_count
                set sum_total_bw [expr {$sum_total_bw + $tot_bw}]
                set sum_active_bw [expr {$sum_active_bw + $tot_bw}]
                incr active_row_count
            }
            if {$WAIT_FOR_TRAFFIC_BEFORE_STOP} {
                if {$seen_traffic && $log_row && [expr {abs($tot_bw) < 0.0001}]} {
                    puts "Total BW ~ 0 — stopping."
                    break
                }
            } elseif {$log_row && [expr {abs($tot_bw) < 0.0001}]} {
                puts "Total BW ~ 0 — stopping."
                break
            }
        } else {
            if {$is_active} {
                set seen_traffic 1
                set consecutive_idle 0
                puts $fileId [join [concat $per_ddr [lindex $result 1] [lindex $result 2] $tot_bw] ","]
                flush $fileId
                incr sample_count
                set sum_total_bw [expr {$sum_total_bw + $tot_bw}]
                set sum_active_bw [expr {$sum_active_bw + $tot_bw}]
                incr active_row_count
                if {$STOP_AFTER_ACTIVE_SAMPLES > 0 && $sample_count >= $STOP_AFTER_ACTIVE_SAMPLES} {
                    puts "Collected ${STOP_AFTER_ACTIVE_SAMPLES} active samples — stopping (STOP_AFTER_ACTIVE_SAMPLES)."
                    break
                }
            } elseif {$seen_traffic} {
                incr consecutive_idle
                if {$consecutive_idle >= $STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES} {
                    puts "Stopping: ${STOP_AFTER_CONSECUTIVE_IDLE_SAMPLES} consecutive polls with TOTAL_BW <= ${MIN_TOTAL_BW_CSV} GB/s (workload ended)."
                    break
                }
            } else {
                puts "Waiting for DDR traffic (TOTAL_BW > ${MIN_TOTAL_BW_CSV} GB/s)..."
            }
        }

        if {$RUN_DURATION_SEC > 0} {
            set elapsed_ms [expr {[clock milliseconds] - $run_start_ms}]
            if {$elapsed_ms >= $run_limit_ms} {
                puts "Run duration ${RUN_DURATION_SEC} s reached — stopping."
                break
            }
        }

        if {$POLL_INTERVAL_MS > 0} { after $POLL_INTERVAL_MS }
    }
} err]} {
    puts "ERROR: $err"
}

close $fileId
puts "CSV saved: $output_file ($sample_count rows)"

if {$sample_count > 0} {
    set avg_all [expr {$sum_total_bw / $sample_count}]
    puts "============================================================"
    puts "VEK385 CSV summary — TOTAL_BW (GB/s)"
    puts "  Mean (all $sample_count rows)     : [format %.4f $avg_all]"
    if {$active_row_count > 0} {
        set avg_active [expr {$sum_active_bw / $active_row_count}]
        puts "  Mean (CSV rows, TOTAL_BW>${MIN_TOTAL_BW_CSV}, n=$active_row_count): [format %.4f $avg_active]"
    }
    puts "============================================================"
} else {
    puts "No CSV data rows — average TOTAL_BW not computed."
}
