// interface_modport_top.sv — interface/modport active-trace fixture
// BSD-3-Clause License

interface trace_bus_if;
    logic [7:0] data;
    modport source(output data);
    modport sink(input data);
endinterface

module trace_source(input logic [7:0] data_i, trace_bus_if.source bus);
    assign bus.data = data_i;
endmodule

module trace_sink(trace_bus_if.sink bus, output logic [7:0] data_o);
    assign data_o = bus.data;
endmodule

module interface_modport_top(
    input  logic [7:0] source,
    output logic [7:0] observed
);
    trace_bus_if bus();
    trace_source u_source(.data_i(source), .bus(bus.source));
    trace_sink u_sink(.bus(bus.sink), .data_o(observed));
endmodule
