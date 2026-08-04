module xprop_child (
    input  wire clk,
    input  wire [7:0] a,
    output reg  [7:0] y
);
    always @(posedge clk) begin
        y <= a & 8'h0f;
    end
endmodule

module xprop_top (
    input  wire clk,
    input  wire reset,
    output wire [7:0] out
);
    reg [7:0] a = 8'hxx;  // declared X initial
    wire [7:0] y;
    xprop_child u_child (.clk(clk), .a(a), .y(y));
    assign out = y;
    always @(posedge clk) begin
        if (reset) a <= 8'h00;
        else a <= a + 8'h01;
    end
endmodule
