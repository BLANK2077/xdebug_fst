`timescale 1ns/1ps

// Pin-level, dependency-free semantic mirror of xdebug.apb_vip.  The
// proprietary SVT producer is intentionally not part of this fixture.
interface xdebug_svt_apb_if;
  logic [31:0] paddr;
  logic [31:0] pwdata;
  logic [31:0] prdata [0:0];
  logic        pwrite;
  logic        penable;
  logic [0:0]  psel;
  logic [0:0]  pready;
  logic [0:0]  pslverr;
  logic [3:0]  pstrb;
endinterface

module apb_slave_dut (
    input  logic        pclk,
    input  logic        presetn,
    input  logic [31:0] paddr,
    input  logic        psel,
    input  logic        penable,
    input  logic        pwrite,
    input  logic [31:0] pwdata,
    input  logic [3:0]  pstrb,
    output logic [31:0] prdata,
    output logic        pready,
    output logic        pslverr
);
  logic [31:0] registers [0:3];
  logic [2:0] wait_count;
  integer i;

  always_ff @(posedge pclk or negedge presetn) begin
    if (!presetn) begin
      prdata <= '0;
      pready <= 1'b0;
      pslverr <= 1'b0;
      wait_count <= '0;
      for (i = 0; i < 4; i++) registers[i] <= '0;
    end else begin
      pready <= 1'b0;
      pslverr <= 1'b0;

      if (psel && !penable) begin
        wait_count <= {1'b0, paddr[3:2]};
      end else if (psel && penable) begin
        if (wait_count != 0) begin
          wait_count <= wait_count - 1'b1;
        end else begin
          pready <= 1'b1;
          pslverr <= (paddr[7:0] == 8'hF0);
          if (paddr[7:0] == 8'hF0) begin
            prdata <= 32'hBAD0_00F0;
          end else if (pwrite) begin
            if (pstrb[0]) registers[paddr[3:2]][7:0] <= pwdata[7:0];
            if (pstrb[1]) registers[paddr[3:2]][15:8] <= pwdata[15:8];
            if (pstrb[2]) registers[paddr[3:2]][23:16] <= pwdata[23:16];
            if (pstrb[3]) registers[paddr[3:2]][31:24] <= pwdata[31:24];
          end else begin
            prdata <= registers[paddr[3:2]];
          end
        end
      end
    end
  end
endmodule

module apb_vip_fixture_top;
  logic clk;
  logic rst_n;
  xdebug_svt_apb_if apb_if();

  apb_slave_dut dut (
      .pclk(clk),
      .presetn(rst_n),
      .paddr(apb_if.paddr),
      .psel(apb_if.psel[0]),
      .penable(apb_if.penable),
      .pwrite(apb_if.pwrite),
      .pwdata(apb_if.pwdata),
      .pstrb(apb_if.pstrb),
      .prdata(apb_if.prdata[0]),
      .pready(apb_if.pready[0]),
      .pslverr(apb_if.pslverr[0])
  );

  initial begin
    clk = 1'b0;
    forever #5ns clk = ~clk;
  end

  initial begin
    rst_n = 1'b0;
    repeat (10) @(posedge clk);
    rst_n = 1'b1;
  end

  task automatic apb_transfer(
      input logic        is_write,
      input logic [31:0] address,
      input logic [31:0] data,
      input logic [3:0]  strobe
  );
    apb_if.psel[0] = 1'b1;
    apb_if.penable = 1'b0;
    apb_if.pwrite = is_write;
    apb_if.paddr = address;
    apb_if.pwdata = data;
    apb_if.pstrb = strobe;
    @(negedge clk);
    apb_if.penable = 1'b1;
    do @(negedge clk); while (apb_if.pready[0] !== 1'b1);
    @(posedge clk);
    @(negedge clk);
    apb_if.psel[0] = 1'b0;
    apb_if.penable = 1'b0;
  endtask

  initial begin : deterministic_svt_stimulus
    apb_if.paddr = '0;
    apb_if.pwdata = '0;
    apb_if.pwrite = 1'b0;
    apb_if.penable = 1'b0;
    apb_if.psel[0] = 1'b0;
    apb_if.pstrb = '0;
    wait (rst_n === 1'b1);
    @(negedge clk);

    apb_transfer(1'b1, 32'h00, 32'h1122_3344, 4'b1111);
    apb_transfer(1'b1, 32'h04, 32'h5566_7788, 4'b1111);
    apb_transfer(1'b1, 32'h08, 32'hA5A5_5A5A, 4'b1111);
    apb_transfer(1'b1, 32'h0C, 32'hDEAD_BEEF, 4'b1111);
    apb_transfer(1'b1, 32'h04, 32'h0000_ABCD, 4'b0011);
    apb_transfer(1'b0, 32'h00, '0, 4'b0000);
    apb_transfer(1'b0, 32'h04, '0, 4'b0000);
    apb_transfer(1'b0, 32'h08, '0, 4'b0000);
    apb_transfer(1'b0, 32'h0C, '0, 4'b0000);
    apb_transfer(1'b0, 32'hF0, '0, 4'b0000);

    repeat (2) @(posedge clk);
    $finish;
  end
endmodule
