module output_mixed_top (
    input  wire [7:0] child_data,
    input  wire [7:0] parent_data,
    output wire [7:0] probe
);
    wire [7:0] mixed_bus;

    output_mixed_leaf u_leaf (
        .data_i(child_data),
        .data_o(mixed_bus)
    );

    assign mixed_bus = parent_data;
    assign probe = mixed_bus;
endmodule

module output_mixed_leaf (
    input  wire [7:0] data_i,
    output wire [7:0] data_o
);
    assign data_o = data_i;
endmodule
