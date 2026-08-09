// AXI4 fixture: master (pin-driven BFM) -> simple slave (256x32 memory)
module axi_slave #(
    parameter AW = 8,
    parameter DW = 32,
    parameter ID = 4
)(
    input  wire              aclk,
    input  wire              aresetn,
    input  wire [ID-1:0]     awid,
    input  wire [AW-1:0]     awaddr,
    input  wire [7:0]        awlen,
    input  wire [2:0]        awsize,
    input  wire [1:0]        awburst,
    input  wire              awvalid,
    output wire              awready,
    input  wire [DW-1:0]     wdata,
    input  wire [(DW/8)-1:0] wstrb,
    input  wire              wlast,
    input  wire              wvalid,
    output wire              wready,
    output reg  [ID-1:0]     bid,
    output reg  [1:0]        bresp,
    output reg               bvalid,
    input  wire              bready,
    input  wire [ID-1:0]     arid,
    input  wire [AW-1:0]     araddr,
    input  wire [7:0]        arlen,
    input  wire [2:0]        arsize,
    input  wire [1:0]        arburst,
    input  wire              arvalid,
    output wire              arready,
    output reg  [ID-1:0]     rid,
    output reg  [DW-1:0]     rdata,
    output reg  [1:0]        rresp,
    output reg               rlast,
    output reg               rvalid,
    input  wire              rready
);
    reg [DW-1:0] mem [0:255];
    reg [ID-1:0] awid_q; reg [AW-1:0] awaddr_q; reg [7:0] awlen_q;
    reg [7:0] wcnt;
    reg [ID-1:0] arid_q; reg [AW-1:0] araddr_q; reg [7:0] arlen_q;
    reg [7:0] rcnt;
    reg ar_pending;

    // write channel: always ready (single slave, accepts every cycle)
    assign awready = 1'b1;
    assign wready  = 1'b1;

    always @(posedge aclk) begin
        if (!aresetn) begin
            awid_q <= 0; awaddr_q <= 0; awlen_q <= 0; wcnt <= 0;
            bvalid <= 0; bid <= 0; bresp <= 0;
        end else begin
            if (awvalid && awready) begin
                awid_q <= awid; awaddr_q <= awaddr; awlen_q <= awlen; wcnt <= 0;
            end
            if (wvalid && wready) begin
                mem[awaddr_q + wcnt] <= wdata;
                wcnt <= wcnt + 1;
            end
            if (wvalid && wready && wlast) begin
                bvalid <= 1; bid <= awid_q; bresp <= 0; wcnt <= 0;
            end
            if (bvalid && bready) bvalid <= 0;
        end
    end

    // read channel: accept address, then stream rdata beats
    assign arready = !rvalid;

    always @(posedge aclk) begin
        if (!aresetn) begin
            arid_q <= 0; araddr_q <= 0; arlen_q <= 0; rcnt <= 0;
            rvalid <= 0; rid <= 0; rdata <= 0; rresp <= 0; rlast <= 0;
            ar_pending <= 0;
        end else begin
            if (arvalid && arready) begin
                arid_q <= arid; araddr_q <= araddr; arlen_q <= arlen;
                ar_pending <= 1;
            end
            if (ar_pending && !rvalid) begin
                rvalid <= 1; rid <= arid_q; rresp <= 0;
                rdata <= mem[araddr_q];
                rlast <= (arlen_q == 0);
                rcnt <= 1;
                ar_pending <= 0;
            end
            if (rvalid && rready) begin
                if (rlast) begin
                    rvalid <= 0; rlast <= 0;
                end else begin
                    rdata <= mem[araddr_q + rcnt];
                    rlast <= (rcnt == arlen_q);
                    rcnt <= rcnt + 1;
                end
            end
        end
    end
endmodule

module axi_top (
    input  wire        aclk,
    input  wire        aresetn,
    input  wire [3:0]  awid,   input  wire [7:0] awaddr, input  wire [7:0] awlen,
    input  wire [2:0]  awsize, input  wire [1:0] awburst,
    input  wire        awvalid, output wire        awready,
    input  wire [31:0] wdata,  input  wire [3:0]  wstrb,  input  wire        wlast,
    input  wire        wvalid, output wire        wready,
    output wire [3:0]  bid,    output wire [1:0]  bresp,  output wire        bvalid, input wire bready,
    input  wire [3:0]  arid,   input  wire [7:0]  araddr, input  wire [7:0]  arlen,
    input  wire [2:0]  arsize, input  wire [1:0] arburst,
    input  wire        arvalid, output wire        arready,
    output wire [3:0]  rid,    output wire [31:0] rdata,  output wire [1:0]  rresp,
    output wire        rlast,  output wire        rvalid, input  wire        rready
);
    axi_slave #(.AW(8), .DW(32), .ID(4)) u_slave (
        .aclk(aclk), .aresetn(aresetn),
        .awid(awid), .awaddr(awaddr), .awlen(awlen), .awsize(awsize),
        .awburst(awburst), .awvalid(awvalid), .awready(awready),
        .wdata(wdata), .wstrb(wstrb), .wlast(wlast), .wvalid(wvalid), .wready(wready),
        .bid(bid), .bresp(bresp), .bvalid(bvalid), .bready(bready),
        .arid(arid), .araddr(araddr), .arlen(arlen), .arsize(arsize),
        .arburst(arburst), .arvalid(arvalid), .arready(arready),
        .rid(rid), .rdata(rdata), .rresp(rresp), .rlast(rlast), .rvalid(rvalid), .rready(rready)
    );
endmodule
