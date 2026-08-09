module case_top (
    input  wire       clk,
    input  wire       reset,
    input  wire [1:0] sel,
    input  wire [7:0] data,
    output reg  [7:0] out,
    output reg  [7:0] out_casez,
    output reg  [7:0] out_casex
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

    reg [7:0] ternary_out;
    always @(posedge clk) ternary_out <= sel[0] ? data : 8'h5a;

    wire [7:0] multiple_driver_out;
    assign multiple_driver_out = data;
    assign multiple_driver_out = {6'b0, sel};

    reg [7:0] inside_out;
    always @(posedge clk) begin
        case (sel) inside
            2'b1?: inside_out <= data;
            [2'd0:2'd1]: inside_out <= data + 8'h02;
            default: inside_out <= 8'hfb;
        endcase
    end

    wire [7:0] inout_bus;
    assign inout_bus = data;
    inout_leaf u_inout (.bus(inout_bus));

    wire [7:0] nested_inout_bus;
    assign nested_inout_bus = data;
    inout_mid u_inout_mid (.bus(nested_inout_bus));

    wire [7:0] child_output_bus;
    output_leaf u_output (
        .data_i(data),
        .data_o(child_output_bus)
    );

    reg [7:0] procedural_multi_out;
    always @(posedge clk) begin
        if (!reset)
            procedural_multi_out <= data;
    end
    always @(posedge clk) begin
        if (sel[0])
            procedural_multi_out <= data + 8'h10;
    end
endmodule

module inout_leaf (
    inout wire [7:0] bus
);
endmodule

module inout_mid (
    inout wire [7:0] bus
);
    wire [7:0] leaf_bus;
    assign leaf_bus = bus;
    inout_leaf u_leaf (.bus(leaf_bus));
endmodule

module output_leaf (
    input  wire [7:0] data_i,
    output wire [7:0] data_o
);
    assign data_o = data_i;
endmodule
