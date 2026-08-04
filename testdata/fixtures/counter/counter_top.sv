module counter_top (
    input  wire clk,
    input  wire reset,
    output reg  [7:0] count,
    output reg  overflow
);
    always @(posedge clk) begin
        if (reset) begin
            count <= 8'h00;
            overflow <= 1'b0;
        end else begin
            count <= count + 8'h01;
            overflow <= (count == 8'hff);
        end
    end
endmodule
