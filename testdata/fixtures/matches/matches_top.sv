module matches_top (
    input  wire       clk,
    input  wire       reset,
    input  wire       async_reset_n,
    input  wire       force_en,
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
  wire [7:0] temporal_mid;
  wire [7:0] temporal_deep;
  assign temporal_out = temporal_q;
  assign temporal_mid = temporal_q;
  assign temporal_deep = temporal_mid;
  always @(posedge clk) begin
        if (reset)
            temporal_q <= 8'h00;
        else
            temporal_q <= data;
    end

  reg [7:0] async_q;
  wire [7:0] async_out;
  assign async_out = async_q;
  always @(posedge clk or negedge async_reset_n) begin
        if (!async_reset_n)
            async_q <= 8'h00;
        else
            async_q <= data;
    end

  reg [7:0] forced_q;
  wire [7:0] forced_out;
  assign forced_out = forced_q;
  always @(posedge clk)
    forced_q <= 8'h11;
  always @(posedge clk) begin
    if (force_en)
      force forced_q = data;
    else
      release forced_q;
  end
endmodule
