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

  reg [7:0] changed_q;
  wire [7:0] changed_out;
  assign changed_out = changed_q;
  always @(clk)
    changed_q <= data;

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

  reg [7:0] mixed_q;
  wire [7:0] mixed_out;
  assign mixed_out = mixed_q;
  always @(posedge clk) begin
    mixed_q = 8'h33;
    mixed_q <= data;
  end

  reg [7:0] exact_match_out;
  reg [7:0] wildcard_match_out;
  always_comb begin
    if (sel matches 2'd1)
      exact_match_out = data;
    else
      exact_match_out = 8'h00;
    if (sel matches .*)
      wildcard_match_out = data;
    else
      wildcard_match_out = 8'h00;
  end

  reg [7:0] double_nba_q;
  wire [7:0] double_nba_out;
  assign double_nba_out = double_nba_q;
  always @(posedge clk) begin
    double_nba_q <= 8'h44;
    double_nba_q <= data;
  end

  reg [7:0] default_only_out;
  always_comb begin
    case (sel) matches
      default: default_only_out = data;
    endcase
  end

  reg [7:0] hold_q;
  wire [7:0] hold_out;
  assign hold_out = hold_q;
  always @(posedge clk) begin
    if (sel == 2'd1)
      hold_q <= data;
    else
      hold_q <= hold_q;
  end

  reg [7:0] gated_q;
  wire [7:0] gated_out;
  assign gated_out = gated_q;
  always @(posedge clk) begin
    if (sel == 2'd1)
      gated_q <= data;
  end

  reg [7:0] ternary_hold_q;
  wire [7:0] ternary_hold_out;
  assign ternary_hold_out = ternary_hold_q;
  always @(posedge clk)
    ternary_hold_q <= sel[0] ? ternary_hold_q : data;
endmodule
