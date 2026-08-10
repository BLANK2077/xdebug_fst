// ref_port_top.sv — ref-port active-trace fixture
// BSD-3-Clause License

module ref_stage(
    input  logic [7:0] source,
    ref    logic [7:0] link,
    output logic [7:0] observed
);
    assign link = source;
    assign observed = link;
endmodule

module ref_port_top(
    input  logic [7:0] source,
    output logic [7:0] observed
);
    logic [7:0] link;
    ref_stage u_ref(.source(source), .link(link), .observed(observed));
endmodule
