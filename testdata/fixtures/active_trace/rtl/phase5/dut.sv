// Phase 5: procedural for + lane select + ternary/function
module phase5_dut #(
    parameter int NUM_LANES = 8,
    parameter int W = 8,
    parameter int SP_IDX0 = 1,
    parameter int SP_IDX1 = 6
)(
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
    function automatic logic [W-1:0] fn_op(
        input logic [W-1:0] a,
        input logic [W-1:0] b,
        input logic         sel
    );
        fn_op = sel ? b : a;
    endfunction

    always_comb begin : lane_proc
        for (int ln = 0; ln < NUM_LANES; ln = ln + 1) begin
            if ((ln == SP_IDX0) || (ln == SP_IDX1)) begin
                dout[ln] = sp_val;
                flag[ln] = en0 & mask_a[ln] & mask_b[ln];
            end
            else begin
                dout[ln] = en1 & ((ctrl_sel | ctrl_mode) ? src_a
                                 : fn_op(src_b, src_c, ctrl_sel));
                flag[ln] = (en2 & mask_a[ln] & mask_b[ln]) |
                           (en1 & mask_a[ln] & (ctrl_sel | ctrl_mode));
            end
        end
    end
endmodule
