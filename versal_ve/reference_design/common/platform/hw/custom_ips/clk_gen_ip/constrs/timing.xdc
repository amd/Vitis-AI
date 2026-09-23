create_clock -period  2.000 -name clk [get_ports clk]
set_property CLOCK_BUFFER_TYPE BUFG [get_ports clk]
