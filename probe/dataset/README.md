# SBR grid decision training data (probe only, never merge)

Labeled dataset for fitting FAAC's HE-AAC v1 SBR per-frame time-grid decision.
Each row is one (clip, rate, channel, frame): FAAC's own pre-decision signal,
paired with the reference encoder's actual grid choice for that same frame
(via a clean-room black-box decode of its output, never its source).

## Why this exists

A hand-tuned heuristic (measured thresholds, built by manual trial and error)
landed only +0.005 MOS vs the unmodified encoder, and regressed real ABR/VBR
bitrate-targeting encodes. Separately, a controlled probe proved that if the
grid *decision* alone matched the reference's choice (while FAAC still
estimates its own envelope energies), it is worth +0.051/+0.054 MOS at 64/96
kbps (49 clips, 32-33 wins / 3-4 losses each) — a much bigger, cleanly
positive number. That gap between +0.005 and +0.05 is why we need a
better-than-guessed decision rule, fit from real examples of what the
reference actually chose.

## Files

- `sbr_grid_dataset_v2_64k.csv.gz`, `sbr_grid_dataset_v2_96k.csv.gz`: same rows as
  the v1 files below, PLUS four cross-frame columns: `prev_ref_frameClass`,
  `prev_ref_numEnvelopes`, `prev_ref_tEnv0`, `prev_ref_tEnv_last` (the previous
  frame's own last envelope border, i.e. `ref_tEnv[prev_numEnvelopes]`) for the
  same (clip, channel), sorted by frame. Sentinel `-1` for the first frame of
  each (clip, channel) pair (no previous frame). These are ground-truth
  *reference* history (not FAAC's own encoder state), added because round-1
  fitting (see FIT_RESULT.md) found the missing piece is cross-frame state:
  SBR grid classes like VARFIX depend on whether the *preceding* frame left a
  trailing border, which no single-frame feature can see. Use these v2 files
  for feature engineering that includes prior-frame context; this doesn't
  change the join/alignment, only adds columns.
- `sbr_grid_dataset_64k.csv.gz`, `sbr_grid_dataset_96k.csv.gz`: 14,752 rows
  each (49 clips, both stereo channels, ~150 frames/clip). Columns:
  - `clip`, `rate`, `channel`, `frame`: identifiers.
  - `slot0`..`slot31`: FAAC's own per-QMF-slot high-band energy for this
    frame/channel (32 slots per frame; raw linear energy, not dB). This is
    the RAW signal available before any grid decision is made — richer than
    the two derived scalars (`faac_transientSlot`, `faac_transientStrength`)
    the existing heuristic uses, which are too lossy to do much better.
  - `faac_transientSlot`, `faac_transientStrength`: the existing heuristic's
    two derived features (argmax slot, peak/mean ratio), included for
    reference/comparison, not because they're sufficient.
  - `ref_frameClass`: the reference's SBR frame class for this frame (0-3;
    see ISO/IEC 14496-3 §4.6.18 `bs_frame_class`: 0=FIXFIX, 1=FIXVAR,
    2=VARFIX, 3=VARVAR).
  - `ref_numEnvelopes`: 1-5.
  - `ref_bsPointer`: the envelope pointer field (spec §4.6.18).
  - `ref_tEnv0`..`ref_tEnv5`: envelope border positions in QMF slots
    (0..32 depending on rate), padded with -1 past `ref_numEnvelopes`.
  - `ref_freqRes0`..`ref_freqRes4`: per-envelope frequency resolution
    (0=low, 1=high), padded with -1 past `ref_numEnvelopes`.

## Known limitations (be aware of these when fitting)

- Frame alignment between FAAC's analysis order and the reference's frame
  index required an empirically-determined offset (+3), independently
  verified two ways (energy-contour correlation peak, and a categorical
  check: does FAAC's top-decile transient-strength frame land on a
  non-FIXFIX reference frame). Join success rate 99.4%; the small remainder
  is trailing frames past the reference's last coded frame (expected, not a
  bug). Treat the labels as good but not perfect — a small fraction of rows
  may carry a slightly misaligned label.
- `slot0`..`slot31` reflect a single analysis pass; there is no look-ahead
  beyond what FAAC's own encoder already buffers. Real-time causality is
  preserved (the fitted rule must remain a plain per-frame/per-channel
  decision usable inside the actual encoder, not an offline/global optimum).
- A loose sanity check (does high `faac_transientStrength` correlate with
  non-FIXFIX reference frames) was inconsistent per-clip — expected, since
  that scalar is known to be weak. The per-slot profile should carry
  meaningfully more signal.

## What this is for

Fit an interpretable decision procedure — measured thresholds or a small,
explainable rule set, not an opaque fitted floating-point table — that
predicts `ref_frameClass` / `ref_numEnvelopes` / border positions / freq_res
from `slot0`..`slot31` (and derived features you compute from them, e.g.
peak position, peak/mean ratio, energy rise time, per-half-frame energy
split). The output should be a description of the rule (thresholds and the
logic) precise enough to translate directly into plain C, matching FAAC's
existing style (no compiler attributes, no fitted floating-point tables, no
hand-unrolling). This is a probe/data artifact only — nothing here is meant
to be merged into the library as-is.
