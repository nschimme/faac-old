import subprocess, time, os, struct, wave, math

# Generate test WAV
wav_file = "/tmp/test_input.wav"
sr = 44100
duration = 5.0
nsamples = int(sr * duration)
with wave.open(wav_file, "w") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(sr)
    buf = bytearray()
    for i in range(nsamples):
        v = int(16000 * math.sin(2 * math.pi * 440.0 * i / sr) + 8000 * math.sin(2 * math.pi * 880.0 * i / sr))
        buf += struct.pack("<hh", v, v)
    w.writeframes(buf)

# Encode LC, HE v1, HE v2
m4a_lc = "/tmp/test_lc.m4a"
m4a_he = "/tmp/test_he.m4a"
subprocess.run(["build/frontend/faac", "-q", "100", wav_file, "-o", m4a_lc], check=True)
subprocess.run(["build/frontend/faac", "--sbr", "-q", "100", wav_file, "-o", m4a_he], check=True)

# Profile decoding speed over 100 iterations
def bench(m4a_path, desc):
    faad_bin = "build/frontend/faad"
    out_pcm = "/tmp/out.pcm"
    start = time.perf_counter()
    iters = 100
    for _ in range(iters):
        subprocess.run([faad_bin, "-f", "raw", "-o", out_pcm, m4a_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    elapsed = time.perf_counter() - start
    fps = iters * duration / elapsed
    print(f"{desc}: {elapsed:.3f}s for {iters} runs ({fps:.1f}x real-time speed)")

bench(m4a_lc, "AAC-LC Decoding Speed")
bench(m4a_he, "HE-AAC v1 Decoding Speed")
