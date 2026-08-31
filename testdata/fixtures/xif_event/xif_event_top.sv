`timescale 1ns/1ps

// 原版 xdebug.xif_event 的公开 pin-level 语义镜像。
// 当前测试只打开同目录原始 FST；generate_xif_event_fst.cpp 按此确定性
// stimulus 写出四态波形，不依赖 UVM、XIF agent、VCS、NPI 或 FSDB。
typedef struct packed {
  logic [7:0]  opcode;
  logic [3:0]  channel;
  logic [3:0]  id;
  logic [15:0] data;
} xif_event_pd_t;

interface xif_event_if;
  logic vld;
  logic rdy;
  logic bp;
  xif_event_pd_t pd;
endinterface

module xif_event_top;
  logic clk;
  logic rst_n;
  logic xz_vld;
  logic [15:0] xz_data;

  xif_event_if if_rdy();
  xif_event_if if_bp();
  xif_event_if if_none();
  xif_event_if if_pair_master();
  xif_event_if if_pair_slave();

  initial begin
    clk = 1'b0;
    forever #5ns clk = ~clk;
  end

  initial begin
    rst_n = 1'b0;
    xz_vld = 1'b0;
    xz_data = 16'h0000;
    #45ns rst_n = 1'b1;
    #20ns xz_vld = 1'b1;
          xz_data = 16'hxxxx;
    #10ns xz_vld = 1'b0;
          xz_data = 16'h0000;
  end

  initial begin
    if_rdy.vld = 1'b0; if_rdy.rdy = 1'b1; if_rdy.bp = 1'b0; if_rdy.pd = '0;
    #84ns if_rdy.vld = 1'b1; if_rdy.pd = '{8'h5a, 4'h3, 4'h2, 16'ha55a};
    #20ns if_rdy.pd = '{8'h10, 4'h1, 4'h1, 16'h1000};
    #2ns  if_rdy.vld = 1'b0;
    #18ns if_rdy.vld = 1'b1; if_rdy.pd = '{8'h11, 4'h2, 4'h0, 16'h1001};
    #10ns if_rdy.pd = '{8'h12, 4'h3, 4'h2, 16'h1002};
    #2ns  if_rdy.vld = 1'b0;
  end

  initial begin
    if_bp.vld = 1'b0; if_bp.rdy = 1'b0; if_bp.bp = 1'b1; if_bp.pd = '0;
    #64ns if_bp.vld = 1'b1; if_bp.pd = '{8'hb0, 4'h0, 4'h0, 16'h2000};
    #10ns if_bp.pd = '{8'hb1, 4'h1, 4'h1, 16'h2001};
    #2ns  if_bp.vld = 1'b0;
    #8ns  if_bp.bp = 1'b0;
    #10ns if_bp.vld = 1'b1; if_bp.pd = '{8'hb2, 4'h2, 4'h2, 16'h2002};
    #10ns if_bp.pd = '{8'hb3, 4'h3, 4'h3, 16'h2003};
    #2ns  if_bp.vld = 1'b0;
  end

  initial begin
    if_none.vld = 1'b0; if_none.rdy = 1'b0; if_none.bp = 1'b0; if_none.pd = '0;
    #64ns if_none.vld = 1'b1; if_none.pd = '{8'hc0, 4'h0, 4'h1, 16'h3000};
    #2ns  if_none.vld = 1'b0;
    #38ns if_none.vld = 1'b1; if_none.pd = '{8'hc1, 4'h1, 4'h2, 16'h3001};
    #10ns if_none.pd = '{8'hc2, 4'h2, 4'h3, 16'h3002};
    #2ns  if_none.vld = 1'b0;
  end

  initial begin
    if_pair_master.vld = 1'b0; if_pair_master.rdy = 1'b0;
    if_pair_master.bp = 1'b0; if_pair_master.pd = '0;
    if_pair_slave.vld = 1'b0; if_pair_slave.rdy = 1'b0;
    if_pair_slave.bp = 1'b0; if_pair_slave.pd = '0;
    #64ns;
    if_pair_master.vld = 1'b1; if_pair_slave.vld = 1'b1;
    if_pair_master.rdy = 1'b1; if_pair_slave.rdy = 1'b1;
    if_pair_master.pd = '{8'hd0, 4'h0, 4'h0, 16'h4000};
    if_pair_slave.pd = if_pair_master.pd;
    #10ns if_pair_master.pd = '{8'hd1, 4'h1, 4'h1, 16'h4001};
          if_pair_slave.pd = if_pair_master.pd;
    #2ns  if_pair_master.rdy = 1'b0; if_pair_slave.rdy = 1'b0;
    #8ns  if_pair_master.pd = '{8'hd2, 4'h2, 4'h2, 16'h4002};
          if_pair_slave.pd = if_pair_master.pd;
    #8ns  if_pair_master.rdy = 1'b1; if_pair_slave.rdy = 1'b1;
    #10ns if_pair_master.pd = '{8'hd3, 4'h3, 4'h3, 16'h4003};
          if_pair_slave.pd = if_pair_master.pd;
    #2ns  if_pair_master.vld = 1'b0; if_pair_slave.vld = 1'b0;
  end

  initial begin
    #195ns $finish;
  end
endmodule
