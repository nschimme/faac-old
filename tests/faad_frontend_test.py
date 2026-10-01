#!/usr/bin/env python3
"""Round-trip the faad frontend through faac: ADTS and MP4 input, WAV to a
file and to stdout.

Usage: faad_frontend_test.py <faac> <faad>
"""

import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


def make_wav(path, rate=44100, secs=2):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(
            struct.pack("<hh",
                        int(8000 * math.sin(2 * math.pi * 440 * i / rate)),
                        int(8000 * math.sin(2 * math.pi * 660 * i / rate)))
            for i in range(rate * secs)))


def wav_sizes(data):
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", "not a WAV"
    i = data.index(b"data")
    return struct.unpack("<I", data[4:8])[0], struct.unpack("<I", data[i + 4:i + 8])[0], len(data) - (i + 8)


def main():
    faac, faad = sys.argv[1:3]
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "in.wav")
        make_wav(src)
        for ext in ("aac", "m4a"):
            enc = os.path.join(d, "t." + ext)
            subprocess.run([faac, "-o", enc, src], check=True, capture_output=True)

            out = os.path.join(d, ext + ".wav")
            subprocess.run([faad, "-q", "-o", out, enc], check=True)
            riff, data, actual = wav_sizes(open(out, "rb").read())
            assert data == actual > 0 and riff == data + 36, f"{ext}: bad WAV header {riff}/{data}/{actual}"

            piped = subprocess.run([faad, "-q", "-w", enc], check=True, capture_output=True).stdout
            riff, data, actual = wav_sizes(piped)
            assert actual > 0, f"{ext}: -w produced no PCM"
            assert riff == 0xFFFFFFFF and data == 0xFFFFFFFF, f"{ext}: -w header must declare a streaming length, got {riff}/{data}"
            assert piped[44:] == open(out, "rb").read()[44:], f"{ext}: -w PCM differs from -o PCM"
    print("faad frontend: ok")


if __name__ == "__main__":
    main()
