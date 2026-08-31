`timescale 1ns/1ps

// Dependency-free pin-level semantic mirror of xdebug.axi_xamba_vip.
interface xdebug_xamba_axi_if;
  logic         aclk;
  logic         aresetn;
  logic [63:0]  awaddr;
  logic [3:0]   awid;
  logic [7:0]   awlen;
  logic [2:0]   awsize;
  logic [1:0]   awburst;
  logic         awvalid;
  logic         awready;
  logic [127:0] wdata;
  logic [15:0]  wstrb;
  logic         wlast;
  logic         wvalid;
  logic         wready;
  logic [3:0]   bid;
  logic [1:0]   bresp;
  logic         bvalid;
  logic         bready;
  logic [63:0]  araddr;
  logic [3:0]   arid;
  logic [7:0]   arlen;
  logic [2:0]   arsize;
  logic [1:0]   arburst;
  logic         arvalid;
  logic         arready;
  logic [3:0]   rid;
  logic [127:0] rdata;
  logic [1:0]   rresp;
  logic         rlast;
  logic         rvalid;
  logic         rready;
endinterface

module xdebug_axi_xamba_fixture_dut;
  xdebug_xamba_axi_if probe_full_if();

  initial begin
    probe_full_if.aclk = 1'b0;
    forever #5ns probe_full_if.aclk = ~probe_full_if.aclk;
  end

  initial begin
    probe_full_if.aresetn = 1'b0;
    repeat (2) @(negedge probe_full_if.aclk);
    probe_full_if.aresetn = 1'b1;
  end

  function automatic logic [1:0] response_for(input int unsigned index);
    if ((index % 7) == 0)
      return 2'b11;
    if ((index % 5) == 0)
      return 2'b10;
    return 2'b00;
  endfunction

  task automatic clear_request_channels;
    probe_full_if.awvalid = 1'b0;
    probe_full_if.awready = 1'b0;
    probe_full_if.wvalid = 1'b0;
    probe_full_if.wready = 1'b0;
    probe_full_if.wlast = 1'b0;
    probe_full_if.arvalid = 1'b0;
    probe_full_if.arready = 1'b0;
  endtask

  task automatic send_write(input int unsigned index);
    int unsigned beats = (index % 4) + 1;
    probe_full_if.awid = 4'(index % 16);
    probe_full_if.awaddr = 64'h0000_0000_1000_0000 + (64'(index) * 64);
    probe_full_if.awlen = 8'(beats - 1);
    probe_full_if.awsize = 3'd4;
    probe_full_if.awburst = 2'b01;
    probe_full_if.awvalid = 1'b1;
    repeat (index % 3) @(posedge probe_full_if.aclk);
    probe_full_if.awready = 1'b1;
    @(posedge probe_full_if.aclk);
    probe_full_if.awvalid = 1'b0;
    probe_full_if.awready = 1'b0;

    for (int unsigned beat = 0; beat < beats; beat++) begin
      probe_full_if.wdata = {64'(index), 32'(beat), 32'h5a5a_0000 | beat};
      probe_full_if.wstrb = 16'hffff;
      probe_full_if.wlast = (beat == beats - 1);
      probe_full_if.wvalid = 1'b1;
      repeat ((index + beat) % 2) @(posedge probe_full_if.aclk);
      probe_full_if.wready = 1'b1;
      @(posedge probe_full_if.aclk);
      probe_full_if.wvalid = 1'b0;
      probe_full_if.wready = 1'b0;
      probe_full_if.wlast = 1'b0;
    end

    probe_full_if.bid = 4'(index % 16);
    probe_full_if.bresp = response_for(index);
    probe_full_if.bvalid = 1'b1;
    @(posedge probe_full_if.aclk);
    probe_full_if.bvalid = 1'b0;
  endtask

  task automatic send_read(input int unsigned index);
    int unsigned beats = (index % 4) + 1;
    probe_full_if.arid = 4'(index % 16);
    probe_full_if.araddr = 64'h0000_0000_2000_0000 + (64'(index) * 64);
    probe_full_if.arlen = 8'(beats - 1);
    probe_full_if.arsize = 3'd4;
    probe_full_if.arburst = 2'b01;
    probe_full_if.arvalid = 1'b1;
    repeat (index % 3) @(posedge probe_full_if.aclk);
    probe_full_if.arready = 1'b1;
    @(posedge probe_full_if.aclk);
    probe_full_if.arvalid = 1'b0;
    probe_full_if.arready = 1'b0;

    for (int unsigned beat = 0; beat < beats; beat++) begin
      probe_full_if.rid = 4'(index % 16);
      probe_full_if.rdata = {
        64'hcafe_0000_0000_0000 | 64'(index),
        32'(beat), 32'h1234_0000 | beat
      };
      probe_full_if.rresp = response_for(index);
      probe_full_if.rlast = (beat == beats - 1);
      probe_full_if.rvalid = 1'b1;
      @(posedge probe_full_if.aclk);
      probe_full_if.rvalid = 1'b0;
      probe_full_if.rlast = 1'b0;
    end
  endtask

  initial begin : deterministic_xamba_axi_stimulus
    probe_full_if.awaddr = '0;
    probe_full_if.awid = '0;
    probe_full_if.awlen = '0;
    probe_full_if.awsize = '0;
    probe_full_if.awburst = '0;
    probe_full_if.wdata = '0;
    probe_full_if.wstrb = '0;
    probe_full_if.bid = '0;
    probe_full_if.bresp = '0;
    probe_full_if.bvalid = 1'b0;
    probe_full_if.bready = 1'b1;
    probe_full_if.araddr = '0;
    probe_full_if.arid = '0;
    probe_full_if.arlen = '0;
    probe_full_if.arsize = '0;
    probe_full_if.arburst = '0;
    probe_full_if.rid = '0;
    probe_full_if.rdata = '0;
    probe_full_if.rresp = '0;
    probe_full_if.rvalid = 1'b0;
    probe_full_if.rready = 1'b1;
    clear_request_channels();
    wait (probe_full_if.aresetn === 1'b1);
    @(posedge probe_full_if.aclk);
    for (int unsigned index = 0; index < 64; index++) begin
      if ((index % 2) == 0)
        send_write(index);
      else
        send_read(index);
    end
    $finish;
  end
endmodule

module xdebug_axi_xamba_fixture_top;
  xdebug_axi_xamba_fixture_dut dut();
endmodule
