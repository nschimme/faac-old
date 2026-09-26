# SBR Grid Decision Rule Fitting, Cross-Frame Ceiling & Recursive Simulation Report

## Executive Summary
This report presents the complete data-fitting, feature engineering, multi-model decision tree analysis, cross-frame ceiling experiment, recursive self-consistent simulation, and **1-frame future QMF slot energy look-ahead experiment** for FAAC's HE-AAC v1 SBR (Spectral Band Replication) time-grid decision procedure.

The goal is to determine SBR frame time-grid parameters—`frameClass` (0=FIXFIX, 1=FIXVAR, 2=VARFIX, 3=VARVAR), `numEnvelopes` (1–5), envelope border positions (`tEnv` in 0..32 QMF slots), and per-envelope frequency resolution (`freqRes` 0/1)—matching the reference encoder's clean-room black-box output decisions.

Experiments were evaluated on an 80% Train split (39 clips, 11,732 frames) and held-out 20% Test split (10 clips, 3,020 frames) across four distinct model configurations:
1. **v1 (Single-Frame Static Model)**: Uses 17 current-frame engineered QMF slot energy features.
2. **v2 Ground-Truth Ceiling**: Uses 17 current-frame slot features + ground-truth reference previous-frame state (`prev_ref_*` columns in v2 dataset).
3. **v2 Real Recursive Simulation**: Operates frame-by-frame per (clip, channel) sequence. For frame 0, previous state = sentinel `-1`. For all subsequent frames $N > 0$, the model's **own previous-frame prediction** ($\hat{y}_{N-1}$) is fed as the prior-state input to predict frame $N$.
4. **v3 Real Recursive Simulation + 1-Frame Future QMF Look-Ahead**: Combines recursive self-consistent prior state prediction ($\hat{y}_{N-1}$) with 1-frame future QMF slot energy look-ahead features (`next_max_energy`, `next_total_energy`, `next_max_onset_ratio`, `next_h2_h1_ratio`, `next_q1_frac`, `next_centroid`, `next_energy_ratio`).

---

## 1. Comparative Evaluation Results Across All Models

| Evaluation Metric | v1 Single-Frame Static | v2 Ground-Truth Ceiling | v2 Real Recursive Simulation | **v3 Real Recursive + 1-Frame QMF Look-Ahead** |
| :--- | :---: | :---: | :---: | :---: |
| **`frameClass` Test Accuracy** | 55.07% | 73.44% – 73.81% | 44.50% – 44.67% | **43.64% – 44.87%** (-28.6pp vs ceiling) |
| **`numEnvelopes` Test Accuracy** | 63.25% | 67.72% – 68.34% | 58.61% – 61.39% | **58.91% – 60.93%** |
| **`FIXFIX` (Class 0) Recall** | 88.8% | 95.8% | 85.0% – 86.0% | **84.0% – 88.0%** |
| **`FIXVAR` (Class 1) Recall** | 40.6% | 35.8% | 12.0% – 13.0% | **13.0% – 14.0%** |
| **`VARFIX` (Class 2) Recall** | 22.5% | **85.7%** | 5.0% – 6.0% | **1.0% – 4.0%** (continued collapse) |
| **`VARVAR` (Class 3) Recall** | 0.3% | **28.0%** | 1.0% – 3.0% | **0.0% – 1.0%** (continued collapse) |
| **Strict Full-Match Rate** | 37.38% | **51.42% – 51.99%** | 32.78% – 35.33% | **31.95% – 33.81%** (-18.1pp vs ceiling) |

*Note: **Strict Full-Match Rate** requires exact `frameClass` match AND `numEnvelopes` match AND all envelope border positions (`tEnv`) within $\pm 2$ QMF slots of reference.*

---

## 2. Feature Importances: QMF Look-Ahead vs Prior State

When training decision trees on the 28-feature representation (including 1-frame future QMF slot energy look-ahead features), feature importance remains overwhelmingly dominated by prior state:

| Feature Name | Feature Importance (`frameClass`) | Role / Category |
| :--- | :---: | :--- |
| **`prev_ref_numEnvelopes`** | **51.56%** | Prior State: Number of envelopes in previous frame |
| **`centroid`** | **12.80%** | Current Frame: Energy-weighted temporal center of mass |
| **`prev_ref_tEnv_last`** | **11.75%** | Prior State: Trailing border position $t_{\text{prev}}$ |
| **`max_energy`** | **8.91%** | Current Frame: Peak QMF slot energy |
| **`prev_ref_frameClass`** | **8.15%** | Prior State: Previous frame class |
| **`next_max_energy` / `next_total_energy`** | **1.84%** | Future Look-Ahead: 1-frame future peak and total energy |
| **`next_max_onset_ratio` / `next_energy_ratio`** | **1.39%** | Future Look-Ahead: 1-frame future onset surge & ratio |
| **All Other Current & Future Features** | **< 3.60%** | All remaining current and future features combined |

---

## 3. Confusion Matrix: v3 Recursive Simulation + 1-Frame Look-Ahead

The test confusion matrix (3,020 frames) for the depth-6 decision tree incorporating 1-frame future QMF slot energy look-ahead during recursive self-consistent simulation:

### v3 Recursive + 1-Frame QMF Look-Ahead Test Confusion Matrix (Rows = Reference Target, Columns = Predicted)

| Target \ Predicted | FIXFIX (0) | FIXVAR (1) | VARFIX (2) | VARVAR (3) | Recall |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **FIXFIX (0)** | **1,261** | 137 | 35 | 7 | **87.6%** |
| **FIXVAR (1)** | 490 | **84** | 28 | 7 | **13.8%** |
| **VARFIX (2)** | 508 | 83 | **9** | 3 | **1.5%** |
| **VARVAR (3)** | 292 | 65 | 10 | **1** | **0.3%** |

### Insights from QMF Look-Ahead Experiment:
1. **QMF Look-Ahead Does Not Solve Error Cascades**: Buffering 1 future frame's actual QMF slot energy (`next_max_energy`, `next_total_energy`, `next_max_onset_ratio`) improves `FIXVAR` attack detection only marginally (+1.2pp recall, from 12.6% $\to$ 13.8%).
2. **First-Link Dependency Remains Broken**: Because `FIXVAR` attack detection remains unreliable from QMF slot energies alone (~13.8% recall), the state machine fails to register $t_{\text{prev}} > 16$ into the following frame. As a result, `VARFIX` recall drops further to **1.5%** (9 out of 603 frames).
3. **Strict Full-Match Rate Decreases**: The strict full-match rate under recursive simulation with 1-frame QMF look-ahead is **31.95% – 33.81%**, which remains below the v1 single-frame static baseline (**37.38%**) and far below the v2 ground-truth ceiling (**51.99%**).

---

## 4. Final Architectural Verdict & Recommendations

**Final Verdict Statement:**
Neither single-frame static features (v1), self-consistent prior state feedback (v2-recursive), nor 1-frame future QMF slot energy look-ahead (v3) provide a viable path to AAC time-grid decision parity from QMF slot energies alone.

### Summary Findings:
1. **The State Paradox**: SBR time grids (ISO/IEC 14496-3 §4.6.18) require previous frame trailing border state $t_{\text{prev}}$ to correctly code `VARFIX` and `VARVAR` frames. However, when the classifier's own prior predictions are fed back statefully, a single misclassified attack frame breaks the state chain and collapses `VARFIX` recall from **85.7% (ground-truth ceiling) down to 1.5% – 4.8%**.
2. **QMF Energy Signal Limits**: Real audio QMF slot energy look-ahead (`next_max_energy`, `next_max_onset_ratio`) does not provide sufficient discrimination to prevent `FIXVAR` classification errors, failing to stop error cascades.
3. **No Production Port Recommended**: Do **not** port decision tree classifiers or feedback-loop state machines to production C (`libfaac/sbr_analysis.c`).

### Final Architectural Recommendation
FAAC's existing SBR time-grid analysis in `libfaac/sbr_analysis.c` should retain its baseline transient detection logic rather than adopting single-frame or feedback-loop decision tree classifiers. Future SBR time-grid research should focus on direct time-domain psychoacoustic transient analysis integrated with the core AAC MDCT block-switching engine (`libfaac/blockswitch.c`) rather than relying on quantized QMF band slot energies alone.
