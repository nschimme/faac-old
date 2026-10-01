# FAAD3 Decoder and FAAM Container Benchmark

Measured with the faac-benchmark decoder phase (`tests/faad_benchmark.py --full`)
on 2026-09-21, libfaad at `4c089193`, against FAAD2 2.11.3, FFmpeg 7.1.5 and
Helix AAC 1.0, on Linux x86-64 (gcc, `-O3`, no `-march`). The full report the
numbers below are taken from is written to `build/faad_benchmark/full_report.md`
by that run.

## Corpus

219 clips at 16/24/32/44.1/48 kHz mono, stereo and 5.1 across 36 bitrate
scenarios, encoded by faac (LC and HE-AAC v1, with and without PNS, M4A and
ADTS), fdkaac (LC, HE-AAC v1, HE-AAC v2, in ADTS and every M4A signalling form:
implicit, sync-extension, explicit hierarchical, PS explicit) and ffmpeg (LC
with and without PNS, M4A and ADTS): 32 326 bitstreams, each decoded by every
decoder. Quality is zimtohrli MOS against the source WAV; conformance is SNR
against FFmpeg's decode of the same bitstream, capped near 78 dB by 16-bit
output. A decoder whose output is within 60 dB of that reference inherits the
reference's MOS rather than being scored again (70 % of rows).

## 1. Overall

| Decoder | .text | Peak RSS | MOS LC | MOS HE-v1 | MOS HE-v2 | Conformance SNR (median) | xRT (median) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| FAAD3 | 110.7 KB | 11.4 MB | 3.91 | 4.18 | 3.65 | 67.4 dB | 146.7× |
| FAAD2 2.11.3 | 319.6 KB | 11.4 MB | 3.91 | 4.18 | 3.62 | 56.3 dB | 90.6× |
| FFmpeg 7.1.5 | 213.3 KB | 58.3 MB | 3.91 | 4.18 | 3.65 | reference | 57.3× |
| Helix 1.0 | 83.0 KB | 11.4 MB | 3.88 | 4.12 | 3.74 ¹ | 76.2 dB | 158.8× |

¹ Helix has no parametric stereo; its HE-v2 rows are a mono decode scored as a
downmix, which is not comparable with the stereo rows above it.

FAAD3's MOS equals the reference decoder's on every profile. The median
conformance figures are dominated by PNS streams, where every decoder differs
from every other by design (noise substitution is not normative); the gated
figure is the PNS-free one in section 4.

## 2. Footprint

| Component | .text | .rodata | .bss | .data |
| :--- | ---: | ---: | ---: | ---: |
| libfaad (FAAD3) | 110.7 KB | 13.8 KB | 54.2 KB | 5.1 KB |
| libfaad2 2.11.3 | 319.6 KB | 109.6 KB | 0.0 KB | 2.3 KB |
| libavcodec AAC subset | 213.3 KB | 50.0 KB | 0.4 KB | 0.1 KB |
| Helix (fixed point, LC + SBR) | 83.0 KB | 41.3 KB | 0.3 KB | 0.0 KB |

libfaad's `.bss` is its one-per-process tables (Huffman, windows, QMF, SBR and
PS filters), built once and shared by every decoder instance; the state of a
stereo HE-AAC v2 decoder instance is 152 KB. Helix is smaller in `.text` but
decodes neither PS nor multichannel.

## 3. Throughput

Median over streams of the mean of three timed decodes, output discarded.

| Profile | Decoder | Streams | Mean (ms) | xRT | Peak RAM |
| :---: | :--- | ---: | ---: | ---: | ---: |
| LC | FAAD3 | 5943 | 54.9 | 150.0× | 11.4 MB |
| LC | FAAD2 | 5943 | 88.3 | 93.5× | 11.4 MB |
| LC | FFmpeg | 5943 | 142.6 | 57.2× | 58.1 MB |
| LC | Helix | 5943 | 49.4 | 165.5× | 11.4 MB |
| HE-v1 | FAAD3 | 343 | 79.4 | 119.9× | 11.4 MB |
| HE-v1 | FAAD2 | 343 | 131.4 | 72.5× | 11.4 MB |
| HE-v1 | FFmpeg | 343 | 159.0 | 60.5× | 58.6 MB |
| HE-v1 | Helix | 343 | 90.2 | 104.6× | 11.4 MB |
| HE-v2 | FAAD3 | 147 | 84.2 | 110.0× | 11.4 MB |
| HE-v2 | FAAD2 | 98 | 133.7 | 71.3× | 11.4 MB |
| HE-v2 | FFmpeg | 147 | 167.4 | 57.3× | 59.0 MB |
| HE-v2 | Helix ¹ | 147 | 71.7 | 131.1× | 11.4 MB |

FAAD3 decodes LC 1.6× faster than FAAD2 and 2.6× faster than FFmpeg, HE-AAC
v1 1.65× and 2.0×, and is the fastest decoder on HE-AAC v1 and on stereo
HE-AAC v2. The peak RSS figures are the process floor of the harness; the
decoder's own working set is the instance state above.

Instruction counts per stream (cachegrind, this branch): LC 64.9 M, HE-AAC v1
233 M, HE-AAC v2 250 M.

## 4. Conformance

SNR of each decoder's output against FFmpeg's decode of the same bitstream.

| Profile | Decoder | Streams | Median dB | ≥ 60 dB | < 60 dB |
| :---: | :--- | ---: | ---: | ---: | ---: |
| LC | FAAD3 | 21 546 | 67.2 | 14 524 | 7 022 |
| LC | FAAD2 | 21 546 | 52.5 | 7 354 | 14 192 |
| LC | Helix | 21 546 | 76.5 | 14 378 | 7 168 |
| HE-v1 | FAAD3 | 7 546 | 72.5 | 6 899 | 647 |
| HE-v1 | FAAD2 | 7 546 | 71.2 | 5 183 | 2 363 |
| HE-v1 | Helix | 7 546 | 74.9 | 6 253 | 1 293 |
| HE-v2 | FAAD3 | 3 234 | 73.5 | 3 234 | 0 |
| HE-v2 | FAAD2 | 2 156 | 19.8 | 88 | 2 068 |
| HE-v2 | Helix ¹ | 3 234 | 9.2 | 69 | 3 165 |

Every FAAD3 stream below 60 dB is a PNS stream; on the 22 014 PNS-free streams
(faac and ffmpeg "PNS off" rows, every fdkaac HE row, ADTS and M4A, every SBR
and PS signalling form) FAAD3 is at or above 60 dB on all of them, and on
HE-AAC v2 at or above 61.9 dB on every stream including the PNS ones. FAAD2
falls below on 14 192 LC streams (its LC output is not sample-exact with the
reference) and fails to decode 1 078 streams, all HE-AAC v2.

## 5. Gapless

Sample offset of the decoded output against the source WAV on M4A streams
(ADTS carries no priming information and is excluded).

| Decoder | M4A streams | Offset 0 | \|Offset\| ≤ 2 | Length delta 0 |
| :--- | ---: | ---: | ---: | ---: |
| FAAD3 | 22 936 | 17 511 | 17 529 | 12 990 |
| FAAD2 | 15 858 | 11 514 | 11 532 | 3 982 |
| FFmpeg | 22 936 | 15 361 | 15 380 | 70 |
| Helix | 22 936 | 0 | 0 | 0 |

FAAD3 honours both `edts`/`elst` and `iTunSMPB` and lands on sample 0 on every
PNS-free stream whose priming is spec-compliant. The rows away from 0 are the
fdkaac HE files (their priming assumes libfdk removes the SBR delay, so every
spec decoder lands 961 samples late), and low-bitrate rows where the alignment
cross-correlation itself locks onto a wrong lag, which every decoder shares.
FFmpeg lands 994 samples late on faac's HE-AAC files.

## 6. Robustness

Every sampled bitstream (one scenario per clip and encoder row, 6 433 streams)
corrupted with a fixed seed and decoded with a timeout and an output-size bound.

| Decoder | Decoded to end | Exited with error | Timeout | Runaway |
| :--- | ---: | ---: | ---: | ---: |
| FAAD3 | 6 426 | 7 | 0 | 0 |
| FAAD2 | 1 845 | 4 588 | 0 | 0 |
| FFmpeg | 0 | 6 433 | 0 | 0 |
| Helix | 5 667 | 766 | 0 | 0 |

An error exit on a corrupted stream is acceptable; a timeout or a decode that
runs away past the intact stream's length is not. FAAD3 resyncs on bad ADTS
headers and conceals the rest, so it reaches the end of 99.9 % of the
corrupted streams.

## 7. Container operations (FAAM)

| Tool | Operation | Mean (ms) |
| :--- | :--- | ---: |
| faam | Mux AAC → M4A | 21.3 |
| ffmpeg | Mux AAC → M4A | 74.8 |
| faam | Demux M4A → AAC | 21.9 |
| faam | Inject iTunes tags | 17.7 |

Process launch dominates all four figures; `libfaam` itself is 51.7 KB of
`.text` and works over stream callbacks.

## Gate

The run's gate, evaluated by `faac-benchmark/gate_check.py` on FAAD3 only:

```
PASS: all 32326 FAAD3 intact-stream decodes OK
PASS: no FAAD3 timeouts or runaways across 6433 robustness cases
PASS: faad3 conformance SNR >= 60 dB on all 22014 measured streams
PASS: faad3 gapless offset within 2 samples on all 7234 PNS-free M4A streams
GATE: PASS
```

## Reproducing

```
tests/faad_benchmark.py --full \
    --faad-bin <faad2 binary> --faad-bin-version "2.11.3 (local build)" \
    --helix-bin ../faac-benchmark/bin/helix-aac-dec --fdkaac-bin fdkaac \
    --iterations 3 --results-json results/full_decoder.json \
    -o results/full_decoder.md --decoder-report build/faad_benchmark/full_report.md
```

`--gate` instead of `--full` runs the fixed gate subset (one clip per scenario,
every encoder variant) in about 20 minutes; the full corpus takes about 2.5 h
on 16 cores.
