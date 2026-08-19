#!/usr/bin/env python3
"""Generate deterministic, semantically dense RTL for trace scaling tests."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


SCHEMA_VERSION = "xdebug.large-rtl-trace.v1"
MIN_LINES = 1024
HIERARCHY_DEPTH = 7
CONTROL_NESTING_DEPTH = 5
SIMULATION_STEPS = 48


def _base_prefix() -> list[str]:
    lines = [
        "`timescale 1ns/1ps",
        "package scale_types_pkg;",
        "  typedef enum logic [2:0] {S_IDLE, S_LOAD, S_EXEC, S_WAIT, S_DONE} state_t;",
        "  typedef struct packed { logic [7:0] tag; logic [23:0] data; } payload_t;",
        "  function automatic logic [31:0] rotate_mix(input logic [31:0] value, input logic [4:0] amount);",
        "    rotate_mix = (value << amount) | (value >> (32 - amount));",
        "  endfunction",
        "endpackage",
        "",
        "interface scale_bus_if #(parameter int WIDTH = 32) (input logic clk, input logic rst_n);",
        "  logic [WIDTH-1:0] data;",
        "  logic [7:0] tag;",
        "  logic valid;",
        "  logic ready;",
        "  modport producer(input clk, rst_n, ready, output data, tag, valid);",
        "  modport consumer(input clk, rst_n, data, tag, valid, output ready);",
        "  modport monitor(input clk, rst_n, data, tag, valid, ready);",
        "endinterface",
        "",
    ]
    for level in range(6):
        lines.extend([
            f"module scale_chain_{level}(input logic [31:0] in_data, input logic [4:0] selector, output logic [31:0] out_data);",
            f"  localparam logic [31:0] LEVEL_MASK = 32'h{(0x10203040 + level * 0x11111111) & 0xffffffff:08x};",
            "  logic [31:0] branch_data;",
            "  always_comb begin",
            "    if (selector[0]) begin",
            "      if (selector[1]) branch_data = in_data ^ LEVEL_MASK;",
            "      else branch_data = in_data + LEVEL_MASK;",
            "    end else begin",
            "      branch_data = {in_data[15:0], in_data[31:16]};",
            "    end",
            "  end",
        ])
        if level == 0:
            lines.append("  assign out_data = branch_data;")
        else:
            lines.append(
                f"  scale_chain_{level - 1} u_next(.in_data(branch_data), .selector(selector), .out_data(out_data));"
            )
        lines.extend(["endmodule", ""])
    lines.extend([
        "module scale_sink(scale_bus_if.consumer bus, output logic [31:0] observed);",
        "  logic [31:0] sampled;",
        "  always_ff @(posedge bus.clk or negedge bus.rst_n) begin",
        "    if (!bus.rst_n) sampled <= '0;",
        "    else if (bus.valid && bus.ready) sampled <= bus.data ^ {24'b0, bus.tag};",
        "  end",
        "  assign bus.ready = !sampled[31] || bus.tag[0];",
        "  assign observed = sampled;",
        "endmodule",
        "",
    ])
    return lines


def _leaf(index: int) -> list[str]:
    salt = (0x9E3779B9 * (index + 1)) & 0xFFFFFFFF
    return [
        f"module scale_leaf_{index:05d} #(",
        f"  parameter logic [31:0] SALT = 32'h{salt:08x}",
        ") (",
        "  input logic clk,",
        "  input logic rst_n,",
        "  input logic [31:0] seed,",
        "  input logic [7:0] opcode,",
        "  input logic [4:0] selector,",
        "  scale_bus_if.producer bus,",
        "  output logic [31:0] leaf_out",
        ");",
        "  import scale_types_pkg::*;",
        "  state_t state, next_state;",
        "  payload_t payload, next_payload;",
        "  logic [31:0] lanes [0:3];",
        "  logic [31:0] selected, chained;",
        "  logic deep_enable;",
        "  genvar lane;",
        "  generate",
        "    for (lane = 0; lane < 4; lane++) begin : g_lane",
        "      logic [31:0] lane_seed;",
        "      assign lane_seed = seed + SALT + lane;",
        "      assign lanes[lane] = rotate_mix(lane_seed, selector + lane);",
        "    end",
        f"    if (({index} % 2) == 0) begin : g_even",
        "      assign deep_enable = ^{opcode, selector, SALT[7:0]};",
        "    end else begin : g_odd",
        "      assign deep_enable = ~^{opcode, selector, SALT[15:8]};",
        "    end",
        "  endgenerate",
        "  always_comb begin",
        "    selected = lanes[selector[1:0]];",
        "    next_payload = payload;",
        "    next_state = state;",
        "    if (opcode[7]) begin",
        "      if (opcode[6]) begin",
        "        if (opcode[5]) begin",
        "          if (opcode[4]) begin",
        "            if (deep_enable) next_payload.data = selected[23:0];",
        "            else next_payload.data = selected[31:8];",
        "          end else next_payload.data = selected[23:0] ^ SALT[23:0];",
        "        end else next_payload.data = selected[23:0] + SALT[23:0];",
        "      end else next_payload.data = selected[23:0] - SALT[23:0];",
        "    end else next_payload.data = {selected[7:0], selected[23:8]};",
        "    casez (opcode[3:0])",
        "      4'b00??: next_payload.tag = opcode ^ SALT[7:0];",
        "      4'b01??: next_payload.tag = opcode + {3'b0, selector};",
        "      4'b1??0: next_payload.tag = opcode - {3'b0, selector};",
        "      default: next_payload.tag = {selector, state};",
        "    endcase",
        "    unique case (state)",
        "      S_IDLE: if (deep_enable) next_state = S_LOAD;",
        "      S_LOAD: next_state = opcode[0] ? S_EXEC : S_WAIT;",
        "      S_EXEC: if (bus.ready) next_state = S_DONE;",
        "      S_WAIT: if (opcode[1]) next_state = S_EXEC;",
        "      S_DONE: next_state = S_IDLE;",
        "      default: next_state = S_IDLE;",
        "    endcase",
        "  end",
        "  always_ff @(posedge clk or negedge rst_n) begin",
        "    if (!rst_n) begin",
        "      state <= S_IDLE;",
        "      payload <= '0;",
        "    end else begin",
        "      state <= next_state;",
        "      payload <= next_payload;",
        "    end",
        "  end",
        "  scale_chain_5 u_chain(.in_data({payload.tag, payload.data}), .selector(selector), .out_data(chained));",
        "  assign bus.data = chained ^ SALT;",
        "  assign bus.tag = payload.tag;",
        "  assign bus.valid = (state == S_EXEC) || (state == S_DONE);",
        "  assign leaf_out = bus.data ^ {31'b0, bus.ready};",
        "endmodule",
        "",
    ]


def _top(tile_count: int, tail_lines: int) -> list[str]:
    lines = [
        "module large_trace_top(",
        "  input logic clk,",
        "  input logic rst_n,",
        "  input logic [31:0] seed,",
        "  input logic [7:0] opcode,",
        "  input logic [4:0] selector,",
        "  output logic [31:0] observed",
        ");",
        f"  logic [31:0] leaf_out [0:{tile_count - 1}];",
        f"  logic [31:0] sink_out [0:{tile_count - 1}];",
    ]
    for index in range(tile_count):
        lines.extend([
            f"  scale_bus_if #(.WIDTH(32)) bus_{index:05d}(.clk(clk), .rst_n(rst_n));",
            f"  scale_leaf_{index:05d} u_leaf_{index:05d}(",
            "    .clk(clk), .rst_n(rst_n), .seed(seed), .opcode(opcode),",
            f"    .selector(selector), .bus(bus_{index:05d}.producer), .leaf_out(leaf_out[{index}])",
            "  );",
            f"  scale_sink u_sink_{index:05d}(.bus(bus_{index:05d}.consumer), .observed(sink_out[{index}]));",
        ])
    lines.extend([
        "  integer reduce_index;",
        "  always_comb begin",
        "    observed = '0;",
        f"    for (reduce_index = 0; reduce_index < {tile_count}; reduce_index++) begin",
        "      observed = observed ^ leaf_out[reduce_index] ^ sink_out[reduce_index];",
        "    end",
        "  end",
    ])
    for index in range(tail_lines):
        value = (0xA5A50000 + index) & 0xFFFFFFFF
        lines.append(
            f"  localparam logic [31:0] SCALE_PAD_{index:04d} = 32'h{value:08x};"
        )
    lines.append("endmodule")
    return lines


def _testbench() -> str:
    return """// Generated benchmark testbench; do not edit.
#include "Vlarge_trace_top.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <cstdint>

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    Vlarge_trace_top top;
    VerilatedFstC trace;
    top.trace(&trace, 99);
    trace.open("waves.fst");
    top.clk = 0;
    top.rst_n = 0;
    top.seed = 0;
    top.opcode = 0;
    top.selector = 0;
    for (uint64_t step = 0; step < 48; ++step) {
        top.clk = !top.clk;
        if (step == 4) top.rst_n = 1;
        if (!top.clk) {
            top.seed = static_cast<uint32_t>(step * 0x10203u + 0x55aa33u);
            top.opcode = static_cast<uint8_t>(step * 13u);
            top.selector = static_cast<uint8_t>(step % 31u);
        }
        top.eval();
        trace.dump(step * 10);
    }
    top.final();
    trace.close();
    return 0;
}
"""


def render(target_lines: int) -> tuple[str, dict[str, object]]:
    if target_lines < MIN_LINES:
        raise ValueError(f"target_lines must be at least {MIN_LINES}")
    prefix = _base_prefix()
    leaves: list[list[str]] = []
    tile_count = 0
    while True:
        candidate_leaf = _leaf(tile_count)
        candidate_count = tile_count + 1
        candidate_size = len(prefix) + sum(map(len, leaves)) + len(candidate_leaf) + len(_top(candidate_count, 0))
        if candidate_size > target_lines:
            break
        leaves.append(candidate_leaf)
        tile_count = candidate_count
    if tile_count == 0:
        raise ValueError("target is too small for one semantic tile")
    core = prefix + [line for leaf in leaves for line in leaf]
    tail_lines = target_lines - len(core) - len(_top(tile_count, 0))
    if tail_lines < 0:
        raise AssertionError("negative semantic tail")
    lines = core + _top(tile_count, tail_lines)
    if len(lines) != target_lines:
        raise AssertionError(f"line-count mismatch: {len(lines)} != {target_lines}")
    text = "\n".join(lines) + "\n"
    metadata: dict[str, object] = {
        "schema_version": SCHEMA_VERSION,
        "target_rtl_lines": target_lines,
        "rtl_line_count": len(lines),
        "rtl_sha256": hashlib.sha256(text.encode()).hexdigest(),
        "tile_module_count": tile_count,
        "module_definition_count": tile_count + 8,
        "interface_definition_count": 1,
        "interface_instance_count": tile_count,
        "leaf_instance_count": tile_count,
        "sink_instance_count": tile_count,
        "elaborated_chain_instance_count": tile_count * 6,
        "expected_minimum_instance_count": 1 + tile_count * 8,
        "max_hierarchy_depth": HIERARCHY_DEPTH,
        "control_nesting_depth": CONTROL_NESTING_DEPTH,
        "simulation_steps": SIMULATION_STEPS,
        "features": [
            "package", "parameter", "enum", "packed_struct", "interface",
            "modport", "generate_for", "generate_if", "always_comb",
            "always_ff", "nested_if", "case", "casez", "packed_array",
            "unpacked_array", "continuous_assign", "cross_module_chain",
        ],
        "query_anchors": {
            "resolve": "top.u_leaf_00000.payload",
            "value": "TOP.u_leaf_00000.leaf_out",
            "driver": "top.u_leaf_00000.leaf_out",
            "active_driver": "top.u_leaf_00000.bus.data",
            "active_chain": "top.u_sink_00000.observed",
            "changes": "TOP.u_leaf_00000.state",
            "query_time": "350ps",
            "changes_begin": "0ps",
            "changes_end": "470ps",
        },
    }
    return text, metadata


def generate(target_lines: int, output_dir: Path) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)
    rtl, metadata = render(target_lines)
    (output_dir / "large_trace.sv").write_text(rtl, encoding="utf-8")
    (output_dir / "tb_large_trace.cpp").write_text(_testbench(), encoding="utf-8")
    (output_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return metadata


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lines", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    metadata = generate(args.lines, args.output_dir.resolve())
    print(json.dumps(metadata, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
