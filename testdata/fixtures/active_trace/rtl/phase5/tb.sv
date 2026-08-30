`timescale 1ns/1ps

module top;
    logic [7:0] mask_a, mask_b;
    logic       en0, en1, en2;
    logic       ctrl_sel, ctrl_mode;
    logic [7:0] src_a, src_b, src_c, sp_val;
    logic [7:0] dout [8];
    logic [7:0] flag;

    phase5_dut #(.NUM_LANES(8), .W(8), .SP_IDX0(1), .SP_IDX1(6))
        u_dut (.*);

    initial begin
        // ── stable initial state ──
        mask_a = 8'hFF; mask_b = 8'hFF;
        en0=1; en1=1; en2=1;
        ctrl_sel=1; ctrl_mode=0;
        src_a=8'hA0; src_b=8'hB0; src_c=8'hC0; sp_val=8'h50;
        #5;  // settle

        // ── S1 @10ns: normal lane, src_b changes → ternary fn_op path ──
        // fn_op(src_b, src_c, ctrl_sel=1) = src_c. When src_b changes,
        // fn_op output depends on ctrl_sel and src_c, not src_b directly.
        // Actually: ctrl_sel=1, ctrl_mode=0 → ternary selects src_a.
        // Let's make ctrl_sel=0 so ternary goes to fn_op path.
        #5; ctrl_sel=0;  // @10ns: now ternary → fn_op(src_b, src_c, ctrl_sel)
        // ctrl_sel=0 → fn_op = sel ? b : a = 0 ? src_c : src_b = src_b
        // Change src_b at same time:
        src_b = 8'hB1;

        // ── S2 @20ns: ctrl_sel changes → ternary path switches ──
        #10; ctrl_sel=1;  // @20ns: ternary now → src_a (true path)

        // ── S3 @30ns: src_a changes (ternary true path active) ──
        #10; src_a = 8'hA1;  // @30ns

        // ── S4 @40ns: en1 changes → dout[2] should see en1 as cause ──
        #10; en1 = 0;     // @40ns: dout disabled
        #1;  en1 = 1;     // @41ns: dout enabled again

        // ── S5 @50ns: special lane SP_IDX0=1, sp_val changes ──
        #9;  sp_val = 8'h51;  // @50ns

        // ── S6 @60ns: special lane, normal-lane signals change ──
        #10; src_a = 8'hA2; src_b = 8'hB2;  // @60ns: should NOT affect dout[1]

        // ── S7 @70ns: flag[2], mask_a[2] changes ──
        #10; mask_a[2] = 0;  // @70ns: flag[2] drops
        #1;  mask_a[2] = 1;  // @71ns

        // ── S8 @80ns: flag[2], OR second path en1/ctrl_sel triggered ──
        // flag[ln] = (en2 & mask_a & mask_b) | (en1 & mask_a & (ctrl_sel|ctrl_mode))
        // Normal lane: en2=1, en1=1, mask_a[2]=1, mask_b[2]=1, ctrl_sel=1
        // Both OR branches are active. Change en1 to toggle second branch:
        #9; en1 = 0;  // @80ns: OR first branch still active via en2
        #1;  en1 = 1;  // @81ns

        // ── S9 @90ns: multiple lanes change simultaneously ──
        // Change src_a affects dout[0..7] except dout[1] and dout[6]
        #9;  src_a = 8'hA3;  // @90ns: all normal lanes change

        // ── S10 @100ns: only mask_a[3] changes ──
        #10; mask_a[3] = 0;  // @100ns: flag[3] drops, flag[2] should NOT change
        #1;  mask_a[3] = 1;

        // ── S11: settle ctrl_sel=0 at 105ns, only src_b changes at 110ns ──
        #5;  ctrl_sel=0;    // @105ns: settle, no query
        #5;  src_b=8'hB5;   // @110ns: only src_b toggles

        // ── S12: settle ctrl_mode at 115ns, only src_a changes at 120ns ──
        #5;  ctrl_mode=1;   // @115ns: settle
        #5;  src_a=8'hA5;   // @120ns: ternary true path, only src_a toggles

        // ── S13 @130ns: special lane, only sp_val changes ──
        #10; sp_val=8'h55;  // @130ns

        #10; $finish;
    end
endmodule
