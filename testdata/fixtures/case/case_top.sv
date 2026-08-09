module case_top (
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
            case (sel)
                2'd0: out <= data;
                2'd1: out <= data + 8'h01;
                default: out <= 8'hff;
            endcase
        end
    end
endmodule
