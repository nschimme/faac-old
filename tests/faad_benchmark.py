#!/usr/bin/env python3
"""
FAAD 3.0 Hardened Multi-Encoder & Multi-Profile Benchmark & MOS Quality Suite
Tests decoding performance, PCM SHA-256 hash consistency, and ViSQOL MOS audio quality
across FAAC, FFmpeg, and FDK-AAC encoded bitstreams across AAC-LC, HE-AAC v1, and HE-AAC v2 profiles.
"""

import os
import sys
import time
import json
import wave
import math
import struct
import hashlib
import subprocess

# Add /opt/faac-benchmark to path for MOS computation if available
sys.path.append("/opt/faac-benchmark")
try:
    import phase2_mos
    HAVE_FAAC_BENCHMARK = True
except ImportError:
    HAVE_FAAC_BENCHMARK = False

FAAD_BIN = os.path.abspath("build/frontend/faad")
FAAC_BIN = os.path.abspath("build/frontend/faac")
LIBFAAD_SO = os.path.abspath("build/libfaad/libfaad.so")
BENCH_DIR = "/tmp/faad_hardening_bench"

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

def generate_reference_wav(filepath, duration=3.0, sample_rate=44100):
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    num_samples = int(sample_rate * duration)
    with wave.open(filepath, "w") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        buf = bytearray()
        for i in range(num_samples):
            # Complex multi-harmonic audio signal
            s1 = int(12000 * math.sin(2 * math.pi * 440.0 * i / sample_rate))
            s2 = int(6000 * math.sin(2 * math.pi * 880.0 * i / sample_rate))
            s3 = int(3000 * math.sin(2 * math.pi * 1760.0 * i / sample_rate))
            val = s1 + s2 + s3
            if val > 32767: val = 32767
            if val < -32768: val = -32768
            buf += struct.pack("<hh", val, val)
        w.writeframes(buf)
    return filepath

def encode_audio_matrix(ref_wav):
    os.makedirs(BENCH_DIR, exist_ok=True)
    encoded_files = []

    # 1. FAAC Encodings
    if os.path.exists(FAAC_BIN):
        # AAC-LC
        faac_lc = os.path.join(BENCH_DIR, "faac_lc.m4a")
        subprocess.run([FAAC_BIN, "-q", "100", ref_wav, "-o", faac_lc], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.exists(faac_lc):
            encoded_files.append(("FAAC", "AAC-LC", faac_lc))

        # HE-AAC v1
        faac_he1 = os.path.join(BENCH_DIR, "faac_he_v1.m4a")
        subprocess.run([FAAC_BIN, "-b", "64", ref_wav, "-o", faac_he1], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.exists(faac_he1):
            encoded_files.append(("FAAC", "HE-AAC v1", faac_he1))

    # 2. FFmpeg Encodings
    ffmpeg_bin = shutil_which("ffmpeg")
    if ffmpeg_bin:
        # AAC-LC via FFmpeg native
        ff_lc = os.path.join(BENCH_DIR, "ffmpeg_lc.m4a")
        subprocess.run([ffmpeg_bin, "-y", "-i", ref_wav, "-c:a", "aac", "-b:a", "128k", ff_lc], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.exists(ff_lc):
            encoded_files.append(("FFmpeg", "AAC-LC", ff_lc))

        # FDK-AAC via FFmpeg if available
        ff_fdk_lc = os.path.join(BENCH_DIR, "fdk_lc.m4a")
        res = subprocess.run([ffmpeg_bin, "-y", "-i", ref_wav, "-c:a", "libfdk_aac", "-profile:a", "aac_low", "-b:a", "128k", ff_fdk_lc], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res.returncode == 0 and os.path.exists(ff_fdk_lc):
            encoded_files.append(("FDK-AAC", "AAC-LC", ff_fdk_lc))

        ff_fdk_he1 = os.path.join(BENCH_DIR, "fdk_he_v1.m4a")
        res = subprocess.run([ffmpeg_bin, "-y", "-i", ref_wav, "-c:a", "libfdk_aac", "-profile:a", "aac_he", "-b:a", "64k", ff_fdk_he1], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res.returncode == 0 and os.path.exists(ff_fdk_he1):
            encoded_files.append(("FDK-AAC", "HE-AAC v1", ff_fdk_he1))

        ff_fdk_he2 = os.path.join(BENCH_DIR, "fdk_he_v2.m4a")
        res = subprocess.run([ffmpeg_bin, "-y", "-i", ref_wav, "-c:a", "libfdk_aac", "-profile:a", "aac_he_v2", "-b:a", "32k", ff_fdk_he2], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res.returncode == 0 and os.path.exists(ff_fdk_he2):
            encoded_files.append(("FDK-AAC", "HE-AAC v2", ff_fdk_he2))

    return encoded_files

def shutil_which(cmd):
    import shutil
    return shutil.which(cmd)

def compute_mos(ref_wav, decoded_wav):
    if not HAVE_FAAC_BENCHMARK:
        return 1.0  # Default fallback if phase2_mos not imported
    try:
        score = phase2_mos.compute_visqol_single(ref_wav, decoded_wav)
        return float(score) if score is not None else 1.0
    except Exception:
        # Calculate SNR-based spectral MOS fallback
        try:
            with wave.open(ref_wav, "r") as rw, wave.open(decoded_wav, "r") as dw:
                r_bytes = rw.readframes(rw.getnframes())
                d_bytes = dw.readframes(dw.getnframes())
                min_len = min(len(r_bytes), len(d_bytes)) // 2
                r_samples = struct.unpack(f"<{min_len}h", r_bytes[:min_len*2])
                d_samples = struct.unpack(f"<{min_len}h", d_bytes[:min_len*2])
                signal_pwr = sum(r*r for r in r_samples) + 1e-9
                noise_pwr = sum((r - d)*(r - d) for r, d in zip(r_samples, d_samples)) + 1e-9
                snr = 10.0 * math.log10(signal_pwr / noise_pwr)
                mos = 1.0 + 3.8 / (1.0 + math.exp(-0.15 * (snr - 15.0)))
                return round(mos, 2)
        except Exception:
            return 1.0

def benchmark_file(encoder_name, profile, filepath, ref_wav, iterations=10):
    pcm_out = os.path.join(BENCH_DIR, f"dec_{os.path.basename(filepath)}.wav")
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

    mos_score = compute_mos(ref_wav, pcm_out)

    # Benchmark run
    start_time = time.perf_counter()
    for _ in range(iterations):
        subprocess.run([FAAD_BIN, "-w", filepath], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end_time = time.perf_counter()

    avg_time_ms = ((end_time - start_time) / iterations) * 1000.0

    return {
        "encoder": encoder_name,
        "profile": profile,
        "file": os.path.basename(filepath),
        "pcm_hash": pcm_hash,
        "pcm_size": pcm_size,
        "mos": mos_score,
        "avg_time_ms": avg_time_ms,
        "iterations": iterations
    }

def main():
    if not os.path.exists(FAAD_BIN):
        print(f"FAAD binary not found at {FAAD_BIN}. Please build project first.")
        sys.exit(1)

    print("=== FAAD 3.0 Hardened Multi-Encoder & Multi-Profile Benchmark ===")
    sizes = get_binary_sizes()
    print(f"libfaad.so size: text={sizes.get('text')}, data={sizes.get('data')}, bss={sizes.get('bss')}, total={sizes.get('total')}\n")

    ref_wav = os.path.join(BENCH_DIR, "ref_44100_stereo.wav")
    generate_reference_wav(ref_wav, duration=3.0)

    encoded_matrix = encode_audio_matrix(ref_wav)
    print(f"Generated {len(encoded_matrix)} bitstream test vectors across FAAC, FFmpeg, FDK-AAC.\n")

    print(f"{'Encoder':<10} | {'Profile':<12} | {'Time (ms)':<10} | {'PCM Size (B)':<10} | {'MOS Score':<8} | {'Hash (SHA256)':<12}")
    print("-" * 72)

    results = {
        "sizes": sizes,
        "files": []
    }

    for enc, prof, fpath in encoded_matrix:
        res = benchmark_file(enc, prof, fpath, ref_wav, iterations=10)
        if res:
            print(f"{res['encoder']:<10} | {res['profile']:<12} | {res['avg_time_ms']:10.2f} | {res['pcm_size']:10d} | {res['mos']:8.2f} | {res['pcm_hash'][:12]}")
            results["files"].append(res)

    output_path = "/tmp/faad_benchmark_results.json"
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    with open(output_path, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\nBenchmark completed successfully. Results saved to {output_path}")

    # Cleanup temporary benchmark directory
    import shutil
    shutil.rmtree(BENCH_DIR, ignore_errors=True)

if __name__ == "__main__":
    main()
