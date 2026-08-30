// Phase 4: composite pre-chain + always @(posedge clk) + simple post-chain
// Pre: chain_dut from composite/ (A/F/M/G/I/B/P combinations)
// Boundary: always @(posedge clk) flop
// Post: 2-3 simple assign layers

module phase4_dut #(
    parameter int PRE_A = 1, PRE_F = 1, PRE_M = 1, PRE_G = 1,
    parameter int PRE_I = 1, PRE_B = 1, PRE_P = 1,
    parameter int NUM_POST = 3
) (
    input  logic clk, rst_n,
    input  logic [7:0] data_in,
    input  logic [1:0] mux_sel,
    input  logic [1:0] proc_sel,
    output logic [7:0] data_out
);
    // ── pre-chain (reuse chain_dut) ──
    logic [7:0] pre_out;
    chain_dut #(
        .ENABLE_ASSIGN  (PRE_A), .ENABLE_FLOP    (PRE_F),
        .ENABLE_MODULE  (PRE_M), .ENABLE_GENERATE(PRE_G),
        .ENABLE_IFACE   (PRE_I), .ENABLE_MUX     (PRE_B),
        .ENABLE_PROCFOR (PRE_P)
    ) u_pre (.clk, .rst_n, .data_in, .mux_sel, .proc_sel, .data_out(pre_out));

    // ── always @(posedge clk) timing boundary ──
    logic [7:0] mid;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) mid <= 8'h00; else mid <= pre_out;

    // ── post-chain (simple assign layers) ──
    logic [7:0] p1, p2, p3;
    assign p1 = mid;
    assign p2 = p1;
    assign p3 = p2;
    // Use generate to avoid ternary in the chain
    generate
        if (NUM_POST >= 3) assign data_out = p3;
        else if (NUM_POST >= 2) assign data_out = p2;
        else assign data_out = p1;
    endgenerate
endmodule
