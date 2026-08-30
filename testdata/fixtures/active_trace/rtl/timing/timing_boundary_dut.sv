// Phase 3: always @(posedge clk) timing boundary DUT
// Pre: NUM_PRE assign layers, Post: NUM_POST assign layers
// MUX_POS: bitmask of positions where mux is inserted (bit 0=pos0, etc.)

module timing_boundary_dut #(
    parameter NUM_PRE  = 5,
    parameter NUM_POST = 5,
    parameter int MUX_PRE  = 0,   // bitmask of pre layers with mux (e.g. 4 = bit2)
    parameter int MUX_POST = 0    // bitmask of post layers with mux
) (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    input  logic       mux_sel,
    output logic [7:0] data_out
);
    // ── pre assign chain ──
    logic [7:0] pre [0:NUM_PRE];
    assign pre[0] = data_in;
    generate for (genvar i = 0; i < NUM_PRE; i++) begin : gen_pre
        if ((MUX_PRE >> i) & 1)
            assign pre[i+1] = mux_sel ? pre[i] : 8'hAA;
        else
            assign pre[i+1] = pre[i];
    end endgenerate

    // ── timing boundary: always @(posedge clk) ──
    logic [7:0] mid;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) mid <= 8'h00;
        else        mid <= pre[NUM_PRE];
    end

    // ── post assign chain ──
    logic [7:0] post [0:NUM_POST];
    assign post[0] = mid;
    generate for (genvar i = 0; i < NUM_POST; i++) begin : gen_post
        if ((MUX_POST >> i) & 1)
            assign post[i+1] = mux_sel ? post[i] : 8'h55;
        else
            assign post[i+1] = post[i];
    end endgenerate

    assign data_out = post[NUM_POST];
endmodule
