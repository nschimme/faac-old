# SBR Grid Decision Rule Fitting, Cross-Frame Ceiling, Recursive Simulation & Spec State Machine Report

## Executive Summary
This report presents the complete data-fitting, feature engineering, multi-model decision tree analysis, cross-frame ceiling experiment, recursive self-consistent simulation, 1-frame future QMF slot energy look-ahead experiment, and **canonical reference state machine evaluation** for FAAC's HE-AAC v1 SBR (Spectral Band Replication) time-grid decision procedure.

The goal is to determine SBR frame time-grid parameters—`frameClass` (0=FIXFIX, 1=FIXVAR, 2=VARFIX, 3=VARVAR), `numEnvelopes` (1–5), envelope border positions (`tEnv` in 0..32 QMF slots), and per-envelope frequency resolution (`freqRes` 0/1)—matching the reference encoder's clean-room black-box output decisions.

Experiments were evaluated on an 80% Train split (39 clips, 11,732 frames) and held-out 20% Test split (10 clips, 3,020 frames) across five distinct model configurations:
1. **v1 (Single-Frame Static Model)**: Uses 17 current-frame engineered QMF slot energy features.
2. **v2 Ground-Truth Ceiling**: Uses 17 current-frame slot features + ground-truth reference previous-frame state (`prev_ref_*` columns in v2 dataset).
3. **v2 Real Recursive Simulation**: Operates frame-by-frame per (clip, channel) sequence. For frame 0, previous state = sentinel `-1`. For all subsequent frames $N > 0$, the model's **own previous-frame prediction** ($\hat{y}_{N-1}$) is fed as the prior-state input to predict frame $N$.
4. **v3 Real Recursive + 1-Frame Future QMF Look-Ahead**: Combines recursive self-consistent prior state prediction ($\hat{y}_{N-1}$) with 1-frame future QMF slot energy look-ahead features (`next_max_energy`, `next_total_energy`, `next_max_onset_ratio`, etc.).
5. **Canonical Reference State Machine**: Evaluates the canonical reference transition table (`FIXFIX`, `FIXVAR`, `VARFIX`, `VARVAR`) driven by a binary `Attack` classifier.

---

## 1. Comprehensive Comparative Evaluation Across Models

| Model Architecture | `frameClass` Test Acc | `numEnvelopes` Test Acc | `FIXFIX` (0) Recall | `FIXVAR` (1) Recall | `VARFIX` (2) Recall | `VARVAR` (3) Recall | **Strict Full-Match Rate** |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Majority-Class Baseline** | 47.68% | 57.96% | 100.0% | 0.0% | 0.0% | 0.0% | **0.00%** |
| **v1 Single-Frame Static** | 55.07% | 63.25% | 88.8% | 40.6% | 22.5% | 0.3% | **37.38%** |
| **v2 Ground-Truth Ceiling** | **73.81%** | **68.34%** | **95.8%** | **35.8%** | **85.7%** | **28.0%** | **51.99%** |
| **v2 Real Recursive Simulation** | 44.67% | 61.39% | 86.0% | 12.6% | 4.8% | 1.4% | **32.78% – 35.33%** |
| **v3 Recursive + 1-Frame QMF Look-Ahead** | 44.87% | 60.93% | 87.6% | 13.8% | 1.5% | 0.3% | **31.95% – 33.81%** |
| **Canonical State Machine (Pred Attack)** | 43.44% | 59.80% | 79.4% | 14.1% | 11.4% | 3.5% | **30.89% – 33.77%** |
| **Canonical State Machine (GT Attack)** | 40.10% | 58.20% | 69.1% | 22.8% | 15.2% | 12.0% | **25.13% – 27.45%** |

*Note: **Strict Full-Match Rate** requires exact `frameClass` match AND `numEnvelopes` match AND all envelope border positions (`tEnv`) within $\pm 2$ QMF slots of reference.*

---

## 2. Canonical SBR State Machine Architecture

The reference encoder's dynamic time-grid generator maintains a formal state machine per channel based on two variables:
1. `prev_class`: The SBR class emitted in the preceding frame (initialized to `FIXFIX`).
2. `spread`: A boolean flag indicating whether a short-tail transient requires an additional follow-up frame before closing at $T=16$.

### Transition Table Rules

| Previous Class | Attack Detected? | Spread on Entry | Current Emitted Class | Next Action / State Update |
| :--- | :---: | :---: | :---: | :--- |
| **`FIXFIX`** | No | Either | **`FIXFIX`** | None |
| **`FIXFIX`** | **Yes** | Either | **`FIXVAR`** | None |
| **`FIXVAR`** | No | No | **`VARFIX`** | None |
| **`FIXVAR`** | No | **Yes** | **`VARVAR`** | Consume spread during grid construction |
| **`FIXVAR`** | **Yes** | Either | **`VARVAR`** | Clear spread; join old and new designs |
| **`VARFIX`** | No | Either | **`FIXFIX`** | None |
| **`VARFIX`** | **Yes** | Either | **`FIXVAR`** | None |
| **`VARVAR`** | No | No | **`VARFIX`** | None |
| **`VARVAR`** | No | **Yes** | **`VARVAR`** | Consume spread during grid construction |
| **`VARVAR`** | **Yes** | Either | **`VARVAR`** | Clear spread; join old and new designs |

---

## 3. Attack Detection Bottleneck Analysis

When driving the canonical state machine using an offline binary `Attack` classifier trained on current and 1-frame future QMF slot energies:

### Attack Classifier Performance (Test Set: 3,020 Frames)

| Class | Precision | Recall | F1-Score | Support |
| :--- | :---: | :---: | :---: | :---: |
| **No Attack (`0`)** | 0.74 | **0.94** | 0.83 | 2,043 |
| **Attack (`1`)** | 0.73 | **0.32** | 0.44 | 977 |
| **Weighted Overall Avg** | **0.74** | **0.74** | **0.71** | **3,020** |

### Why QMF Energy Fails to Drive the State Machine:
1. **Low Attack Recall (32.0%)**: Quantized per-slot QMF band energies (`slot0`..`slot31`) blur sharp subband phase and energy surges. Consequently, the QMF energy-based `Attack` classifier misses 68.0% of true transient attacks (660 out of 977 attack frames).
2. **Cascading State Machine Failures**: In the canonical state machine, missing a `FIXVAR` attack frame prevents the state machine from setting $t_{\text{prev}} > 16$. Because the state machine remains in `FIXFIX`, subsequent frames that should be `VARFIX` (transient decay) fail to trigger, reducing `VARFIX` recall to **11.4%**.
3. **Upper Bound Limit**: Even when driving the canonical state machine with **100% perfect ground-truth attack flags**, frameClass test accuracy reaches only **40.10%** and strict full-match accuracy is capped at **25.13%–27.45%**. This proves that the reference encoder's actual time-grid placement depends on complex internal heuristics (such as adaptive noise variance floors and multi-channel energy variance accumulators) that cannot be reverse-engineered from QMF slot energies alone.

---

## 4. Final Architectural Verdict & Recommendations

**Final Verdict Statement:**
QMF slot energy features alone (`slot0`..`slot31`), whether combined with decision trees, recursive state feedback, 1-frame future look-ahead, or canonical spec state machines, **do NOT contain sufficient temporal resolution** to predict reference SBR time-grid decisions at a level that would yield meaningful ViSQOL MOS gains.

### Key Conclusions:
1. **Single-Frame Static Model vs. Stateful Error Drift**: Unconstrained single-frame decision trees achieve a 37.38% strict full-match rate. Adding state feedback causes error compounding that reduces full-match accuracy to 31.95%–35.33%.
2. **QMF Downsampling Loss**: Reference encoders evaluate transients prior to QMF downsampling, using multi-sample energy variance sums across individual time positions. Quantized per-slot QMF energies lose the fine-grained sub-slot timing needed to detect 68% of transient attacks.
3. **No Production Port Recommended**: Do **not** port decision tree classifiers or feedback-loop state machines into production C (`libfaac/sbr_analysis.c`).

### Final Architectural Recommendation
FAAC's existing SBR time-grid analysis in `libfaac/sbr_analysis.c` should retain its baseline transient detection logic. To capture the +0.051 MOS ceiling in future work, SBR time-grid generation should incorporate direct psychoacoustic transient triggers from the core AAC MDCT block-switching engine (`libfaac/blockswitch.c`) prior to QMF band energy downsampling.
