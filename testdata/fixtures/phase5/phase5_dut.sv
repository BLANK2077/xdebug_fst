// Phase-5 procedural-for, lane-select, ternary, and function fixture.
// BSD-3-Clause License
`timescale 1ns/1ps

module phase5_dut #(
    parameter int NUM_LANES = 8,
    parameter int W = 8,
    parameter int SP_IDX0 = 1,
    parameter int SP_IDX1 = 6
) (
    input  logic [NUM_LANES-1:0] mask_a,
    input  logic [NUM_LANES-1:0] mask_b,
    input  logic                 en0,
    input  logic                 en1,
    input  logic                 en2,
    input  logic                 ctrl_sel,
    input  logic                 ctrl_mode,
    input  logic [W-1:0]         src_a,
    input  logic [W-1:0]         src_b,
    input  logic [W-1:0]         src_c,
    input  logic [W-1:0]         sp_val,
    output logic [W-1:0]         dout [NUM_LANES],
    output logic [NUM_LANES-1:0] flag
);
    function automatic logic [W-1:0] select_source(
        input logic [W-1:0] a,
        input logic [W-1:0] b,
        input logic         select_b
    );
        select_source = select_b ? b : a;
    endfunction

    always_comb begin : lane_process
        for (int lane = 0; lane < NUM_LANES; lane = lane + 1) begin
            if ((lane == SP_IDX0) || (lane == SP_IDX1)) begin
                dout[lane] = sp_val;  // PHASE5_SPECIAL_DOUT
                flag[lane] = en0 & mask_a[lane] & mask_b[lane];
            end else begin
                dout[lane] = en1 & ((ctrl_sel | ctrl_mode) ? src_a
                    : select_source(src_b, src_c, ctrl_sel));  // PHASE5_NORMAL_DOUT
                flag[lane] = (en2 & mask_a[lane] & mask_b[lane])
                    | (en1 & mask_a[lane] & (ctrl_sel | ctrl_mode));
            end
        end
    end
endmodule
