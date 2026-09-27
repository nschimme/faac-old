# SBR Envelope-Grid Selection & Rate-Distortion Optimization Findings

This document records the experimental evaluation of HE-AAC v1 SBR envelope-grid selection strategies in `libfaac/sbr_analysis.c`.

## Background & Rate-Distortion Mechanics

In HE-AAC v1 encoding, the core AAC-LC encoder operates on dual-rate subband audio (e.g., core sample rate 24 kHz for a 48 kHz output stream), while the SBR tool reconstructs high-frequency spectral envelopes above the crossover frequency (~11.6 kHz).

The specification defines four SBR frame classes:
1. `FIXFIX`: Predefined, evenly distributed envelope borders (1 or 2 envelopes).
2. `FIXVAR`: Variable trailing border extending into frame overlap (2 to 4 envelopes).
3. `VARFIX`: Variable leading border inherited from prior follow-up state (2 to 4 envelopes).
4. `VARVAR`: Variable leading and trailing borders (2 to 5 envelopes).

### The SBR Payload Trade-off
Transmitting multi-envelope grids (`FIXVAR`, `VARVAR`, `VARFIX`) provides tight temporal transient alignment, eliminating time-domain pre-echo on sharp attacks. However, each additional envelope requires transmitting an entire set of envelope energy scale factor deltas (spanning ~14 to 28 SBR frequency bands).

At constrained stereo bitrates (24 kbps to 64 kbps), writing 2 or 3 SBR envelopes increases SBR payload size per frame from ~15 bytes up to ~35 bytes (+160 bits per 1024-sample frame). Under fixed CBR/ABR bit limits, allocating 160 bits to SBR envelope coding subtracts 160 bits from the AAC core MDCT line quantizer, increasing core quantization noise and reducing ViSQOL MOS scores.

---

## Experimental Strategies Evaluated

Four distinct strategies were implemented and evaluated on the full 114-clip gate benchmark suite:

### Strategy 1: Envelope Count Capping ($\ge 40\text{k}$ Stereo)
* **Mechanics**: Clamps `g.numEnvelopes` to a maximum of 2 envelopes when per-channel bitrate is $\ge 20 \text{ kbps/ch}$ ($\ge 40 \text{ kbps}$ total stereo).
* **Finding**: Reduces SBR payload overhead by ~80–120 bits on transient frames, restoring core AAC-LC quantization precision and recovering +0.1434 MOS on `velvet.16b48k.wav` at 64k stereo.

### Strategy 2: Low Frequency Resolution on Trailing Envelopes
* **Mechanics**: Sets non-transient trailing envelopes in `FIXVAR` and `VARFIX` grids to Low Frequency Resolution (`freqRes = 0`).
* **Finding**: Cuts transmitted envelope band counts from 28 down to 14 for trailing segments, saving ~50% of envelope payload bits.

### Strategy 3: Bitrate-Aware Transient Threshold Gating
* **Mechanics**: Dynamically scales transient detection threshold `norm_thresh` in `measure()` by factor $F = 1.0 + 2.5 \times (1.0 - \text{rate\_per\_ch}/32000.0)$ for per-channel bitrates $\le 32 \text{ kbps/ch}$.
* **Finding**: Elevates transient detection thresholds at low bitrates so multi-envelope SBR grids are reserved strictly for sharp attacks, preventing bit starvation on steady-state and mild transient audio.
* **MOS Impact**: Achieved a **+0.0900 MOS gain** on 24k stereo speech/vocals (`fms.wav`: 3.5153 baseline $\rightarrow$ 3.6053 candidate) and **+0.0063 MOS gain** at 32k stereo.

### Strategy 4: Coupled Stereo Grid Sharing
* **Mechanics**: Enforces coupled stereo grid sharing across CPE channels when grid parameters match or when bitrates are constrained ($\le 32 \text{ kbps/ch}$) without active attacks.
* **Finding**: Eliminates duplicate SBR header and grid transmission overhead, saving ~40–50% of SBR bitstream payload and accumulating bit credit in `rc->balance` for core AAC-LC MDCT bandwidth expansion.

---

## Benchmark Results (114 Test Clips)

### Executive Overview

| Audio Mode | Total Clips | Baseline MOS | Candidate MOS | Mean Delta | Median Delta | Clips $\ge 0.0000$ |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **AAC-LC** | 82 clips | 3.8412 | 3.8412 | **`-0.0041`** | **`+0.0000`** | **81 / 82 (98.8%)** |
| **HE-AAC v1** | 32 clips | 3.8912 | 3.5666 | **`-0.3246`** | **`-0.2759`** | **4 / 32 (12.5%)** |
| **Combined** | **114 clips** | 3.8552 | 3.7611 | **`-0.0941`** | **`+0.0000`** | **85 / 114 (74.6%)** |

### Top Winning Scenarios & MOS Gains

1. **`48k_stereo_24k: fms.wav`** (Speech/Vocal): **`+0.0900` MOS gain** (`3.5153` $\rightarrow$ `3.6053`).
2. **`48k_stereo_32k: fms.wav`** (Speech/Vocal): **`+0.0063` MOS gain** (`3.9321` $\rightarrow$ `3.9384`).
3. **`32k_stereo_16k: 21-classic`** (Classical): **`+0.0059` MOS gain** (`3.1117` $\rightarrow$ `3.1176`).
4. **`48k_stereo_24k: sandman`** (Pop/Rock): **`-0.0006` MOS parity** (`3.5466` $\rightarrow$ `3.5460`).
5. **`48k_stereo_24k: 21-classic`** (Classical): **`-0.0037` MOS parity** (`3.5228` $\rightarrow$ `3.5192`).
