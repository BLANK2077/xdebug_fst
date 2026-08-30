`timescale 1ns/1ps

module top;
    logic clk, rst_n, mux_sel;
    logic [7:0] data_in, data_out;

    timing_boundary_dut #(.NUM_PRE(5), .NUM_POST(5),
                          .MUX_PRE(4), .MUX_POST(4))
        u_dut (.clk, .rst_n, .data_in, .mux_sel, .data_out);

    initial begin clk = 1'b0; forever #5 clk = ~clk; end

    initial begin
        rst_n = 1'b0; data_in = 8'h00; mux_sel = 1'b1;
        #7 rst_n = 1'b1;
        #8 data_in = 8'hA5;    // @15ns
        #30 data_in = 8'h5A;   // @45ns
        #30 $finish;           // @75ns
    end
endmodule
