// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, BLANK2077
`timescale 1ns/1ps
module top;
    logic clk = 0;
    logic [7:0] count = 0;
    always #5 clk = ~clk;
    always @(posedge clk) count <= count + 1;
    initial begin
        $dumpfile("waves.fst");
        $dumpvars(0, top);
        #100;
        $finish;
    end
endmodule
