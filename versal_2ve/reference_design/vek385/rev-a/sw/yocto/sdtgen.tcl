
#!/bin/bash
#
# Copyright (C) 2025, Advanced Micro Devices, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Description:
#   tcl script to configure args for sdtgen tool
#

#+---------------------------------------------------------------------------------------
#| RSH-5357 - TCL script for sdtgen utility (Telluride)
#+---------------------------------------------------------------------------------------
for { set i 0 } { $i < $argc } { incr i } {
    # xsa path
    if { [lindex $argv $i] == "-xsa_path" } {
      incr i
      set xsa_path [lindex $argv $i]
    }
    # SDT path
    if { [lindex $argv $i] == "-sdt_path" } {
      incr i
      set sdt_path [lindex $argv $i]
    }
    #board dts name
    if { [lindex $argv $i] == "-board_dts" } {
      incr i
      set board_dts [lindex $argv $i]
    }
  }
  
  set_dt_param -debug enable
  set_dt_param -dir $sdt_path -zocl "enable"
  set_dt_param -xsa $xsa_path
  set_dt_param -board_dts $board_dts
  generate_sdt
