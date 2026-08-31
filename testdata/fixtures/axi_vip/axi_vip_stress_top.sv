`timescale 1ns/1ps

interface axi_master_mirror_if;
  logic [63:0] awaddr; logic [7:0] awid; logic [9:0] awlen;
  logic [2:0] awsize; logic [1:0] awburst; logic awvalid; logic awready;
  logic [1023:0] wdata; logic [127:0] wstrb; logic wlast; logic wvalid; logic wready;
  logic [7:0] bid; logic [3:0] bresp; logic bvalid; logic bready;
  logic [63:0] araddr; logic [7:0] arid; logic [9:0] arlen;
  logic [2:0] arsize; logic [1:0] arburst; logic arvalid; logic arready;
  logic [7:0] rid; logic [1023:0] rdata; logic [3:0] rresp;
  logic rlast; logic rvalid; logic rready;
endinterface

interface axi_vip_mirror_if;
  axi_master_mirror_if master_if[1]();
endinterface

module axi_vip_fixture_top;
  bit clk;
  bit rst_n;
  axi_vip_mirror_if axi_vip_if();
endmodule
