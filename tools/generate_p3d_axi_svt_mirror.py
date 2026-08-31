#!/usr/bin/env python3
"""Freeze and render repository-local pin-level SVT AXI semantic mirrors."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
RUNS = {
    "fixed_delay": ("axi_fixed_delay", "23be697da2934298b03586440aa94b773d497625814fd953236377a5ff97d16c"),
    "random_seed_7": ("axi_random_seed_7", "afc6001c8a4863626f8059973e7e0e37a4e4f2076f04e1115c629109cbe3c7a2"),
    "random_seed_19": ("axi_random_seed_19", "52ac70c3eee9c685f5bc34903102d92466906f60aa76484ba04a1cb5bbffdae3"),
    "random_seed_73": ("axi_random_seed_73", "02777d03cb05ab3641cb8ddac08184ea3df4b7deb649011787e8260f5a9b4655"),
}
FIELDS = (
    "channel", "time_ps", "valid_begin_time_ps", "id", "addr", "len", "last",
    "resp", "data", "wstrb",
)


def inside_repo(path: Path) -> Path:
    resolved = path.resolve()
    if resolved != REPO and REPO not in resolved.parents:
        raise SystemExit(f"refusing output outside repository: {resolved}")
    return resolved


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def freeze(args: argparse.Namespace) -> None:
    source_root = Path(args.source_root).resolve()
    output_root = inside_repo(Path(args.output_root))
    output_root.mkdir(parents=True, exist_ok=True)
    oracle = json.loads((REPO / "tests/data/rtl_wave_differential/"
                         "p3d-axi-vip.public-oracle.json").read_text(encoding="utf-8"))
    oracle_runs = {run["name"]: run for run in oracle["runs"]}
    for name, (source_name, expected_sha) in RUNS.items():
        source = source_root / source_name / "axi_handshake.jsonl"
        if sha256(source) != expected_sha:
            raise SystemExit(f"locked handshake identity mismatch: {source}")
        rows = [json.loads(line) for line in source.read_text(encoding="utf-8").splitlines()]
        detailed = next(row for row in oracle_runs[name]["observations"]
                        if row["observation_id"] == "query.first.detailed")
        first_beats = detailed["response"]["data"]["transaction"]["data"]["beats"]
        w_rows = [row for row in rows if row["channel"] == "W"]
        for event, beat in zip(w_rows, first_beats, strict=False):
            event["data"] = beat["data"].split("'h", 1)[1]
            event["wstrb"] = beat["wstrb"].split("'h", 1)[1]
        output = output_root / f"{name}.events.tsv"
        with output.open("w", encoding="utf-8", newline="") as stream:
            stream.write("\t".join(FIELDS) + "\n")
            for row in rows:
                values = [str(row.get(field, "")) for field in FIELDS]
                stream.write("\t".join(values).rstrip("\t") + "\n")


def load_events(path: Path) -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    with path.open(encoding="utf-8", newline="") as stream:
        for raw in csv.DictReader(stream, delimiter="\t"):
            row: dict[str, int | str] = {"channel": raw["channel"]}
            for field in FIELDS[1:8]:
                row[field] = int(raw[field]) if raw[field] else 0
            row["data"] = raw["data"]
            row["wstrb"] = raw["wstrb"]
            rows.append(row)
    return rows


def emit_channel(channel: str, rows: list[dict[str, int | str]]) -> str:
    signal = channel.lower()
    lines = [f"  initial begin : replay_{signal}", "    #0;"]
    cursor = 0
    for row in rows:
        handshake = int(row["time_ps"]) // 1000
        begin = int(row["valid_begin_time_ps"] or row["time_ps"]) // 1000
        start = begin - 1
        if start < cursor:
            raise SystemExit(f"overlapping {channel} event at {handshake}ns")
        lines.append(f"    #{start - cursor}ns;")
        if channel in ("AW", "AR"):
            lines.extend([
                f"    axi_vip_if.master_if[0].{signal}id = 8'h{int(row['id']):02x};",
                f"    axi_vip_if.master_if[0].{signal}addr = 64'h{int(row['addr']):016x};",
                f"    axi_vip_if.master_if[0].{signal}len = 10'h{int(row['len']):03x};",
                f"    axi_vip_if.master_if[0].{signal}valid = 1'b1;",
            ])
            lines.append(f"    #{handshake - begin}ns;")
            lines.append(f"    axi_vip_if.master_if[0].{signal}ready = 1'b1;")
        elif channel == "W":
            lines.extend([
                (f"    axi_vip_if.master_if[0].wdata = 1024'h{row['data']};"
                 if row["data"] else "    axi_vip_if.master_if[0].wdata = '0;"),
                (f"    axi_vip_if.master_if[0].wstrb = 128'h{row['wstrb']};"
                 if row["wstrb"] else
                 "    axi_vip_if.master_if[0].wstrb = 128'h000000000000000000000000000000ff;"),
                f"    axi_vip_if.master_if[0].wlast = 1'b{int(row['last'])};",
                "    axi_vip_if.master_if[0].wvalid = 1'b1;",
                f"    #{handshake - begin}ns;",
                "    axi_vip_if.master_if[0].wready = 1'b1;",
            ])
        elif channel == "B":
            lines.extend([
                f"    axi_vip_if.master_if[0].bid = 8'h{int(row['id']):02x};",
                f"    axi_vip_if.master_if[0].bresp = 4'h{int(row['resp']):x};",
                "    axi_vip_if.master_if[0].bvalid = 1'b1;",
            ])
        elif channel == "R":
            lines.extend([
                f"    axi_vip_if.master_if[0].rid = 8'h{int(row['id']):02x};",
                f"    axi_vip_if.master_if[0].rresp = 4'h{int(row['resp']):x};",
                f"    axi_vip_if.master_if[0].rlast = 1'b{int(row['last'])};",
                "    axi_vip_if.master_if[0].rdata = '0;",
                "    axi_vip_if.master_if[0].rvalid = 1'b1;",
            ])
        lines.append("    #2ns;")
        lines.append(f"    axi_vip_if.master_if[0].{signal}valid = 1'b0;")
        if channel in ("AW", "AR", "W"):
            lines.append(f"    axi_vip_if.master_if[0].{signal}ready = 1'b0;")
        if channel in ("W", "R"):
            lines.append(f"    axi_vip_if.master_if[0].{signal}last = 1'b0;")
        cursor = handshake + 1
    lines.extend(["  end", ""])
    return "\n".join(lines)


def render(args: argparse.Namespace) -> None:
    events_path = Path(args.events).resolve()
    output = inside_repo(Path(args.output))
    rows = load_events(events_path)
    end_ns = max(int(row["time_ps"]) for row in rows) // 1000 + 10_000
    by_channel = {channel: [row for row in rows if row["channel"] == channel]
                  for channel in ("AW", "W", "B", "AR", "R")}
    body = f"""`timescale 1ns/1ps

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

  initial begin clk = 1'b0; forever #5ns clk = ~clk; end
  initial begin rst_n = 1'b0; #195ns rst_n = 1'b1; end
  initial begin
    axi_vip_if.master_if[0].awaddr = '0; axi_vip_if.master_if[0].awid = '0;
    axi_vip_if.master_if[0].awlen = '0; axi_vip_if.master_if[0].awsize = 3'h3;
    axi_vip_if.master_if[0].awburst = 2'h1; axi_vip_if.master_if[0].awvalid = 0;
    axi_vip_if.master_if[0].awready = 0; axi_vip_if.master_if[0].wdata = '0;
    axi_vip_if.master_if[0].wstrb = '0; axi_vip_if.master_if[0].wlast = 0;
    axi_vip_if.master_if[0].wvalid = 0; axi_vip_if.master_if[0].wready = 0;
    axi_vip_if.master_if[0].bid = '0; axi_vip_if.master_if[0].bresp = '0;
    axi_vip_if.master_if[0].bvalid = 0; axi_vip_if.master_if[0].bready = 0;
    axi_vip_if.master_if[0].araddr = '0; axi_vip_if.master_if[0].arid = '0;
    axi_vip_if.master_if[0].arlen = '0; axi_vip_if.master_if[0].arsize = 3'h3;
    axi_vip_if.master_if[0].arburst = 2'h1; axi_vip_if.master_if[0].arvalid = 0;
    axi_vip_if.master_if[0].arready = 0; axi_vip_if.master_if[0].rid = '0;
    axi_vip_if.master_if[0].rdata = '0; axi_vip_if.master_if[0].rresp = '0;
    axi_vip_if.master_if[0].rlast = 0; axi_vip_if.master_if[0].rvalid = 0;
    axi_vip_if.master_if[0].rready = 0;
  end
  initial begin #14ns; axi_vip_if.master_if[0].bready = 1;
                        axi_vip_if.master_if[0].rready = 1; end

{''.join(emit_channel(channel, by_channel[channel]) for channel in by_channel)}
  initial begin #{end_ns}ns; $finish; end
endmodule
"""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(body, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    freeze_parser = sub.add_parser("freeze")
    freeze_parser.add_argument("--source-root", required=True)
    freeze_parser.add_argument("--output-root", required=True)
    freeze_parser.set_defaults(func=freeze)
    render_parser = sub.add_parser("render")
    render_parser.add_argument("--events", required=True)
    render_parser.add_argument("--output", required=True)
    render_parser.set_defaults(func=render)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
