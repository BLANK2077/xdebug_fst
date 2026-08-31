`timescale 1ns/1ps

// Dependency-free pin-level semantic mirror of xdebug.apb_xamba_vip.
// Product XAMBA/UVM sources are provenance only and are not compiled here.
interface xdebug_xamba_apb_if;
  logic        pclk;
  logic        presetn;
  logic [31:0] paddr;
  logic        psel;
  logic        penable;
  logic        pwrite;
  logic [31:0] pwdata;
  logic [31:0] prdata;
  logic        pready;
  logic        pslverr;
  logic [3:0]  pstrb;
  logic [2:0]  pprot;
  logic        pnse;
endinterface

module xdebug_apb_xamba_fixture_dut;
  xdebug_xamba_apb_if apb_reply_if();

  initial begin
    apb_reply_if.pclk = 1'b0;
    forever #5ns apb_reply_if.pclk = ~apb_reply_if.pclk;
  end

  initial begin
    apb_reply_if.presetn = 1'b0;
    repeat (2) @(negedge apb_reply_if.pclk);
    apb_reply_if.presetn = 1'b1;
  end

  task automatic send_transfer(input int unsigned index);
    logic is_write;
    int unsigned wait_cycles;

    is_write = ((index % 2) == 0);
    wait_cycles = index % 4;
    apb_reply_if.psel = 1'b1;
    apb_reply_if.penable = 1'b0;
    apb_reply_if.pwrite = is_write;
    apb_reply_if.paddr = 32'h0000_1000 + (index * 4);
    apb_reply_if.pwdata = 32'ha500_0000 | index;
    apb_reply_if.prdata = 32'h5a00_0000 | index;
    apb_reply_if.pstrb = is_write ? (4'b0001 << (index % 4)) : 4'b0000;
    apb_reply_if.pprot = index[2:0];
    apb_reply_if.pnse = index[3];
    apb_reply_if.pready = 1'b0;
    apb_reply_if.pslverr = 1'b0;

    @(negedge apb_reply_if.pclk);
    apb_reply_if.penable = 1'b1;
    repeat (wait_cycles) @(negedge apb_reply_if.pclk);
    @(negedge apb_reply_if.pclk);
    apb_reply_if.pready = 1'b1;
    apb_reply_if.pslverr = ((index % 11) == 0);
    @(posedge apb_reply_if.pclk);
    @(negedge apb_reply_if.pclk);

    apb_reply_if.psel = 1'b0;
    apb_reply_if.penable = 1'b0;
    apb_reply_if.pwrite = 1'b0;
    apb_reply_if.pready = 1'b0;
    apb_reply_if.pslverr = 1'b0;
    apb_reply_if.pstrb = 4'b0000;
    apb_reply_if.pprot = 3'b000;
    apb_reply_if.pnse = 1'b0;
  endtask

  initial begin : deterministic_xamba_stimulus
    apb_reply_if.paddr = '0;
    apb_reply_if.psel = 1'b0;
    apb_reply_if.penable = 1'b0;
    apb_reply_if.pwrite = 1'b0;
    apb_reply_if.pwdata = '0;
    apb_reply_if.prdata = '0;
    apb_reply_if.pready = 1'b0;
    apb_reply_if.pslverr = 1'b0;
    apb_reply_if.pstrb = '0;
    apb_reply_if.pprot = '0;
    apb_reply_if.pnse = 1'b0;
    wait (apb_reply_if.presetn === 1'b1);
    @(posedge apb_reply_if.pclk);

    for (int unsigned index = 0; index < 64; index++)
      send_transfer(index);

    repeat (2) @(posedge apb_reply_if.pclk);
    $finish;
  end
endmodule

module xdebug_apb_xamba_fixture_top;
  xdebug_apb_xamba_fixture_dut dut();
endmodule
