// APB fixture: master (pin-driven) -> simple slave with 4x8 memory
module apb_slave (
    input  wire        pclk,
    input  wire        presetn,
    input  wire        psel,
    input  wire        penable,
    input  wire        pwrite,
    input  wire [7:0]  paddr,
    input  wire [7:0]  pwdata,
    output reg  [7:0]  prdata,
    output wire        pready,
    output wire        pslverr
);
    reg [7:0] mem [0:3];
    assign pready  = 1'b1;
    assign pslverr = 1'b0;
    always @(posedge pclk) begin
        if (!presetn) begin
            mem[0] <= 8'h00; mem[1] <= 8'h00; mem[2] <= 8'h00; mem[3] <= 8'h00;
            prdata <= 8'h00;
        end else begin
            if (psel && penable && pwrite)
                mem[paddr[1:0]] <= pwdata;
            if (psel && penable && !pwrite)
                prdata <= mem[paddr[1:0]];
        end
    end
endmodule

module apb_top (
    input  wire        pclk,
    input  wire        presetn,
    input  wire        psel,
    input  wire        penable,
    input  wire        pwrite,
    input  wire [7:0]  paddr,
    input  wire [7:0]  pwdata,
    output wire [7:0]  prdata,
    output wire        pready,
    output wire        pslverr
);
    apb_slave u_slave (
        .pclk(pclk), .presetn(presetn), .psel(psel), .penable(penable),
        .pwrite(pwrite), .paddr(paddr), .pwdata(pwdata),
        .prdata(prdata), .pready(pready), .pslverr(pslverr)
    );
endmodule
