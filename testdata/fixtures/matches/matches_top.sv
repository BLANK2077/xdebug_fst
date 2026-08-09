module matches_top (
    input  wire       clk,
    input  wire       reset,
    input  wire [1:0] sel,
    input  wire [7:0] data,
    output reg  [7:0] out
);
    always @(posedge clk) begin
        if (reset) begin
            out <= 8'h00;
        end else begin
            case (sel) matches
                2'd1: out <= data + 8'h01;
                default: out <= 8'hff;
            endcase
        end
    end

    reg [7:0] temporal_q;
    wire [7:0] temporal_out;
    assign temporal_out = temporal_q;
    always @(posedge clk) begin
        if (reset)
            temporal_q <= 8'h00;
        else
            temporal_q <= data;
    end
endmodule
