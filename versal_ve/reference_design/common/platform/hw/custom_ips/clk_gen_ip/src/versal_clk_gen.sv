/* Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved. */

module versal_clk_gen
(
  input  clk,
  input  clk_locked,
  output clk_4x,
  output clk_2x,
  output clk_1x
);

MBUFGCE
#(
   .SIM_DEVICE  ("VERSAL_AI_CORE"  )
  ,.MODE        ("PERFORMANCE"     )
)
clkroot
(
   .O1         (clk_4x      )
  ,.O2         (clk_2x      )
  ,.O3         (clk_1x      )
  ,.O4         (            )
  ,.CE         (clk_locked  )
  ,.CLRB_LEAF  (1'b 1       )
  ,.I          (clk         )
);

endmodule
