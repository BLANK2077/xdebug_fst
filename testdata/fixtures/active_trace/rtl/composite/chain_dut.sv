// ═══════════════════════════════════════════════════════════════════
// chain_dut: Parameterized multi-stage driver chain
// Each stage can be enabled/disabled via parameter.
// Disabled stages become pass-through wires.
// ═══════════════════════════════════════════════════════════════════

// ─── M: pass-through module (module boundary stage) ──────────────
module pass_through (
    input  logic [7:0] in,
    output logic [7:0] out
);
    assign out = in;
endmodule

// ─── G: generate-for pass-through ────────────────────────────────
module generate_pass_through #(parameter int W = 8) (
    input  logic [W-1:0] in,
    output logic [W-1:0] out
);
    genvar i;
    generate
        for (i = 0; i < W; i++) begin : gen_bit
            assign out[i] = in[i];
        end
    endgenerate
endmodule

// ─── I: interface ─────────────────────────────────────────────────
interface chain_if (input logic clk);
    logic [7:0] data;
    modport source (output data, input clk);
    modport sink   (input data, input clk);
endinterface

// ─── interface source driver module ───────────────────────────────
module if_source_drv (
    input  logic [7:0] in,
    chain_if.source    bus
);
    assign bus.data = in;
endmodule

// ─── interface sink consumer module ───────────────────────────────
module if_sink_rcv (
    chain_if.sink      bus,
    output logic [7:0] out
);
    assign out = bus.data;
endmodule

// ═══════════════════════════════════════════════════════════════════
// Main parameterized DUT
// ═══════════════════════════════════════════════════════════════════
module chain_dut #(
    parameter int ENABLE_ASSIGN   = 1,  // A: assign pass-through
    parameter int ENABLE_FLOP     = 1,  // F: flop temporal boundary
    parameter int ENABLE_MODULE   = 1,  // M: module boundary
    parameter int ENABLE_GENERATE = 1,  // G: generate for
    parameter int ENABLE_IFACE    = 1,  // I: interface modport
    parameter int ENABLE_MUX      = 1,  // B: mux branch
    parameter int ENABLE_PROCFOR  = 1   // P: procedural for
) (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    input  logic [1:0] mux_sel,
    input  logic [1:0] proc_sel,
    output logic [7:0] data_out
);
    // ── internal pipeline wires ──
    logic [7:0] s_in, s_a, s_f, s_m, s_g, s_i, s_b, s_p;

    assign s_in = data_in;

    // ── A: continuous assign pass-through ──
    generate if (ENABLE_ASSIGN) begin : g_a
        assign s_a = s_in;
    end else begin : g_a_bypass
        assign s_a = s_in;
    end endgenerate

    // ── F: flop temporal boundary ──
    logic [7:0] flop_q;
    generate if (ENABLE_FLOP) begin : g_f
        always_ff @(posedge clk or negedge rst_n)
            if (!rst_n) flop_q <= 8'h00;
            else        flop_q <= s_a;
        assign s_f = flop_q;
    end else begin : g_f_bypass
        assign s_f = s_a;
    end endgenerate

    // ── M: module boundary ──
    logic [7:0] mod_out;
    generate if (ENABLE_MODULE) begin : g_m
        pass_through u_m (.in(s_f), .out(mod_out));
        assign s_m = mod_out;
    end else begin : g_m_bypass
        assign s_m = s_f;
    end endgenerate

    // ── G: generate-for ──
    logic [7:0] gen_out;
    generate if (ENABLE_GENERATE) begin : g_g
        generate_pass_through #(8) u_gen (.in(s_m), .out(gen_out));
        assign s_g = gen_out;
    end else begin : g_g_bypass
        assign s_g = s_m;
    end endgenerate

    // ── I: interface modport ──
    chain_if iface(.clk(clk));
    logic [7:0] iface_out;
    generate if (ENABLE_IFACE) begin : g_i
        if_source_drv u_src (.in(s_g), .bus(iface.source));
        if_sink_rcv   u_snk (.bus(iface.sink), .out(iface_out));
        assign s_i = iface_out;
    end else begin : g_i_bypass
        assign s_i = s_g;
    end endgenerate

    // ── B: mux branch ──
    logic [7:0] other_source;
    assign other_source = 8'hAA;  // constant alternate source
    generate if (ENABLE_MUX) begin : g_b
        assign s_b = mux_sel[0] ? s_i : other_source;
    end else begin : g_b_bypass
        assign s_b = s_i;
    end endgenerate

    // ── P: procedural for ──
    logic [7:0] proc_out;
    generate if (ENABLE_PROCFOR) begin : g_p
        always_comb begin
            proc_out = 8'h00;
            for (int i = 0; i < 8; i++) begin
                if (proc_sel == i[1:0])
                    proc_out = s_b;
            end
        end
        assign s_p = proc_out;
    end else begin : g_p_bypass
        assign s_p = s_b;
    end endgenerate

    assign data_out = s_p;
endmodule
