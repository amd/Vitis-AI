
create_clock -period    2.000 -name clk_4x    [get_ports clk_4x]
create_clock -period    4.000 -name clk_2x    [get_ports clk_2x]
create_clock -period    8.000 -name clk_1x    [get_ports clk_1x]
create_clock -period    4.000 -name clk_ddr   [get_ports clk_ddr]
create_clock -period    4.000 -name clk_pcie  [get_ports clk_pcie]
create_clock -period  100.000 -name clk_srv   [get_ports clk_srv]

set_clock_groups -asynchronous -group [get_clocks {clk_1x clk_2x clk_4x}]
set_clock_groups -asynchronous -group [get_clocks {clk_ddr}]
set_clock_groups -asynchronous -group [get_clocks {clk_pcie}]
set_clock_groups -asynchronous -group [get_clocks {clk_srv}]

