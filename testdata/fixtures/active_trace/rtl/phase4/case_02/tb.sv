`timescale 1ns/1ps

module top;
    logic clk, rst_n;
    logic [7:0] data_in, data_out;
    logic [1:0] mux_sel, proc_sel;

    phase4_dut #(.PRE_A(1), .PRE_F(1), .PRE_M(0), .PRE_G(0),
                 .PRE_I(0), .PRE_B(0), .PRE_P(0), .NUM_POST(3))
        u_dut (.clk, .rst_n, .data_in, .mux_sel, .proc_sel, .data_out);

    initial begin clk = 1'b0; forever #5 clk = ~clk; end

    initial begin
        rst_n=0; data_in=0; mux_sel=2'b01; proc_sel=2'b00;
        #7 rst_n=1;
        #8 data_in=8'hA5;     // @15ns, sampled at 25ns posedge
        #30 $finish;          // @45ns
    end
endmodule
