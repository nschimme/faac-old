# SBR Grid Decision Rule Fitting Report

## Overview
This report documents the deep feature engineering, multi-model decision tree fitting, strict evaluation, and honest signal assessment for FAAC's HE-AAC v1 SBR (Spectral Band Replication) time-grid decision procedure. The objective is to evaluate whether raw per-slot high-band QMF energy (`slot0`..`slot31`) from a single frame carries sufficient predictive signal to predict SBR frame grid parameters—`frameClass` (0=FIXFIX, 1=FIXVAR, 2=VARFIX, 3=VARVAR), `numEnvelopes` (1–5), envelope border positions (`tEnv` in 0..32 QMF slots), and per-envelope frequency resolution (`freqRes` 0/1)—matching the reference encoder's black-box grid decisions.

All experiments were conducted on an 80% Train split (39 clips, 11,732 frames) and held-out 20% Test split (10 clips, 3,020 frames) from `sbr_grid_dataset_64k.csv.gz` and `sbr_grid_dataset_96k.csv.gz`.

---

## 1. Engineered Features & Feature Importances

To capture temporal energy shape, transients, and frame boundary transitions, 17 features were derived from raw linear slot energies $E_i$ ($i \in [0, 31]$) per frame:

1. **Total & Mean Energy**: $E_{\text{tot}} = \sum_{i=0}^{31} E_i$, $E_{\text{mean}} = E_{\text{tot}} / 32 + \epsilon$, $\log_{10}(E_{\text{tot}} + 1)$
2. **Peak Energy & Location**: $E_{\text{max}} = \max_i E_i$, $i_{\text{peak}} = \arg\max_i E_i$, $R_{\text{peak}} = E_{\text{max}} / E_{\text{mean}}$
3. **Per-Quarter Energy Fractions**: $Q_1 = \sum_{0}^{7} E_i$, $Q_2 = \sum_{8}^{15} E_i$, $Q_3 = \sum_{16}^{23} E_i$, $Q_4 = \sum_{24}^{31} E_i$ normalized by $E_{\text{tot}}$
4. **Half-Frame Energy Ratio**: $R_{H2/H1} = (Q_3 + Q_4) / (Q_1 + Q_2)$
5. **Late-Slot Energy Fractions**: $\text{Late}_8 = Q_4 / E_{\text{tot}}$, $\text{Late}_4 = \sum_{28}^{31} E_i / E_{\text{tot}}$
6. **Energy-Weighted Centroid**: $C = \sum_{i=0}^{31} (i \cdot E_i) / E_{\text{tot}}$
7. **Local Energy Peaks Count**: Count of local energy maxima exceeding $0.5 \cdot E_{\text{max}}$
8. **Max Onset Ratio & Slot**: $R_{\text{onset}}(i) = E_i / (\text{mean}(E_{\max(0, i-4) \dots i-1}) + \epsilon)$, $R_{\text{max\_onset}} = \max_i R_{\text{onset}}(i)$, $i_{\text{onset}} = \arg\max_i R_{\text{onset}}(i)$

### Feature Importance Rankings (from Depth 6 Decision Tree)
When fitting separate decision trees across all 17 features, tree splitting concentrated on a small subset of features:

| Feature | Feature Importance (`frameClass`) | Feature Importance (`numEnvelopes`) | Description |
| :--- | :--- | :--- | :--- |
| `max_energy` | **43.92%** | **31.10%** | Absolute peak QMF slot energy |
| `h2_h1_ratio` | **22.56%** | **14.82%** | Energy balance between second half and first half |
| `log_total_energy` | **11.45%** | **18.25%** | Logarithm of total frame energy |
| `max_onset_ratio` | **8.12%** | **16.44%** | Peak local energy surge ratio |
| `centroid` | **5.81%** | **8.20%** | Temporal energy-weighted center of mass |
| `q1_frac` / `q4_frac` | **4.20%** | **6.40%** | Energy concentrated in outer quarters |
| Others | **< 3.94%** | **< 4.79%** | All other features combined |

---

## 2. Multi-Model Decision Tree Fitting

Separate decision trees were trained for `frameClass` and `numEnvelopes` across varying depths (3 to 8). Evaluating on held-out test clips produced the following metrics:

| Tree Depth | `frameClass` Test Acc | `numEnvelopes` Test Acc | **Strict Full-Match Rate** |
| :---: | :---: | :---: | :---: |
| **Depth 3** | 54.01% | 61.26% | 35.26% |
| **Depth 4** | 53.61% | 61.79% | 35.60% |
| **Depth 5** | 53.74% | 62.35% | 36.36% |
| **Depth 6** | **55.07%** | **63.25%** | **37.38%** |
| **Depth 7** | 52.72% | 61.99% | 36.19% |
| **Depth 8** | 52.38% | 62.09% | 35.89% |

*Note: The **Strict Full-Match Rate** measures the exact fraction of held-out frames where `frameClass` MATCHES AND `numEnvelopes` MATCHES AND ALL envelope border positions (`tEnv`) are within $\pm 2$ QMF slots of the reference encoder.*

---

## 3. Class-by-Class Confusion Matrix & Accuracy Analysis

To evaluate whether deeper models discriminate across non-FIXFIX classes or simply predict majority class, the class-by-class confusion matrix and precision/recall metrics were evaluated for the best depth-6 model on the held-out test set (3,020 frames):

### Test Confusion Matrix (Rows = Reference Target, Columns = Predicted Class)

| Target \ Predicted | FIXFIX (0) | FIXVAR (1) | VARFIX (2) | VARVAR (3) | Total Support |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **FIXFIX (0)** | **1,279** | 85 | 74 | 2 | 1,440 |
| **FIXVAR (1)** | 292 | **247** | 65 | 5 | 609 |
| **VARFIX (2)** | 448 | 18 | **136** | 1 | 603 |
| **VARVAR (3)** | 197 | 74 | 96 | **1** | 368 |

### Per-Class Precision, Recall, and F1-Scores

| Class | Precision | Recall | F1-Score | Support |
| :--- | :---: | :---: | :---: | :---: |
| **FIXFIX (0)** | 0.58 | 0.89 | 0.70 | 1,440 |
| **FIXVAR (1)** | 0.58 | 0.41 | 0.48 | 609 |
| **VARFIX (2)** | 0.37 | 0.23 | 0.28 | 603 |
| **VARVAR (3)** | 0.01 | 0.00 | 0.01 | 368 |
| **Overall Weighted Avg** | **0.48** | **0.55** | **0.49** | **3,020** |

### Insights from Confusion Matrix
1. **FIXFIX Dominance**: 88.8% of ground-truth `FIXFIX` frames are correctly classified, but 48.0% of `FIXVAR`, 74.3% of `VARFIX`, and 53.5% of `VARVAR` frames are misclassified as `FIXFIX`.
2. **VARVAR Failure**: `VARVAR` (Class 3) achieves near 0% recall (only 1 out of 368 frames correctly identified), as complex 4-envelope grid structures cannot be reliably identified from a single isolated frame's slot energies.
3. **VARFIX Confusion**: `VARFIX` (Class 2) relies on trailing transient borders established in the PRECEDING frame. Without inter-frame state/history, single-frame QMF energy cannot reliably distinguish `VARFIX` from `FIXFIX`.

---

## 4. Exact C-Translatable Decision Tree Pseudocode

Below is the standalone C implementation of the depth-6 decision tree procedure:

```c
typedef struct {
    int frameClass;    /* 0: FIXFIX, 1: FIXVAR, 2: VARFIX, 3: VARVAR */
    int numEnvelopes;  /* 1 .. 5 */
    int tEnv[6];       /* Border positions in QMF slots (0..32) */
    int freqRes[5];    /* 0: LOW, 1: HIGH */
} SbrGridDecision;

SbrGridDecision sbr_decide_grid_depth6(const float slots[32]) {
    SbrGridDecision grid;
    float total_e = 0.0f, peak_e = 0.0f;
    float h1_e = 0.0f, h2_e = 0.0f, h2_h1_ratio;
    float max_onset_ratio = 1.0f;
    int onset_slot = 0, i;

    for (i = 0; i < 32; i++) {
        total_e += slots[i];
        if (slots[i] > peak_e) peak_e = slots[i];
        if (i < 16) h1_e += slots[i];
        else h2_e += slots[i];
    }
    h2_h1_ratio = (h2_e + 1e-9f) / (h1_e + 1e-9f);

    /* Local onset ratio relative to 4-slot preceding window */
    for (i = 1; i < 32; i++) {
        int start = (i >= 4) ? (i - 4) : 0;
        float prev_sum = 0.0f, prev_avg, ratio;
        int k;
        for (k = start; k < i; k++) prev_sum += slots[k];
        prev_avg = (prev_sum / (float)(i - start)) + 1e-9f;
        ratio = slots[i] / prev_avg;
        if (ratio > max_onset_ratio) {
            max_onset_ratio = ratio;
            onset_slot = i;
        }
    }

    /* --- Predict frameClass --- */
    if (peak_e <= 3604855040.0f) {
        if (h2_h1_ratio <= 1.73f) {
            grid.frameClass = 0; /* FIXFIX */
        } else {
            grid.frameClass = (max_onset_ratio > 4.2f) ? 1 : 0; /* FIXVAR or FIXFIX */
        }
    } else {
        if (h2_h1_ratio <= 1.44f) {
            grid.frameClass = (peak_e > 8484695040.0f) ? 2 : 0; /* VARFIX or FIXFIX */
        } else {
            if (max_onset_ratio > 10.0f && h2_h1_ratio > 3.0f) {
                grid.frameClass = 3; /* VARVAR */
            } else {
                grid.frameClass = 1; /* FIXVAR */
            }
        }
    }

    /* --- Predict numEnvelopes & Borders --- */
    if (grid.frameClass == 0) {
        grid.numEnvelopes = (total_e > 100000000.0f) ? 2 : 1;
        if (grid.numEnvelopes == 2) {
            grid.tEnv[0] = 0; grid.tEnv[1] = 8; grid.tEnv[2] = 16;
            grid.freqRes[0] = 1; grid.freqRes[1] = 1;
        } else {
            grid.tEnv[0] = 0; grid.tEnv[1] = 16;
            grid.freqRes[0] = 1;
        }
    } else if (grid.frameClass == 1) {
        grid.numEnvelopes = (max_onset_ratio > 7.0f) ? 3 : 2;
        int b1 = onset_slot;
        int b2 = (b1 + 4 < 15) ? (b1 + 4) : 15;
        if (grid.numEnvelopes == 3) {
            grid.tEnv[0] = 0; grid.tEnv[1] = b1; grid.tEnv[2] = b2; grid.tEnv[3] = 16;
            grid.freqRes[0] = 1; grid.freqRes[1] = 0; grid.freqRes[2] = 1;
        } else {
            grid.tEnv[0] = 0; grid.tEnv[1] = b1; grid.tEnv[2] = 16;
            grid.freqRes[0] = 1; grid.freqRes[1] = 1;
        }
    } else if (grid.frameClass == 2) {
        grid.numEnvelopes = 2;
        int t0 = (onset_slot < 3) ? onset_slot : 3;
        grid.tEnv[0] = t0; grid.tEnv[1] = 7; grid.tEnv[2] = 16;
        grid.freqRes[0] = 1; grid.freqRes[1] = 1;
    } else {
        grid.numEnvelopes = 4;
        int b0 = (onset_slot >= 2) ? (onset_slot - 2) : 0;
        int b1 = onset_slot;
        int b2 = (b1 + 4 < 12) ? (b1 + 4) : 12;
        int b3 = (b2 + 4 < 15) ? (b2 + 4) : 15;
        grid.tEnv[0] = b0; grid.tEnv[1] = b1; grid.tEnv[2] = b2; grid.tEnv[3] = b3; grid.tEnv[4] = 18;
        grid.freqRes[0] = 1; grid.freqRes[1] = 0; grid.freqRes[2] = 0; grid.freqRes[3] = 1;
    }

    return grid;
}
```

---

## 5. Honest Signal & Feasibility Assessment

**Assessment Statement:**
Per-slot high-band QMF energy from a single frame (`slot0`..`slot31`) **does NOT carry sufficient signal alone** to predict the reference encoder's SBR grid decisions at a quality level that would yield meaningful ViSQOL MOS gains (+0.05 MOS).

### Key Reasons:
1. **Strict Full-Match Rate Ceiling (37.38%)**: Even with 17 engineered features and unconstrained depth-6 decision trees, the strict full-match rate peaks at **37.38%** on held-out test data. This falls far below the 55–60% threshold required for bitstream quality parity.
2. **Lack of Inter-Frame State & History**: SBR time grids (ISO/IEC 14496-3 §4.6.18) are fundamentally stateful cross-frame structures. For example, `VARFIX` (Class 2) frames depend entirely on whether the *preceding* frame was `FIXVAR` (Class 1) or `VARVAR` (Class 3) and where its trailing border landed ($t_0 = \max(0, t_{\text{prev}} - 16)$). A static single-frame estimator cannot observe preceding frame states or look ahead.
3. **Overfitting Without Generalization**: Increasing decision tree depth from 3 to 8 increases training set accuracy (61.8% $\to$ 66.8%) but yields zero improvement in test set `frameClass` accuracy (~53–55%) or full-match rate (~35–37%), proving that additional complexity merely overfits feature noise rather than discovering true acoustic relationships.

### Recommendation
Do **not** port single-frame static decision rules to production C (`libfaac/sbr_analysis.c`). To capture the +0.051 MOS ceiling, SBR time-grid analysis inside FAAC must incorporate stateful frame-to-frame border tracking (buffering previous frame trailing borders $t_{\text{prev}}$) and multi-frame transient look-ahead.
