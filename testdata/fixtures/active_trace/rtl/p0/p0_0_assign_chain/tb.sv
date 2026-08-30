`timescale 1ns/1ps

// P0-0: continuous assign chain
//   assign a   = in0 & in1;
//   assign b   = a;
//   assign out = b;
// Expect: out → b → a → in0/in1 (primary input stop)

module top;
  logic in0, in1;
  logic a, b, out;

  assign a   = in0 & in1;
  assign b   = a;
  assign out = b;

  initial begin
    in0 = 1'b0;
    in1 = 1'b0;
    #5;
    in0 = 1'b1;
    in1 = 1'b1;  // out becomes 1 at 5ns
    #20;
    $finish;
  end
endmodule
