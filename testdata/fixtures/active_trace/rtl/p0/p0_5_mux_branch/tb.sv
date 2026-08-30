`timescale 1ns/1ps

// P0-5: mux branch — 8 scenes covering simultaneous changes
//   assign y = sel ? a : b;
//   Scenes A-H: precisely timed independent/simultaneous toggles

module top;
  logic a, b, sel, y;

  assign y = sel ? a : b;

  initial begin
    a=0; b=0; sel=0;

    // Scene A: sel=1, only a toggles @10ns
    #10; sel=1; a=1;        // a:0→1, b:stable 0, sel:0→1

    // Scene B: sel=1, both a and b toggle @20ns
    #10; a=0; b=1;          // a:1→0, b:0→1

    // Scene C: only sel toggles @30ns
    #10; sel=0;             // sel:1→0, a/b stable

    // Scene D: sel=0, only b toggles @40ns
    #10; b=0;               // b:1→0

    // Scene E: sel=0, b and sel toggle @50ns
    #10; sel=1; b=1;        // sel:0→1, b:0→1

    // Scene F: sel=1, a and sel toggle @60ns
    #10; sel=0; a=1;        // sel:1→0, a:0→1

    // Scene G: sel=0, both a and b toggle @70ns
    #10; a=0; b=0;          // sel=0 stable, a:1→0, b:1→0

    // Scene H: all toggle @80ns
    #10; sel=1; a=1; b=1;   // all toggling

    #10; $finish;
  end
endmodule
