#!/usr/bin/env python3
"""Run TerminalC's gated validation plan and save an auditable report.

The runner uses only Python's standard library and works on macOS, Linux, and
Windows. It intentionally does not publish, tag, or commit any artifacts.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import platform
import re
import shlex
import subprocess
import sys
import time
from decimal import Decimal, InvalidOperation, getcontext
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

getcontext().prec = 160


ACCURACY_CASES = [
    {
        "name": "precision_4_near_one_truncation_guard",
        "precision": 4,
        "expr": "sind(89.999999)",
        "first_line": "0.9999",
        "contains": "[4 digits,",
    },
    {
        "name": "precision_10_near_one_truncation_guard",
        "precision": 10,
        "expr": "sind(89.999999)",
        "first_line": "0.9999999999",
        "contains": "[10 digits,",
    },
    {
        "name": "precision_20_near_one_accuracy",
        "precision": 20,
        "expr": "sind(89.999999)",
        "expected": "0.99999999999999984769",
        "tolerance": "1e-20",
    },
    {
        "name": "sqrt_two_precision_30",
        "precision": 30,
        "expr": "sqrt(2)",
        "expected": "1.414213562373095048801688724210",
        "tolerance": "1e-30",
    },
    {
        "name": "exp_one_precision_30",
        "precision": 30,
        "expr": "exp(1)",
        "expected": "2.718281828459045235360287471353",
        "tolerance": "1e-30",
    },
    {
        "name": "ln_two_precision_30",
        "precision": 30,
        "expr": "ln(2)",
        "expected": "0.693147180559945309417232121458",
        "tolerance": "1e-30",
    },
    {
        "name": "pi_precision_30",
        "precision": 30,
        "expr": "pi",
        "expected": "3.141592653589793238462643383280",
        "tolerance": "1e-30",
    },
    {
        "name": "ramanujan_pi_precision_50",
        "precision": 50,
        "expr": "ramanujan_pi()",
        "expected": "3.14159265358979323846264338327950288419716939937511",
        "tolerance": "1e-50",
    },
    {
        "name": "exact_trigonometric_value",
        "precision": 30,
        "expr": "sin(pi/6)",
        "expected": "0.5",
        "tolerance": "0",
    },
]


COMPLEX_CASES = [
    {
        "name": "fft_ifft_roundtrip_power_of_two",
        "precision": 40,
        "expr": "real(ifft(fft([1,2,3,4])))",
        "expect": r"\[\s*1\s+2\s+3\s+4\s*\]",
    },
    {
        "name": "fft_imaginary_component",
        "precision": 40,
        "expr": "imag(fft([1,2,3,4]))",
        "expect": r"\[\s*0\s+2\s+0\s+-2\s*\]",
    },
    {
        "name": "fft_direct_dft_non_power_of_two",
        "precision": 40,
        "expr": "real(fft([1,2,3]))",
        "expect": r"\[\s*6\s+-1\.5\s+-1\.5\s*\]",
    },
    {
        "name": "matrix_inverse_determinant",
        "precision": 40,
        "expr": "det(inv([1,2;3,5]))",
        "expect": r"(^|\n)-1(\n|$)",
    },
    {
        "name": "numerical_integral",
        "precision": 30,
        "expr": "integral(sin(x),0,pi)",
        "expect": r"(^|\n)2(\n|$)",
    },
    {
        "name": "numeric_root_solver",
        "precision": 30,
        "expr": "solve(x^2-2,1)",
        "expect": r"1\.414213562373095048801688724210",
    },
    {
        "name": "unit_conversion_chain",
        "precision": 30,
        "expr": "to(5*km,m)",
        "expect": r"(^|\n)5000 m(\n|$)",
    },
]


STRESS_CASES = [
    {
        "name": "million_term_sum",
        "precision": 80,
        "expr": "sum(x^2,1,1000000)",
        "expect": r"333333833333500000",
        "timeout": 60,
    },
    {
        "name": "large_factorial_exact_ratio",
        "precision": 20,
        "expr": "fact(100000)/fact(100000)",
        "expect": r"(^|\n)1(\n|$)",
        "timeout": 60,
    },
    {
        "name": "high_precision_ramanujan_pi",
        "precision": 200,
        "expr": "ramanujan_pi()",
        "expect": r"3\.14159265358979323846264338327950288419716939937510",
        "timeout": 60,
    },
]


def _safe_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode(errors="replace")
    return str(value)


def _numeric_output(stdout: str) -> Optional[Decimal]:
    candidates: List[str] = []
    for line in stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("Decimal:"):
            candidates.append(stripped.split(":", 1)[1].strip())
    candidates.extend(line.strip() for line in stdout.splitlines())
    numeric = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$")
    for candidate in candidates:
        if not numeric.fullmatch(candidate):
            continue
        try:
            return Decimal(candidate)
        except InvalidOperation:
            continue
    return None


class ValidationRunner:
    def __init__(self, root: Path, output: Path, parallel: int) -> None:
        self.root = root
        self.output = output
        self.parallel = max(1, parallel)
        self._build_dir = root / "build"
        self.records: List[Dict[str, Any]] = []
        self.env = os.environ.copy()
        self.env.update(
            {
                "NO_COLOR": "1",
                "TERMINALC_CONFIG": "",
                "TERMINALC_HISTORY_FILE": "",
            }
        )

    def _log_path(self, category: str) -> Path:
        return self.output / (re.sub(r"[^A-Za-z0-9_.-]+", "_", category) + ".log")

    def command(
        self,
        category: str,
        command: Sequence[Path | str],
        timeout: int,
        env: Optional[Dict[str, str]] = None,
    ) -> Dict[str, Any]:
        argv = [str(item) for item in command]
        started = time.monotonic()
        timed_out = False
        try:
            completed = subprocess.run(
                argv,
                cwd=self.root,
                env=env or self.env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=timeout,
                check=False,
            )
            stdout = _safe_text(completed.stdout)
            stderr = _safe_text(completed.stderr)
            return_code = completed.returncode
        except subprocess.TimeoutExpired as exc:
            timed_out = True
            stdout = _safe_text(exc.stdout)
            stderr = _safe_text(exc.stderr)
            return_code = 124
        except OSError as exc:
            stdout = ""
            stderr = str(exc)
            return_code = 127
        duration = time.monotonic() - started
        record: Dict[str, Any] = {
            "category": category,
            "command": argv,
            "duration_seconds": round(duration, 3),
            "return_code": return_code,
            "timed_out": timed_out,
            "ok": return_code == 0 and not timed_out,
        }
        log_path = self._log_path(category)
        with log_path.open("a", encoding="utf-8") as log:
            log.write("\n=== " + dt.datetime.now().isoformat() + " ===\n")
            log.write("$ " + shlex.join(argv) + "\n")
            log.write("return_code: " + str(return_code) + "\n")
            log.write("duration_seconds: " + f"{duration:.3f}" + "\n")
            if timed_out:
                log.write("status: TIMEOUT\n")
            log.write("--- stdout ---\n" + stdout)
            if not stdout.endswith("\n"):
                log.write("\n")
            log.write("--- stderr ---\n" + stderr)
            if not stderr.endswith("\n"):
                log.write("\n")
        record["log"] = str(log_path.relative_to(self.output))
        record["stdout"] = stdout
        record["stderr"] = stderr
        self.records.append(record)
        return record

    def check(self, record: Dict[str, Any], ok: bool, detail: str) -> None:
        record["ok"] = bool(record["ok"] and ok)
        record["detail"] = detail

    def mark(self, category: str, name: str, ok: bool, detail: str) -> None:
        self.records.append(
            {
                "category": category,
                "name": name,
                "ok": ok,
                "detail": detail,
                "duration_seconds": 0.0,
            }
        )

    def cli(self, category: str, case: Dict[str, Any]) -> Dict[str, Any]:
        exe = self.exe
        command: List[str | Path] = [exe, "--no-color"]
        if case.get("precision") is not None:
            command += ["--precision", str(case["precision"])]
        if case.get("script"):
            command += ["--script", str(case["script"])]
        else:
            command += ["--eval", str(case["expr"])]
        record = self.command(category, command, int(case.get("timeout", 30)))
        record["name"] = case["name"]
        return record

    @property
    def exe(self) -> Path:
        name = "terminalc.exe" if os.name == "nt" else "terminalc"
        return self.build_dir / name

    @property
    def build_dir(self) -> Path:
        return self._build_dir

    def run_accuracy(self, build_dir: Path) -> None:
        self._build_dir = build_dir
        for case in ACCURACY_CASES:
            record = self.cli("accuracy", case)
            stdout = record.get("stdout", "")
            if not record["ok"]:
                self.check(record, False, "calculator command failed")
                continue
            checks: List[str] = []
            ok = True
            if case.get("first_line") is not None:
                first = next((line.strip() for line in stdout.splitlines() if line.strip()), "")
                expected = str(case["first_line"])
                ok = ok and first == expected
                checks.append(f"first_line={first!r}")
            if case.get("contains"):
                expected = str(case["contains"])
                ok = ok and expected in stdout
                checks.append(f"contains={expected!r}")
            if case.get("expected") is not None:
                actual = _numeric_output(stdout)
                expected_decimal = Decimal(str(case["expected"]))
                tolerance = Decimal(str(case["tolerance"]))
                numeric_ok = actual is not None and abs(actual - expected_decimal) <= tolerance
                ok = ok and numeric_ok
                checks.append(f"actual={actual}, expected={expected_decimal}, tolerance={tolerance}")
            self.check(record, ok, "; ".join(checks))

    def run_pattern_cases(self, category: str, build_dir: Path, cases: Sequence[Dict[str, Any]]) -> None:
        self._build_dir = build_dir
        for case in cases:
            record = self.cli(category, case)
            if not record["ok"]:
                self.check(record, False, "calculator command failed")
                continue
            matched = re.search(str(case["expect"]), record.get("stdout", ""), re.MULTILINE) is not None
            self.check(record, matched, f"pattern={case['expect']!r}")

    def write_chain_script(self) -> Path:
        script = self.output / "complex-chain.tc"
        script.write_text(
            "precision 40\n"
            "samples = real(ifft(fft([1,2,3,4])))\n"
            "determinant = det(inv([1,2;3,5]))\n"
            "area = integral(sin(x),0,pi)\n"
            "distance = to(5*km,m)\n"
            "samples\n"
            "determinant\n"
            "area\n"
            "distance\n",
            encoding="utf-8",
        )
        return script

    def run_exhaustive(self, build_dir: Path, sanitizer: bool = False) -> None:
        label = "sanitizer-ctest" if sanitizer else "exhaustive-ctest"
        env = dict(self.env)
        if sanitizer:
            asan_options = ["halt_on_error=1", "abort_on_error=1"]
            if sys.platform != "darwin":
                asan_options.insert(0, "detect_leaks=1")
            env["ASAN_OPTIONS"] = ":".join(asan_options)
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        command = [
            "ctest",
            "--test-dir",
            build_dir,
            "--output-on-failure",
            "--parallel",
            str(self.parallel),
        ]
        record = self.command(label, command, 300, env)
        if not record["ok"]:
            record["detail"] = "CTest failed; inspect the saved log"
            return
        match = re.search(r"100% tests passed out of (\d+)", record.get("stdout", ""))
        if not match:
            self.check(record, False, "CTest did not report a complete passing suite")
        else:
            self.check(record, True, f"{match.group(1)} tests passed")


def _git_commit(root: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def write_summary(runner: ValidationRunner, root: Path, started: float, output: Path) -> int:
    failures = [record for record in runner.records if not record.get("ok", False) and not record.get("skipped")]
    skipped = [record for record in runner.records if record.get("skipped")]
    status = "PASS" if not failures and not skipped else "PARTIAL" if not failures else "FAIL"
    payload = {
        "status": status,
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "duration_seconds": round(time.monotonic() - started, 3),
        "commit": _git_commit(root),
        "platform": platform.platform(),
        "python": sys.version,
        "records": runner.records,
    }
    (output / "summary.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# TerminalC validation report",
        "",
        f"- Status: **{status}**",
        f"- Commit: `{payload['commit']}`",
        f"- Generated (UTC): `{payload['generated_utc']}`",
        f"- Total runner time: `{payload['duration_seconds']:.3f}s`",
        "",
        "| Category | Name | Status | Duration | Detail |",
        "|---|---|---:|---:|---|",
    ]
    for record in runner.records:
        record_status = "SKIPPED" if record.get("skipped") else "PASS" if record.get("ok") else "FAIL"
        lines.append(
            f"| {record.get('category', '')} | {record.get('name', record.get('category', ''))} | "
            f"{record_status} | {record.get('duration_seconds', 0):.3f}s | "
            f"{record.get('detail', '')} |"
        )
    lines += [
        "",
        "Detailed command output is stored in the adjacent `.log` files.",
        "This report is intentionally local and is not published by the runner.",
    ]
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Validation status: {status}")
    print(f"Report: {output / 'summary.md'}")
    print(f"JSON:   {output / 'summary.json'}")
    if failures:
        print(f"Failures: {len(failures)}", file=sys.stderr)
        return 1
    if skipped:
        print(f"Skipped gates: {len(skipped)}", file=sys.stderr)
        return 2
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build", help="existing normal CMake build directory")
    parser.add_argument("--sanitizer-dir", default="build-sanitize", help="existing ASan/UBSan build directory")
    parser.add_argument("--output-dir", help="report directory; default is validation-results/<UTC timestamp>")
    parser.add_argument("--parallel", type=int, default=4, help="CTest worker count")
    parser.add_argument("--build", action="store_true", help="build normal and sanitizer trees before testing")
    parser.add_argument("--skip-sanitizers", action="store_true", help="mark sanitizer gate skipped instead of failing")
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parents[1]
    build_dir = (root / args.build_dir).resolve()
    sanitizer_dir = (root / args.sanitizer_dir).resolve()
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = Path(args.output_dir).resolve() if args.output_dir else root / "validation-results" / timestamp
    output.mkdir(parents=True, exist_ok=True)
    runner = ValidationRunner(root, output, args.parallel)
    started = time.monotonic()

    if args.build:
        runner.command("build", ["cmake", "--build", build_dir, "--parallel", str(args.parallel)], 300)
        if not args.skip_sanitizers:
            runner.command("sanitizer-build", ["cmake", "--build", sanitizer_dir, "--parallel", str(args.parallel)], 300)

    if not runner.exe.exists():
        runner.mark("preflight", "normal-build", False, f"missing executable: {runner.exe}")
    else:
        runner.run_exhaustive(build_dir)
        runner.run_accuracy(build_dir)
        runner.run_pattern_cases("complex", build_dir, COMPLEX_CASES)
        chain = runner.write_chain_script()
        chain_record = runner.cli("complex", {"name": "complex_chained_script", "script": chain, "timeout": 30})
        runner.check(
            chain_record,
            chain_record["ok"] and all(token in chain_record.get("stdout", "") for token in ("[ 1 2 3 4 ]", "-1", "2", "5000")),
            "chained FFT/matrix/integral/unit script output",
        )
        runner.run_pattern_cases("stress", build_dir, STRESS_CASES)

    sanitizer_exe = sanitizer_dir / ("terminalc.exe" if os.name == "nt" else "terminalc")
    if args.skip_sanitizers:
        runner.records.append(
            {
                "category": "memory-corruption",
                "name": "asan-ubsan-ctest",
                "ok": False,
                "skipped": True,
                "detail": "explicitly skipped with --skip-sanitizers",
                "duration_seconds": 0.0,
            }
        )
    elif not sanitizer_exe.exists():
        runner.mark("memory-corruption", "asan-ubsan-ctest", False, f"missing executable: {sanitizer_exe}")
    else:
        runner.run_exhaustive(sanitizer_dir, sanitizer=True)

    return write_summary(runner, root, started, output)


if __name__ == "__main__":
    raise SystemExit(main())
