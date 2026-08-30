`timescale 1ns/1ps

// P0-3: generate for + module instance — 3 scenes covering parallel bit changes

module leaf(input logic a, b, output logic y);
  assign y = a & b;
endmodule

module top;
  parameter int N = 4;
  logic [N-1:0] a, b, y;

  genvar i;
  generate
    for (i = 0; i < N; i++) begin : gen_u
      leaf u_leaf(.a(a[i]), .b(b[i]), .y(y[i]));
    end
  endgenerate

  initial begin
    a=0; b=0;

    // Scene A: only a[0] toggles at 10ns
    #10; a[0]=1; b[0]=1;

    // Scene B: a[0]+a[2] toggle simultaneously at 20ns
    #10; a[0]=0; a[2]=1; b[2]=1;

    // Scene C: all bits toggle at 30ns
    #10; a=4'b1111; b=4'b1111;

    #10; $finish;
  end
endmodule
