`timescale 1ns/1ps

// P0-1: flop temporal boundary — 3 scenes covering race conditions
//   assign comb = in0 & in1;
//   always_ff @(posedge clk) q <= comb;
//   assign out = q;

module top;
  logic       clk;
  logic       in0, in1;
  logic       comb, q, out;

  assign comb = in0 & in1;
  assign out  = q;

  always_ff @(posedge clk)
    q <= comb;

  // clock: period 10ns, posedge at 5,15,25,35,45...
  initial begin
    clk = 1'b0;
    forever #5 clk = ~clk;
  end

  initial begin
    in0=0; in1=0;

    // Scene A: comb changes at 3ns (well before posedge@15ns)
    #3; in0=1; in1=1;       // comb:0→1 at 3ns, sampled at 15ns

    // Scene B: comb changes exactly at posedge
    #12;                    // now at 15ns, clk edge
    in0=0;                  // comb:1→0 at 15ns = same time as posedge (race)

    // Scene C: comb changes after posedge@25ns
    #10;                    // now at 25ns, posedge sampled old comb
    #2; in0=1; in1=1;       // comb:0→1 at 27ns, after posedge

    #10; $finish;
  end
endmodule
