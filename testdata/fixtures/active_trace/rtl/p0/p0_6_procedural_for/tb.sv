`timescale 1ns/1ps

// P0-6: procedural for mux — 4 scenes covering simultaneous changes
//   always_comb: for (i=0; i<N; i++) if (sel==i) y = a[i];

module top #(parameter int N = 4);
  logic [N-1:0] a;
  logic [$clog2(N)-1:0] sel;
  logic y;

  always_comb begin
    y = 1'b0;
    for (int i = 0; i < N; i++) begin
      if (sel == i)
        y = a[i];
    end
  end

  initial begin
    a=0; sel=2;

    // Scene A: sel=2 stable, only a[2] toggles @10ns
    #10; a[2]=1;            // A: unique source

    // Scene B: sel=2 stable, a[2] and a[1] both toggle @20ns
    #10; a[2]=0; a[1]=1;    // B: selected+unselected both toggle

    // Scene C: sel and a[1] (new selected) change @30ns
    #10; sel=1; a[1]=0;     // C: sel:2→1, new selected a[1]:1→0

    // Scene D: sel and a[0] (new selected) change @40ns
    #10; sel=0; a[0]=1;     // D: sel:1→0, new selected a[0]:0→1

    #10; $finish;
  end
endmodule
