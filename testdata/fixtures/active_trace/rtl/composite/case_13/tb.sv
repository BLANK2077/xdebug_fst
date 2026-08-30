`timescale 1ns/1ps


module top;
    logic       clk;
    logic       rst_n;
    logic [7:0] data_in;
    logic [1:0] mux_sel;
    logic [1:0] proc_sel;
    logic [7:0] data_out;

    chain_dut #(
        .ENABLE_ASSIGN  (1),
        .ENABLE_FLOP    (1),
        .ENABLE_MODULE  (1),
        .ENABLE_GENERATE(1),
        .ENABLE_IFACE   (0),
        .ENABLE_MUX     (1),
        .ENABLE_PROCFOR (0)
    ) u_dut (
        .clk(clk), .rst_n(rst_n),
        .data_in(data_in), .mux_sel(mux_sel), .proc_sel(proc_sel),
        .data_out(data_out)
    );
    // clock: period 10ns
    initial begin clk = 1'b0; forever #5 clk = ~clk; end

    initial begin
        rst_n    = 1'b0;
        data_in  = 8'h00;
        mux_sel  = 2'b01;   // select data_in path
        proc_sel = 2'b00;

        #7  rst_n = 1'b1;

        // drive data_in at 15ns → propagates through chain
        #8  data_in = 8'hA5;   // @15ns

        // second change at 35ns
        #20 data_in = 8'h5A;   // @35ns

        #20 $finish;            // @55ns
    end
endmodule
