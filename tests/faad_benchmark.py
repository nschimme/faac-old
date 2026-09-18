#!/usr/bin/env python3
import os
import sys
import subprocess
import time
import hashlib
import json

TEST_FILES = [
    "/opt/faac-benchmark/output/48k_stereo_192k_21-classic.441.16b48k.wav_candidate.m4a",
    "/opt/faac-benchmark/output/48k_stereo_24k_21-classic.441.16b48k.wav_candidate.m4a",
    "/opt/faac-benchmark/output/tp_music_tonal_lc_candidate.m4a",
    "/opt/faac-benchmark/output/tp_music_tonal_he_candidate.m4a",
]

FAAD_BIN = os.path.abspath("build/frontend/faad")
LIBFAAD_SO = os.path.abspath("build/libfaad/libfaad.so")

def get_binary_sizes():
    if not os.path.exists(LIBFAAD_SO):
        return {}
    res = subprocess.run(["size", LIBFAAD_SO], capture_output=True, text=True)
    lines = res.stdout.strip().split("\n")
    if len(lines) >= 2:
        parts = lines[1].split()
        return {
            "text": int(parts[0]),
            "data": int(parts[1]),
            "bss": int(parts[2]),
            "total": int(parts[3])
        }
    return {}

def benchmark_file(filepath, iterations=10):
    if not os.path.exists(filepath):
        return None

    # First run to get pcm hash and output length
    pcm_out = "/tmp/faad_bench_out.wav"
    if os.path.exists(pcm_out):
        os.remove(pcm_out)

    cmd = [FAAD_BIN, "-o", pcm_out, filepath]
    res = subprocess.run(cmd, capture_output=True)
    if res.returncode != 0:
        print(f"Error decoding {filepath}: {res.stderr.decode()}")
        return None

    with open(pcm_out, "rb") as f:
        pcm_data = f.read()
    pcm_hash = hashlib.sha256(pcm_data).hexdigest()
    pcm_size = len(pcm_data)

    # Benchmark run to stdout / null
    start_time = time.perf_counter()
    for _ in range(iterations):
        res = subprocess.run([FAAD_BIN, "-w", filepath], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end_time = time.perf_counter()

    avg_time_ms = ((end_time - start_time) / iterations) * 1000.0
    return {
        "file": os.path.basename(filepath),
        "pcm_hash": pcm_hash,
        "pcm_size": pcm_size,
        "avg_time_ms": avg_time_ms,
        "iterations": iterations
    }

def main():
    if not os.path.exists(FAAD_BIN):
        print(f"FAAD binary not found at {FAAD_BIN}. Please build project first.")
        sys.exit(1)

    print("=== FAAD Benchmark Harness ===")
    sizes = get_binary_sizes()
    print(f"libfaad.so size: text={sizes.get('text')}, data={sizes.get('data')}, bss={sizes.get('bss')}, total={sizes.get('total')}")

    results = {
        "sizes": sizes,
        "files": []
    }

    for f in TEST_FILES:
        if not os.path.exists(f):
            print(f"Warning: {f} does not exist, skipping.")
            continue
        res = benchmark_file(f, iterations=15)
        if res:
            print(f"File: {res['file']:<45} | Time: {res['avg_time_ms']:6.2f} ms | PCM Size: {res['pcm_size']:8d} B | Hash: {res['pcm_hash'][:12]}")
            results["files"].append(res)

    output_path = "/tmp/faad_benchmark_results.json"
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    with open(output_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"Results written to {output_path}")

if __name__ == "__main__":
    main()
