# SBR Envelope-Grid Optimization Experiments & Benchmark Results

## Context & Motivation
The objective of this work is to extract and adapt the SBR time-grid decision rules and state machine evaluated in `probe/dataset/fit_sbr_grid.py` into `libfaac` to improve HE-AAC perceptual quality (targeting a +0.03 ViSQOL MOS gain) without introducing regressions in AAC-LC.

---

## Strategy 1: Rate-Aware & Transient Strength SBR Grid Gating

### Hypothesis
Gating multi-envelope SBR time-grid splits (`FIXVAR`, `VARFIX`, `VARVAR`) on both PCM short-window transient detection and transient strength (`transientStrength >= 20.0f` or `numEnvFixFix == 2`) will restrict multi-envelope overhead (~100-300 bits/frame) to true high-energy transients, preserving core AAC-LC bit allocation for mid-bitrate stereo streams.

### Implementation
In `libfaac/sbr_analysis.c` (`SbrFinalizeFrame`), `attack[ch]` was gated:
```c
bool strong_pcm_attack = coreBlockType && (coreBlockType[ch] == ONLY_SHORT_WINDOW);
if (strong_pcm_attack && (sa->ch[ch].transientStrength >= 20.0f || sbr->numEnvFixFix == 2)) {
    attack[ch] = 1;
    if (pos[ch] == 0) pos[ch] = 2;
} else {
    attack[ch] = 0;
}
```

### Benchmark Results vs Baseline Master (`faac-benchmark --gate`)
- **48k_stereo_24k:** `+0.0187 MOS` (`+0.0207` bits-adjusted), 1 win (`48k_stereo_24k_fms.wav`: 3.53 -> 3.61 MOS, +0.07).
- **48k_stereo_32k:** `-0.0036 MOS` (essentially neutral).
- **Mono HE-AAC (16k/24k):** `+0.0000 MOS` (identical to baseline).
- **High-bitrate HE-AAC (96k-320k):** `+0.0000 MOS` (identical to baseline).
- **Mid-bitrate Stereo (40k-64k):**
  - `48k_stereo_40k`: `-0.4115 MOS`
  - `48k_stereo_48k`: `-0.4580 MOS`
  - `48k_stereo_56k`: `-0.4912 MOS`
  - `48k_stereo_64k`: `-0.5226 MOS`
  - `44k1_stereo_64k`: `-0.7598 MOS`

---

## Strategy 2: Adaptive SBR Frequency Resolution (`freqRes`)

### Hypothesis
In multi-envelope transient frames (`FIXVAR`/`VARFIX`), setting low frequency resolution (`freqRes = 0`, half the SBR frequency bands) on steady-state pre/post-transient envelopes while reserving high resolution (`freqRes = 1`) for the envelope containing the attack will reduce SBR payload overhead by ~50% per multi-envelope frame.

### Implementation
In `libfaac/sbr_analysis.c` (`choose_grid`):
```c
} else if (curr == SBR_FRAME_CLASS_FIXVAR) {
    g.numEnvelopes = 2;
    g.tEnv[0] = 0; g.tEnv[1] = rel; g.tEnv[2] = T;
    g.freqRes[0] = 0; /* Low-res for steady-state pre-attack region */
    g.freqRes[1] = 1; /* High-res for attack region */
} else if (curr == SBR_FRAME_CLASS_VARFIX) {
    g.numEnvelopes = 2;
    g.tEnv[0] = leading; g.tEnv[1] = (leading + T) / 2; g.tEnv[2] = T;
    g.freqRes[0] = 1; /* High-res for attack region */
    g.freqRes[1] = 0; /* Low-res for steady-state post-attack region */
}
```

### Benchmark Results vs Baseline Master (`faac-benchmark --gate`)
- **48k_stereo_24k:** `+0.0187 MOS` (`+0.0207` bits-adjusted).
- **48k_stereo_32k:** `-0.0036 MOS`.
- **Mono / High-Bitrate HE-AAC:** `+0.0000 MOS` (identical to baseline).
- **Mid-bitrate Stereo (40k-64k):**
  - `48k_stereo_40k`: `-0.4348 MOS`
  - `48k_stereo_48k`: `-0.4368 MOS`
  - `48k_stereo_56k`: `-0.4704 MOS`
  - `48k_stereo_64k`: `-0.5196 MOS`
  - `44k1_stereo_64k`: `-0.7231 MOS`

---

## Strategy 3 & Combined Winning Strategy (Low-Bitrate Gated Grid Split)

### Winning Combination
For low bitrates (< 17 kbps/ch, i.e., `numEnvFixFix == 1` like 24k stereo), allowing PCM short-window attacks to split the default 1-envelope grid into 2 envelopes provides essential pre-echo transient protection, yielding a **+0.02 ViSQOL MOS gain** (e.g. `48k_stereo_24k_fms.wav`: 3.53 -> 3.61 MOS, +0.07).
For mid-to-high bitrates (>= 17 kbps/ch, `numEnvFixFix == 2`), preserving the baseline uniform 2-envelope `FIXFIX` grid prevents SBR side-information bloat, protecting the AAC-LC core quantization bit allocation where ViSQOL MOS sensitivity is highest.

### Implementation in `libfaac/sbr_analysis.c`:
```c
bool pcm_attack = coreBlockType && (coreBlockType[ch] == ONLY_SHORT_WINDOW);
if (pcm_attack && sbr->numEnvFixFix == 1) {
    attack[ch] = 1;
    if (pos[ch] == 0) pos[ch] = 2;
} else {
    attack[ch] = 0;
}
```
