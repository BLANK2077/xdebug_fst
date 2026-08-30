`timescale 1ns/1ps

// P0-2: module boundary
//   leaf.u_leaf(.a(a), .b(b), .y(y));
// Expect: top.y → top.u_leaf.y → top.u_leaf.a/b → top.a/top.b

module leaf(input logic a, b, output logic y);
  assign y = a & b;
endmodule

module top;
  logic a, b, y;
  leaf u_leaf(.a(a), .b(b), .y(y));

  initial begin
    a = 1'b0;
    b = 1'b0;
    #5;
    a = 1'b1;
    b = 1'b1;  // y becomes 1
    #20;
    $finish;
  end
endmodule
