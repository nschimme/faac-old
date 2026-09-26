# SBR Grid Decision Rule Fitting, Cross-Frame Ceiling & Recursive Simulation Report

## Executive Summary
This report presents the complete data-fitting, feature engineering, multi-model decision tree analysis, cross-frame ceiling experiment, and **recursive self-consistent simulation** for FAAC's HE-AAC v1 SBR (Spectral Band Replication) time-grid decision procedure.

The goal is to determine SBR frame time-grid parameters—`frameClass` (0=FIXFIX, 1=FIXVAR, 2=VARFIX, 3=VARVAR), `numEnvelopes` (1–5), envelope border positions (`tEnv` in 0..32 QMF slots), and per-envelope frequency resolution (`freqRes` 0/1)—matching the reference encoder's clean-room black-box output decisions.

Experiments were evaluated on an 80% Train split (39 clips, 11,732 frames) and held-out 20% Test split (10 clips, 3,020 frames) across three distinct model configurations:
1. **v1 (Single-Frame Static Model)**: Uses 17 current-frame engineered QMF slot energy features.
2. **v2 Ground-Truth Ceiling**: Uses 17 current-frame slot features + ground-truth reference previous-frame state (`prev_ref_*` columns in v2 dataset).
3. **v2 Real Recursive Simulation**: Operates frame-by-frame per (clip, channel) sequence. For frame 0, previous state = sentinel `-1`. For all subsequent frames $N > 0$, the model's **own previous-frame prediction** ($\hat{y}_{N-1}$) is fed as the prior-state input to predict frame $N$.

---

## 1. Comparative Evaluation Results Across Models

| Evaluation Metric | v1 Single-Frame Static | v2 Ground-Truth Ceiling | **v2 Real Recursive Simulation** |
| :--- | :---: | :---: | :---: |
| **`frameClass` Test Accuracy** | 55.07% | 73.44% – 73.81% | **44.50% – 44.67%** (-28.9pp vs ceiling) |
| **`numEnvelopes` Test Accuracy** | 63.25% | 67.72% – 68.34% | **58.61% – 61.39%** (-6.9pp vs ceiling) |
| **`FIXFIX` (Class 0) Recall** | 88.8% | 95.8% | **85.0% – 86.0%** |
| **`FIXVAR` (Class 1) Recall** | 40.6% | 35.8% | **12.0% – 13.0%** |
| **`VARFIX` (Class 2) Recall** | 22.5% | **85.7%** | **5.0% – 6.0%** (-79.7pp collapse) |
| **`VARVAR` (Class 3) Recall** | 0.3% | **28.0%** | **1.0% – 3.0%** (-25.0pp collapse) |
| **Strict Full-Match Rate** | 37.38% | **51.42% – 51.99%** | **32.78% – 35.33%** (-16.7pp vs ceiling) |

*Note: **Strict Full-Match Rate** requires exact `frameClass` match AND `numEnvelopes` match AND all envelope border positions (`tEnv`) within $\pm 2$ QMF slots of reference.*

---

## 2. Confusion Matrix & Error Compounding Analysis

The confusion matrix for the **v2 Real Recursive Simulation** on the held-out test set (3,020 frames) demonstrates severe performance degradation when relying on predicted prior state:

### Recursive Simulation Test Confusion Matrix (Rows = Reference Target, Columns = Predicted)

| Target \ Predicted | FIXFIX (0) | FIXVAR (1) | VARFIX (2) | VARVAR (3) | Recall |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **FIXFIX (0)** | **1,238** | 130 | 60 | 12 | **86.0%** |
| **FIXVAR (1)** | 473 | **77** | 55 | 4 | **12.6%** |
| **VARFIX (2)** | 485 | 78 | **29** | 11 | **4.8%** |
| **VARVAR (3)** | 273 | 59 | 31 | **5** | **1.4%** |

### Per-Class Precision & Recall Breakdown (Recursive)

| Class | Precision | Recall | F1-Score | Support |
| :--- | :---: | :---: | :---: | :---: |
| **FIXFIX (0)** | 0.50 | 0.86 | 0.63 | 1,440 |
| **FIXVAR (1)** | 0.22 | 0.13 | 0.16 | 609 |
| **VARFIX (2)** | 0.17 | 0.05 | 0.07 | 603 |
| **VARVAR (3)** | 0.16 | 0.01 | 0.03 | 368 |
| **Weighted Overall Avg** | **0.34** | **0.45** | **0.35** | **3,020** |

---

## 3. Deep Analysis of Error Compounding & Drift

### Why Did `VARFIX` Recall Collapse from 85.7% to 4.8%?
1. **The Dependency Chain**: In ISO/IEC 14496-3 SBR, a `VARFIX` (Class 2) frame occurs when the *immediately preceding* frame was `FIXVAR` (Class 1) or `VARVAR` (Class 3) with a trailing border ($t_{\text{prev}} > 16$).
2. **First-Link Failure**: In the recursive simulation, if the model misses a `FIXVAR` attack frame (predicting `FIXFIX` instead), the state machine enters the next frame believing the prior frame was `FIXFIX` ($t_{\text{prev}} = 16$).
3. **Compounding Cascades**: Because `FIXVAR` recall is only ~12-13% during recursive simulation, the state machine fails to pass the $t_{\text{prev}} > 16$ signal to the following frame. Consequently, 80.4% of true `VARFIX` frames (485 out of 603) and 74.2% of true `VARVAR` frames (273 out of 368) default to `FIXFIX`.

### Clip-Level Case Study: `velvet.16b48k.wav` (FrameClass Acc: 10.51%)
Inspecting complex, highly dynamic transient audio clips (such as `velvet.16b48k.wav` or `Through The Fire And Flames.16b48k.wav`) shows that once a transient frame prediction is missed, the model falls into a `FIXFIX` default feedback loop:
- **Frame $N-1$**: Ground-truth target is `FIXVAR` (4 envelopes), but model predicts `FIXFIX` (1 envelope, $t_{\text{last}} = 16$).
- **Frame $N$**: Ground-truth target is `VARFIX`. Model evaluates input features with $t_{\text{prev}} = 16$. The decision tree branches down the $t_{\text{prev}} \le 16$ path and predicts `FIXFIX` instead of `VARFIX`.
- **Outcome**: The entire multi-frame transient sequence is collapsed into `FIXFIX` frames, erasing SBR temporal resolution precisely where transients occur.

---

## 4. Final Verdict & Feasibility Assessment

**Final Verdict Statement:**
Error compounding **erases the v2 ceiling gains**, causing the real stateful full-match rate (32.78% – 35.33%) to fall **below** even the simple single-frame v1 baseline (37.38%) and far below the majority-class baseline (47.68%).

### Summary Findings:
1. **Ceiling Illusion vs. Real Performance**: While feeding ground-truth reference history yields an impressive 73.81% accuracy ceiling, real encoders must consume their own prior output. In practice, a single missed transient prediction breaks the state chain and drops `VARFIX` recall from 85.7% down to **4.8%**.
2. **Insufficient Single-Frame Signal**: The root cause of the chain reaction is that single-frame QMF slot energy lacks the fidelity to classify `FIXVAR` attack frames cleanly (achieving only 12–13% recursive recall).
3. **No Production Port Recommended**: Do **not** port this decision tree model or state machine to production C (`libfaac/sbr_analysis.c`).

### Architectural Recommendation
To capture the +0.051 MOS ceiling without error compounding, FAAC's SBR grid generator requires **multi-frame transient look-ahead** (analyzing future QMF slots in a sliding look-ahead buffer) rather than relying on feedback loops from prior unconstrained classifier predictions.
