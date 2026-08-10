#!/usr/bin/env python3
"""Run 30 end-to-end plot cases and 30 plot-export cases."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence


PLOT_CASES = [
    ("sin(x)", "-pi", "pi"), ("cos(x)", "-pi", "pi"), ("tan(x)", "-1", "1"),
    ("x", "-2", "2"), ("x^2", "-2", "2"), ("x^3", "-2", "2"),
    ("abs(x)", "-2", "2"), ("sqrt(abs(x))", "-2", "2"), ("exp(x)", "-2", "2"),
    ("exp(-x^2)", "-3", "3"), ("ln(x)", "0.1", "10"), ("log(x)", "0.1", "10"),
    ("sin(x)+cos(x)", "-pi", "pi"), ("sin(2*x)", "-pi", "pi"),
    ("cos(2*x)", "-pi", "pi"), ("x^2-4", "-3", "3"), ("x^3-x", "-2", "2"),
    ("1/x", "0.1", "10"), ("1/(1+x^2)", "-5", "5"), ("x/2+1", "-4", "4"),
    ("3", "-2", "2"), ("x^4", "-2", "2"), ("sqrt(x)", "0", "4"),
    ("cbrt(x)", "-8", "8"), ("sinh(x)", "-2", "2"), ("cosh(x)", "-2", "2"),
    ("tanh(x)", "-3", "3"), ("sigmoid(x)", "-8", "8"), ("relu(x)", "-3", "3"),
    ("sin(x)*exp(-x^2/10)", "-10", "10"),
]

EXPORT_CASES = [
    ("sin(x)", "0", "2*pi"), ("cos(x)", "0", "2*pi"), ("sin(x);cos(x)", "-pi", "pi"),
    ("x", "-2", "2"), ("x^2", "-2", "2"), ("x^2;2*x", "-3", "3"),
    ("x^3", "-2", "2"), ("abs(x)", "-2", "2"), ("sqrt(x)", "0", "4"),
    ("sqrt(abs(x))", "-2", "2"), ("exp(x)", "-2", "2"), ("exp(-x^2)", "-3", "3"),
    ("ln(x)", "0.1", "10"), ("log(x)", "0.1", "10"), ("sin(2*x);cos(2*x)", "-pi", "pi"),
    ("x^2-4", "-3", "3"), ("x^3-x", "-2", "2"), ("1/x", "0.1", "10"),
    ("1/(1+x^2)", "-5", "5"), ("x/2+1", "-4", "4"), ("3", "-2", "2"),
    ("x^4", "-2", "2"), ("cbrt(x)", "-8", "8"), ("sinh(x)", "-2", "2"),
    ("cosh(x)", "-2", "2"), ("tanh(x)", "-3", "3"), ("sigmoid(x)", "-8", "8"),
    ("relu(x)", "-3", "3"), ("sin(x)*exp(-x^2/10);cos(x)*exp(-x^2/10)", "-10", "10"),
    ("x+sin(x)", "-pi", "pi"),
]


def run(command: List[str], cwd: Path, env: Dict[str, str], timeout: int) -> Dict[str, object]:
    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, check=False)
        stdout, stderr, code = completed.stdout, completed.stderr, completed.returncode
        timed_out = False
    except subprocess.TimeoutExpired:
        stdout, stderr, code, timed_out = "", "", 124, True
    return {
        "command": shlex.join(command), "return_code": code,
        "duration_seconds": round(time.monotonic() - started, 4),
        "stdout": stdout, "stderr": stderr, "timed_out": timed_out,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args(argv)
    exe = args.exe.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not exe.exists():
        print(f"missing executable: {exe}", file=sys.stderr)
        return 2
    env = os.environ.copy()
    env.update({"NO_COLOR": "1", "TERMINALC_CONFIG": "", "TERMINALC_HISTORY_FILE": "", "TERMINALC_ASCII_PLOT": "1"})
    results: List[Dict[str, object]] = []
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="terminalc-graphs-", dir=output) as temp_name:
        temp = Path(temp_name)
        for index, (expr, lo, hi) in enumerate(PLOT_CASES, 1):
            script = temp / f"plot-{index:02d}.tc"
            script.write_text(f"plot({expr}, x={lo}..{hi})\n", encoding="utf-8")
            row = run([str(exe), "--no-color", "--script", str(script)], exe.parent.parent, env, args.timeout)
            row.update({"kind": "plot", "case": index, "expression": expr, "range": f"{lo}..{hi}"})
            row["ok"] = row["return_code"] == 0 and "+-- Plot:" in str(row["stdout"])
            results.append(row)
        for index, (expr, lo, hi) in enumerate(EXPORT_CASES, 1):
            suffix = ".svg" if index % 2 else ".csv"
            artifact = temp / f"export-{index:02d}{suffix}"
            script = temp / f"export-{index:02d}.tc"
            export_expr = expr if ";" in expr else f"{expr};{expr}"
            script.write_text(f"plot_save({export_expr}, x={lo}..{hi}, \"{artifact.as_posix()}\")\n", encoding="utf-8")
            row = run([str(exe), "--no-color", "--script", str(script)], exe.parent.parent, env, args.timeout)
            valid_artifact = artifact.exists() and artifact.stat().st_size > 0
            if valid_artifact and suffix == ".svg":
                valid_artifact = "<svg" in artifact.read_text(encoding="utf-8")
            if valid_artifact and suffix == ".csv":
                valid_artifact = artifact.read_text(encoding="utf-8").startswith("x,")
            row.update({"kind": "plot_save", "case": index, "expression": expr, "range": f"{lo}..{hi}", "artifact": str(artifact), "artifact_valid": valid_artifact})
            row["ok"] = row["return_code"] == 0 and valid_artifact
            results.append(row)
    results.sort(key=lambda row: (str(row["kind"]), int(row["case"])))
    failures = [row for row in results if not row["ok"]]
    payload = {"status": "PASS" if not failures else "FAIL", "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(), "duration_seconds": round(time.monotonic() - started, 3), "plot_cases": len(PLOT_CASES), "export_cases": len(EXPORT_CASES), "case_count": len(results), "results": results}
    (output / "graph-matrix.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    with (output / "graph-matrix.log").open("w", encoding="utf-8") as log:
        log.write(f"plots={len(PLOT_CASES)} plot_save={len(EXPORT_CASES)} duration_seconds={payload['duration_seconds']:.3f}\n\n")
        for row in results:
            log.write(f"[{ 'PASS' if row['ok'] else 'FAIL' }] {row['kind']} case {row['case']:02d} {row['expression']} range={row['range']}\n")
            log.write(f"$ {row['command']}\nreturn_code={row['return_code']} duration_seconds={row['duration_seconds']}\n")
            log.write("stdout:\n" + str(row["stdout"]) + "\nstderr:\n" + str(row["stderr"]) + "\n\n")
    print(f"Graph matrix: {payload['status']} ({len(PLOT_CASES)} plot + {len(EXPORT_CASES)} plot_save cases)")
    print(f"Duration: {payload['duration_seconds']:.3f}s")
    print(f"Results: {output / 'graph-matrix.json'}")
    print(f"Commands: {output / 'graph-matrix.log'}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
