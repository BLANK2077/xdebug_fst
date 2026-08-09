module case_top (
    input  wire       clk,
    input  wire       reset,
    input  wire [1:0] sel,
    input  wire [7:0] data,
    output reg  [7:0] out,
    output reg  [7:0] out_casez,
    output reg  [7:0] out_casex,
    output reg  [7:0] ternary_out
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

    always @(posedge clk) begin
        if (reset) begin
            out_casez <= 8'h00;
            out_casex <= 8'h00;
        end else begin
            casez (sel)
                2'b1z: out_casez <= data;
                default: out_casez <= 8'hfe;
            endcase
            casex (sel)
                2'bx1: out_casex <= data;
                default: out_casex <= 8'hfd;
            endcase
        end
    end

    reg [7:0] nested_out;
    always @(posedge clk) begin
        if (reset)
            nested_out <= 8'h00;
        else if (sel == 2'd0)
            nested_out <= data;
        else if (sel == 2'd1)
            nested_out <= data + 8'h01;
        else
            nested_out <= 8'hfc;
    end

    always @(posedge clk) ternary_out <= sel[0] ? data : 8'h5a;
endmodule
