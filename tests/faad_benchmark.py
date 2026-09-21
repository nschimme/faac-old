#!/usr/bin/env python3
"""Thin wrapper around faac-benchmark's compare_codecs.py --gate.

The real decoder/muxer benchmark harness (encoders x profiles, the four
decoders, Zimtohrli MOS, footprint, peak RSS, robustness, gapless/alignment
offset, and --decoder-report) lives in the sibling faac-benchmark checkout as
compare_codecs.py plus codec_bench/. This script exists so `tests/` and CI
have a single, stdlib-only, repo-local entry point that locates that checkout
and its virtualenv, wires up this repo's own binaries, and forwards the exit
code -- it does not reimplement any measurement itself.

Usage:
    tests/faad_benchmark.py [--benchmark-dir DIR] [extra compare_codecs.py args...]

Runs `compare_codecs.py --gate` (a small fixed clip subset, 3 iterations,
prints a final "GATE: PASS|FAIL" line and exits non-zero on FAIL) against
this repo's build/ tree: faad3 = build/frontend/faad, faac = build/frontend/faac.
Any extra arguments are forwarded verbatim to compare_codecs.py, so a full
(non-gated) run is:

    tests/faad_benchmark.py --coverage 100 --decoder-report build/faad_benchmark/report.md

--benchmark-dir defaults to ../faac-benchmark relative to this repo, or
$FAAC_BENCHMARK_DIR if set.
"""

import argparse
import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BENCHMARK_DIR = os.environ.get(
    "FAAC_BENCHMARK_DIR", os.path.join(os.path.dirname(REPO_ROOT), "faac-benchmark"))


def find_python(benchmark_dir):
    """Prefer the faac-benchmark venv (has numpy/scipy/soundfile/zimtohrli);
    fall back to the running interpreter."""
    venv_python = os.path.join(benchmark_dir, ".venv", "bin", "python3")
    if os.path.exists(venv_python):
        return venv_python
    return sys.executable


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--benchmark-dir", default=DEFAULT_BENCHMARK_DIR,
                        help="Path to the faac-benchmark checkout (default: %(default)s)")
    parser.add_argument("--full", action="store_true",
                        help="Run the full (non-gated) benchmark instead of --gate")
    args, extra = parser.parse_known_args()

    compare_codecs = os.path.join(args.benchmark_dir, "compare_codecs.py")
    if not os.path.exists(compare_codecs):
        print(f"compare_codecs.py not found under --benchmark-dir {args.benchmark_dir}; "
              f"clone/update faac-benchmark next to this repo, or pass --benchmark-dir / "
              f"set $FAAC_BENCHMARK_DIR.", file=sys.stderr)
        sys.exit(1)

    faad3 = os.path.join(REPO_ROOT, "build", "frontend", "faad")
    faac = os.path.join(REPO_ROOT, "build", "frontend", "faac")
    faam = os.path.join(REPO_ROOT, "build", "frontend", "faam")
    if not os.path.exists(faad3):
        print(f"faad3 binary not found at {faad3}; build this repo first.", file=sys.stderr)
        sys.exit(1)

    python_bin = find_python(args.benchmark_dir)

    # Label this tree's decoder by its git revision; the faad3 entry is the
    # first --faad-bin, so any --faad-bin-version the caller adds for extra
    # decoders lines up with theirs and not with ours.
    try:
        rev = subprocess.run(["git", "-C", REPO_ROOT, "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True).stdout.strip()
    except OSError:
        rev = ""
    cmd = [python_bin, compare_codecs, "--faad-bin", faad3,
           "--faad-bin-version", f"git {rev}" if rev else "", "--faac-bin", faac]
    if os.path.exists(faam):
        cmd += ["--faam-bin", faam, "--muxer-bench"]
    if not args.full:
        cmd.append("--gate")
    cmd += extra

    print(f"+ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=args.benchmark_dir)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
