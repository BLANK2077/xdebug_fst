`timescale 1ns/1ps

module active_probe_dut (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       sel,
  input  logic [1:0] mode,
  input  logic [7:0] data_a,
  input  logic [7:0] data_b,
  output logic [7:0] q,
  output logic [7:0] comb_q
);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      q <= 8'h00;
    else if (sel)
      q <= data_a;
    else
      q <= data_b;
  end

  always_comb begin
    case (mode)
      2'b00: comb_q = q;
      2'b01: comb_q = data_a;
      2'b10: comb_q = data_b;
      default: comb_q = 8'hxx;
    endcase
  end

endmodule

module active_driver_tb;
  logic       clk;
  logic       rst_n;
  logic       sel;
  logic [1:0] mode;
  logic [7:0] data_a;
  logic [7:0] data_b;
  logic [7:0] q;
  logic [7:0] comb_q;

  active_probe_dut u_dut (
    .clk(clk),
    .rst_n(rst_n),
    .sel(sel),
    .mode(mode),
    .data_a(data_a),
    .data_b(data_b),
    .q(q),
    .comb_q(comb_q)
  );

  initial begin
    clk = 1'b0;
    forever #5 clk = ~clk;
  end

  initial begin
    rst_n  = 1'b0;
    sel    = 1'b0;
    mode   = 2'b00;
    data_a = 8'hA1;
    data_b = 8'hB1;

    #7;
    rst_n = 1'b1;
    sel   = 1'b1;       // q <= data_a on posedge at 15ns

    #10;
    sel    = 1'b0;
    mode   = 2'b01;
    data_b = 8'hB2;     // q <= data_b on posedge at 25ns

    #10;
    sel    = 1'b1;
    mode   = 2'b10;
    data_a = 8'hA3;     // q <= data_a on posedge at 35ns

    #10;
    force u_dut.q = 8'hF0; // force is active from 37ns to 43ns

    #6;
    release u_dut.q;

    #7;
    mode = 2'b11;       // comb_q takes the default/X branch at 50ns

    #15;
    $finish;
  end

endmodule
