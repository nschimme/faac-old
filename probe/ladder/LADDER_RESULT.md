# LC 128k reference ladder: A, B, and C all PASS

Root-caused and fixed the cross-reference desync from the previous revision.
Both references now decode strictly clean (zero concealment, zero non-END
terminations) on all 5 clips. Stage C's MOS/line-level/MDCT-ratio tables are
below.

## The bug: huffbook() indexed its cost tables out of range

**Symptom** (from the previous revision): running step1 against real
Apple/fdk data (not FAAC's own dump) produced a badly desynced bitstream --
e.g. Severance/Apple decoded with 265/470 frames non-END-terminated, spurious
SCE/CCE/LFE elements, "channels varied 2-7". FAAC's own self-test (offset 0,
its own dump fed back through step1) showed zero such corruption on the same
clip, which is what pointed away from the transition-window and overflow-
fallback gaps (neither fires meaningfully on Severance) and toward something
specific to real cross-reference data.

**Method** (as directed): re-encoded Severance/Apple as raw ADTS, bisected
the output by ADTS frame count to find the first non-END-terminated frame
(frame 1, i.e. the very first output frame -- `ciFrame=0`), then dumped that
one frame's decoded ICS syntax with `FAAD_LADDER_DUMP=1` and diffed it,
element by element, against Apple's own reference dump for the same
(offset-corrected) frame and against what step1 had computed just before
writing (added temporary debug prints at each stage, removed once the bug was
found).

**What the diff showed**: channel 0's decoded output looked plausible on its
own, but channel 1's decoded `global_gain` came back as 169 -- a value that
appears nowhere in either channel's actual data for this or any nearby frame.
Since `global_gain` is the *first* field of a channel's ICS and both
channels share one `ics_info()` header (confirmed correctly read: window
sequence, `max_sfb`, grouping all matched Apple's own dump exactly), the only
explanation is that channel 0's own data consumed the *wrong number of bits*,
leaving channel 1's reader starting from an offset the writer never intended.

**Root cause**: in `Step1Quantize`, a regular band's `ci->book[band]` was set
directly to the *reference's* class (`cb`, e.g. `HCB_1`) for that band, then
passed to `huffbook()`. `huffbook()`'s Viterbi only ever *widens* from
whatever `book[]` arrives with (`lo = ((book-1)&~1)+1`, `hi = HCB_ESC`
always) -- it assumes the caller's preset book is already *at least*
sufficient for the actual values, which is true for a normal encode (where
`BlocQuant` sets `book[band]` from the real `maxq` it just computed) but is
**not** true here: FAAC's own MDCT, quantized at Apple's or fdk's absolute
scalefactor for that band, can produce a magnitude the reference's own
(differently-scaled) content never needed. When that happens, `size_books()`
computes an index into `book01[]`/`book03[]`/etc. (e.g. `40 + 27*q0 + 9*q1 +
3*q2 + q3` for `HCB_1`) using a `q` value outside that book's actual range --
an out-of-bounds table read, not a bounds-checked rejection. The garbage
"cost" it returns can beat the correct, larger book's real cost, so
`huffbook()` picks a book that cannot represent these values. The writer then
emits a codeword/escape sequence sized for the *wrong* book, consuming a
different bit count than the decoder (which trusts the transmitted book
index) expects to consume -- an actual bitstream desync, not a quality
difference. This is exactly the failure mode the self-test structurally
cannot exercise: FAAC's own quantized magnitudes always fit the class FAAC
itself just picked for them.

**Fix** (`libfaac/step1.c`, `Step1Quantize`): compute `maxq` from the actual
quantized values in the band (was already being tracked for the zero-
reclassification check) and derive `ci->book[band]` from *that*, exactly as
`BlocQuant`'s own `assign_band_codebooks` does (`maxq<=LAV_1?HCB_1:...`),
before calling `huffbook()`. `huffbook()` is still free to widen further for
section-merging efficiency; it just never starts from a family too small for
what's actually there. Verified: all 5 clips x both references now decode
with `non-END termination: 0`, `Error concealment: 0 frames`, and structural
element/TNS/short-block counts matching each reference's own diagnostics.

None of the coordinator's four hypotheses were the actual cause, but
checking them was what surfaced this one: hypothesis (1), max_sfb/sfbn
mismatch, was ruled out first (channel 1's shared `ics_info` decoded
correctly, including grouping) and that's what pointed at "something in
channel 0's own data has the wrong bit count" rather than a header-level
desync -- which is what led to inspecting `book[]`/`huffbook()` next.

## Stage A / B (unchanged, re-verified)

- **Control 0**: still bit-exact on Apple/Severance after every change this
  session (re-checked repeatedly; last check just now).
- **Offset control**: peak +1 (ciFrame-space), unchanged from the prior
  revision.
- **Control 1a (step 1's self-test)**: unchanged at ~9.4% of ICS records
  differing (88-89/940), all attributable to the already-documented,
  unfixed gap (`stereo.c`'s `apply_is` permanently modifying the *other*
  channel's spectrum, which step1 doesn't replicate). The huffbook fix above
  doesn't touch this path since FAAC's own self-test never exercises an
  under-sized preset book.

## Stage C: 5 clips x 2 references

Settings: FAAC ABR 112/128/144 kbps (`faac -b <rate>`) for the per-clip
ladder slope; Apple/fdk references as before; step1 at `FAAC_STEP1_OFFSET=1`.
MOS via `faac-benchmark/scripts/score_clip.py` (ViSQOL/zimtohrli backend,
ffmpeg decode path), each variant scored against the clip's own source WAV.
Bits-adjusted delta = `(MOS_x - MOS_faac128) - slope * log2(bytes_x /
bytes_faac128)`, `slope = (MOS_144 - MOS_112) / log2(bytes_144/bytes_112)`,
per clip -- the ladder-slope method from the archived `cmp3.py`.

| clip | ref | MOS(ref) | MOS(step1) | MOS(faac128) | raw ∆(ref) | raw ∆(step1) | adj ∆(ref) | adj ∆(step1) | bytes(ref) | bytes(step1) | bytes(faac128) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Severance | Apple | 4.9237 | 4.9000 | 4.8458 | +0.0779 | +0.0542 | **+0.0717** | **+0.0425** | 164154 | 166642 | 161441 |
| Severance | fdk | 4.8921 | 4.7656 | 4.8458 | +0.0463 | -0.0802 | +0.0403 | **-0.0874** | 164097 | 164631 | 161441 |
| 21-classic | Apple | 4.8969 | 4.8623 | 4.8129 | +0.0840 | +0.0494 | +0.0762 | **+0.0392** | 161500 | 163293 | 155941 |
| 21-classic | fdk | 4.8463 | 4.7285 | 4.8129 | +0.0334 | -0.0844 | +0.0298 | **-0.0875** | 158458 | 158131 | 155941 |
| velvet | Apple | 4.7807 | 4.5679 | 4.4807 | +0.3000 | +0.0872 | +0.2346 | **+0.0118** | 178939 | 181624 | 162457 |
| velvet | fdk | 4.6044 | 4.1968 | 4.4807 | +0.1237 | -0.2839 | +0.1166 | **-0.3268** | 164166 | 173105 | 162457 |
| Greensleeves | Apple | 4.9027 | 4.8225 | 4.8526 | +0.0501 | -0.0301 | +0.0357 | **-0.0556** | 146220 | 150260 | 141092 |
| Greensleeves | fdk | 4.8922 | 4.7315 | 4.8526 | +0.0396 | -0.1211 | +0.0330 | **-0.1370** | 143408 | 146735 | 141092 |
| German | Apple | 4.9245 | 4.8985 | 4.9228 | +0.0017 | -0.0243 | +0.0028 | **-0.0331** | 126356 | 130574 | 126799 |
| German | fdk | 4.8945 | 4.6954 | 4.9228 | -0.0283 | -0.2274 | -0.0332 | **-0.2398** | 128893 | 132167 | 126799 |
| **mean** | Apple | | | | | | +0.0842 | **-0.0110** | | | |
| **mean** | fdk | | | | | | +0.0373 | **-0.1757** | | | |

## Correction: the Stage C verdict below read the delta table backwards

Every delta in the Stage C table is vs FAAC-128. Apple's own lead is +0.084;
step1's is -0.011. That means step1 sits ~0.095 *below* Apple -- it **loses**
essentially all of Apple's lead, not "closes" it. Against fdk, step1 loses
~0.21 (-0.176 vs fdk's own +0.037). The first revision of this file had the
sign of that comparison backwards and concluded the opposite. Since step1
differs from a reference only in the spectrum (FAAC's own MDCT, after the
forced M/S/TNS) and the rounding, the loss sits in one of those two stages,
or in a remaining pipeline bug -- **not** in the rate loop, which step1
never uses either way. See the corrected Verdict at the end of this file.

## Correction: the line-level/MDCT-ratio tables were internally inconsistent

The previous revision's ratio computation used `abs(q)` on both sides before
computing the dequantized-magnitude ratio, discarding sign. An
opposite-sign, equal-magnitude line (`qs=-5, qr=5`) therefore registered as a
perfect `ratio=1.0` in the ratio table while the separately-computed,
sign-aware exact-match table correctly scored it as a large miss (`|(-5)-5|
= 10`). That's what produced the reported inconsistency: a median ratio of
exactly 1.000 with IQR `[1,1]` sitting next to a 7-33% exact-match rate,
which cannot both be true of the same data. Fixed in
`probe/ladder/line_level.py` (moved into the repo, as asked) by keeping sign
through the ratio (`math.copysign(|q|^(4/3)*scale, q)` on each side), and by
requiring `groups`/`window_group_length` to match between step1's output and
the reference before comparing a frame at all (they always did on the
clips checked, but the check is now explicit rather than assumed).

**Encoder delay, verified from each reference's own `iTunSMPB` tag** (not
taken on trust): `ffprobe -show_entries format_tags` on
`probe/ladder/ref/apple/Severance...m4a` gives `iTunSMPB = 00000000
00000840 000000C0 ...` -- field 2 is the encoder delay in samples, hex
`0x840 = 2112`. The same tag on the fdk reference gives `00000000 00000800
00000100 ...`, `0x800 = 2048`. Both match the coordinator's numbers exactly:
Apple's delay is `2*1024 + 64` (a 64-sample sub-frame remainder beyond a
whole 2-frame priming), fdk's is exactly `2*1024` (two whole frames). fdk is
therefore the clean case for a line-level comparison at single-sample MDCT
granularity; Apple's is inherently loosened by that 64-sample phase offset,
which is a real property of the two streams, not a bug in this pipeline.
**The corrected tables below are computed against fdk**, per the
coordinator's instruction to use it as the primary case for this check.

## Line-level match and MDCT-ratio tables (corrected, vs fdk)

Aggregate exact / off-by-1 / larger share, all nonzero (either side) lines,
all bands, whole clip:

| clip | total lines | exact | off by 1 | bigger |
|---|---:|---:|---:|---:|
| Severance | 254,121 | 62.7% | 28.9% | 8.4% |
| 21-classic | 249,641 | 64.9% | 26.9% | 8.2% |
| velvet | 210,569 | 78.9% | 17.8% | 3.4% |
| Greensleeves | 214,444 | 63.3% | 29.2% | 7.5% |
| German | 204,574 | 60.7% | 30.8% | 8.5% |

Per-region/bucket breakdown, Severance x fdk (all 5 clips show the same
shape -- monotonically worse exact-match as `|q|` grows, tight agreement
once split by magnitude, see below):

| region | ref \|q\| | total | exact | off by 1 | bigger |
|---|---|---:|---:|---:|---:|
| 0-2k | 0 | 12,541 | 64.4% | 29.5% | 6.2% |
| 0-2k | 1 | 17,860 | 52.9% | 33.3% | 13.8% |
| 0-2k | 2-4 | 24,430 | 43.7% | 34.5% | 21.8% |
| 0-2k | >4 | 25,841 | 33.1% | 35.0% | 31.9% |
| 2-6k | 0 | 71,122 | 88.4% | 11.3% | 0.3% |
| 2-6k | 1 | 58,668 | 80.4% | 17.8% | 1.8% |
| 2-6k | 2-4 | 23,785 | 70.2% | 23.9% | 5.9% |
| 2-6k | >4 | 5,881 | 53.6% | 34.1% | 12.3% |
| 6-12k | 0 | 183,380 | 97.0% | 3.0% | 0.0% |
| 6-12k | 1 | 47,095 | 86.7% | 12.8% | 0.5% |
| 6-12k | 2-4 | 8,248 | 77.5% | 18.9% | 3.6% |
| 6-12k | >4 | 1,405 | 57.0% | 32.7% | 10.2% |
| >12k | 0 | 132,016 | 96.7% | 3.1% | 0.2% |
| >12k | 1 | 16,641 | 87.9% | 11.7% | 0.3% |
| >12k | 2-4 | 1,239 | 71.3% | 26.9% | 1.9% |
| >12k | >4 | 376 | 57.2% | 41.0% | 1.9% |

Signed MDCT-magnitude ratio (median, IQR), nonzero-nonzero lines, all 5 clips
vs fdk:

| clip | 0-2k | 2-6k | 6-12k | >12k |
|---|---|---|---|---|
| Severance | 1.000 [0.715, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| 21-classic | 1.000 [0.725, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| velvet | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| Greensleeves | 1.000 [0.881, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |
| German | 1.000 [0.855, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] | 1.000 [1.000, 1.000] |

This is now internally consistent: at `|q|=0` and `|q|=1`, exact-match and
IQR-near-1 agree closely (88-97% exact, IQR essentially `[1,1]`); as `|q|`
climbs, exact-match falls to 33-57% and the ratio IQR's lower bound sags to
0.72-0.88 at low frequency -- a real, present rounding gap, not a
computation artifact, and the pre-quantization check below shows why it's
concentrated at low `|q|`.

## Pre-quantization spectrum check (fdk, Severance, as directed)

Added `FAAC_STEP1_SPEC_DUMP=<path>`, a new dump in `frame.c` right after the
forced window/M-S/TNS stages and before `Step1Quantize` runs, in the same
raw layout the `Q` record uses so it reuses `line_level.py`'s band
reconstruction (`probe/ladder/prequant_check.py`). Compared FAAC's real,
pre-quantization spectrum against fdk's *dequantized* value at fdk's own
scalefactor, for every line fdk actually coded (`qr != 0`):

| region | n | correlation | ratio median | ratio IQR | residual (dB rel. to ref energy) |
|---|---:|---:|---:|---|---:|
| 0-2k | 69,362 | 0.9884 | 0.920 | [0.608, 1.095] | -16.3 |
| 2-6k | 89,213 | 0.9819 | 0.894 | [0.651, 1.120] | -14.4 |
| 6-12k | 54,667 | 0.9689 | 0.817 | [0.620, 1.064] | -12.1 |
| >12k | 18,227 | 0.9540 | 0.723 | [0.582, 0.926] | -10.2 |

Read in isolation, that ratio-below-1 trend worsening with frequency could
look like a real scale or window mismatch. Split by the reference's own
`|q|` magnitude instead of frequency, it resolves cleanly:

| ref \|q\| bucket | n | ratio median | ratio IQR |
|---|---:|---:|---|
| small (1-2) | 173,782 | 0.815 | [0.588, 1.108] |
| mid (3-8) | 41,309 | 0.943 | [0.751, 1.074] |
| large (>8) | 16,378 | 0.997 | [0.882, 1.064] |

**Measured**: the ratio converges to 1.000 as `|q|` grows and only sags at
small `|q|`. This is the signature of AAC's own quantization coarseness at
small integers (a `|q|=1` bin covers a wide range of true continuous
magnitudes -- roughly 0.5 to 1.5 in normalized units -- so any two encoders'
independent roundings to `q=1` can differ by a large *relative* amount while
both are legitimately "close" to the true signal), not a systematic
per-frequency scale error. High frequencies simply have a larger share of
small-`|q|` lines (less energy there), which is why the frequency-region
table alone looked worse at >12kHz.

**Inference, labelled as such**: FAAC's spectrum, after the forced window/
M-S/TNS stages, matches the underlying signal fdk's own scale was chosen for
well enough that the residual is fully explained by inherent low-magnitude
AAC quantization coarseness, not a window-shape, TNS-direction, M/S, or
alignment bug. This clears the pipeline to attribute the MOS loss to
rounding rather than a spectral mismatch -- which the next check tests
directly.

## Rounding-variant sweep (fdk, all 5 clips)

Made `MAGIC_NUMBER` (FAAC's own rounding-offset constant, 0.4054, from
`quantize.h`) overridable via `FAAC_STEP1_MAGIC` in `step1.c` and swept
0.3 / 0.4054 (baseline) / 0.5 against fdk on all 5 clips. All three variants
still decode strictly clean (zero non-END terminations) on every clip.

| clip | MOS @0.3 | MOS @0.4054 | MOS @0.5 | bytes @0.3 | bytes @0.4054 | bytes @0.5 |
|---|---:|---:|---:|---:|---:|---:|
| Severance | 4.7406 | **4.7656** | 4.7602 | 154,474 | 164,631 | 175,010 |
| 21-classic | 4.6699 | 4.7285 | **4.7421** | 145,693 | 158,131 | 171,973 |
| velvet | 4.1063 | **4.1968** | 4.1807 | 166,341 | 173,105 | 179,515 |
| Greensleeves | 4.7258 | **4.7315** | 4.7142 | 141,168 | 146,735 | 151,820 |
| German | 4.6839 | **4.6954** | 4.6949 | 129,387 | 132,167 | 134,620 |

**Measured**: FAAC's own default `MAGIC_NUMBER` (0.4054) is at or within
noise of the best of the three on 4 of 5 clips, despite also producing the
*smallest* file of the three on most of them (0.3 rounds down harder,
producing fewer/smaller escape codes and smaller files, but scores worse
everywhere; 0.5 rounds up harder, producing bigger files, and is a coin-flip
against the default). None of the three closes any meaningful fraction of
the ~0.15-0.2 MOS gap between step1 and fdk's own MOS (fdk reference MOS was
4.85-4.92 across these clips; even the best rounding variant tops out at
4.62-4.76).

**Inference, labelled as such**: the rounding *threshold* is not the driver
of the fdk loss -- FAAC's existing constant is already close to optimal for
this task, and no nearby value meaningfully closes the gap. Combined with
the pre-quantization check (spectrum matches well, residual explained by
inherent low-`|q|` coarseness, not a scale/window bug), the loss most plausibly
sits in the accumulated effect of that quantization noise itself:
requantizing FAAC's spectrum at fdk's per-band scale reproduces fdk's exact
integer only 61-79% of the time (see the corrected line-level table above),
and the ~20-40% that land elsewhere -- overwhelmingly off by exactly 1 at low
`|q|`, per the region/bucket table -- are enough small, spectrum-wide
discrepancies to cost real perceptual quality even though no single one is
large. This is a real, if diffuse, quantizer-precision gap rather than a
bug still to find, though I would not treat that as fully settled without
also checking whether fdk's OWN choice of *which* bands to code (a
psychoacoustic decision this ladder holds fixed by construction) is doing
work that a value-for-value integer match can't detect either way.

## Verdict (corrected)

**Measured**: step1's bits-adjusted delta vs FAAC's own ABR 128k is
negative against both references -- mean -0.011 vs Apple (range +0.04 to
-0.06 on 4/5 clips, +0.01 on velvet) and mean -0.176 vs fdk (range -0.03 to
-0.33). Apple's own bits-adjusted lead over FAAC averages +0.084; **step1
gives essentially all of that lead back** (net ~-0.095 vs Apple's own
number), landing close to FAAC-128 rather than close to Apple. fdk's own
lead averages +0.037; step1 lands *below* FAAC-128 by ~0.21 against fdk --
worse than either endpoint.

**Inference, labelled as such**: step1 differs from a reference only in
(a) FAAC's own spectrum in place of the reference's, after the forced
window/M-S/TNS decisions, and (b) FAAC's quantizer rounding at the
reference's scale in place of the reference's own quantizer. The pre-
quantization check (fdk/Severance) shows (a) is sound: high correlation
(0.95-0.99), and a magnitude-dependent ratio that converges to 1.000 exactly
where AAC's own quantization is fine-grained, consistent with inherent
low-`|q|` coarseness rather than a spectral bug. The rounding sweep shows the
loss isn't sitting in the `MAGIC_NUMBER` choice either. That leaves the
*accumulated* effect of value-by-value quantizer disagreement (see the
line-level table: 61-79% exact, but the remainder mostly off by exactly 1,
spread across every band) as the best-supported explanation for the loss --
not a single bug, and not the rate loop (step1 never runs one). This
reframes the earlier "transplant losses were confounded by the rate loop"
reading from the previous revision, which the coordinator correctly flagged
as backwards: the ladder now points at the quantizer's rounding behavior
itself, in aggregate, as where FAAC's remaining gap to both references
lives, at least on this 5-clip sample.

**Stage D (49 clips)**: not run. Before spending that budget, I would want
either (a) a decisive test separating "diffuse rounding-precision gap" from
"one remaining bug I haven't isolated" -- e.g. checking whether the
off-by-1 lines are randomly distributed or concentrated in a specific
band/window pattern -- or (b) acceptance that the 5-clip result (consistent
sign and rough magnitude across all 5 clips, on both references) is already
enough signal to act on without a wider run.

## What's in place

- `libfaac/step1.c`/`step1.h`: the fixed step-1 engine (huffbook preset-book
  fix), plus `FAAC_STEP1_MAGIC` (rounding-constant override, for the sweep
  above).
- `libfaac/frame.c`: adds `FAAC_STEP1_SPEC_DUMP` (pre-quantization spectrum
  dump, same raw layout as the `Q` record) alongside the existing
  `FAAC_MDCT_ENERGY_DUMP` and `FAAC_STEP1`/`FAAC_STEP1_OFFSET` hooks.
- `probe/ladder/mdct_offset.py`: the offset-control script (peak +1,
  ciFrame-space).
- `probe/ladder/line_level.py`: **moved into the repo and fixed** (signed
  ratio, explicit groups/glen check) -- band-major line comparison +
  MDCT-ratio computation from two `FAAD_LADDER_DUMP` dumps. Callable
  standalone (`python3 line_level.py <step1_dump> <ref_dump> <offset>`).
- `probe/ladder/prequant_check.py`: new -- pre-quantization spectrum check,
  reads a `FAAC_STEP1_SPEC_DUMP` output and a reference dump, reports
  correlation/ratio/residual by frequency region and by `|q|` magnitude.
- `/tmp/ladder_c/`: all intermediates for this session's Stage C run (step1
  outputs at various offsets/magic values, FAAC 112/128/144 ladders, dumps,
  MOS score logs) -- not cleaned up yet in case a follow-up wants to re-
  derive tables without re-encoding; safe to delete otherwise.
- Patches (all uncommitted, nothing pushed, regenerated this session):
  `step1_engine.patch`, `frame_step1_hooks.patch`,
  `coder_channels_tns_writer.patch`, `reemit_and_build.patch`.

## Apple-aligned ladder (E1–E3)

### Initial E1 frame-energy control (superseded by shift sweep)

**Measurements.** Severance, 48 kHz stereo. `+64` prepends 64 zero samples per
channel; `−64` drops the first 64 samples per channel. The source WAV was
transformed with Python `wave` into `/tmp/ladder_e/Severance_{plus,minus}.wav`.
Each arm was encoded with `FAAC_MDCT_ENERGY_DUMP=/tmp/ladder_e/Severance_<arm>.energy
build-ladder/frontend/faac -b 128 -o /tmp/ladder_e/Severance_<arm>.m4a <source>`.
`python3 probe/ladder/mdct_offset.py <energy> <reference dump> <label>`
found the peak offset. The peak Pearson correlation was calculated by
`python3 /tmp/ladder_e/corr.py`, using `mdct_offset.py`'s extracted energy
series, paired at each offset, omitting startup frames 0–2. The latter is
the normalized correlation; `mdct_offset.py` itself reports an unnormalized
energy dot product. Reference dumps were under `probe/ladder/survey/`.

| reference | FAAC input | peak Pearson correlation | peak offset (dump frame − FAAC frame) |
|---|---|---:|---:|
| Apple | unshifted | 0.8770 | +2 |
| Apple | +64 | 0.9237 | +2 |
| Apple | −64 | 0.8651 | +2 |
| fdk | unshifted | 0.9636 | +2 |

**Inference.** +64 improves the Apple match, but does not reach the specified
0.95 minimum or fdk's unshifted 0.9636. Neither 64-sample direction passes.
The reason for the remaining gap is unresolved. Per the E1 stop condition,
scoring lag and the Apple re-emit known-answer score were **not run**.

### Initial E2 and E3 stop (superseded below)

Aligned step1 MOS, prequantization and line-level tables, and hybrid arms
were not measured because E1 failed. In particular, no K0/K1 known-answer
controls passed in this run, so there are no arm results to report. The
existing uncommitted `hybrid_merge.py` transition-window fallback was kept.

### E1 addendum (coordinator): shift sweep — +64 IS the Apple alignment

The 0.95 threshold in the brief was arbitrary. The discriminating test is whether the correlation *peaks* at +64 across a sweep. Script: scratchpad shift/sweep.py (same frame-energy Pearson as /tmp/ladder_e/corr.py, FAAC -b 128, Severance).

| shift | 0 | 16 | 32 | 48 | 56 | **64** | 72 | 80 | 96 | 128 | 256 | 512 | 1024 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Apple | .877 | .881 | .867 | .870 | .899 | **.924** | .915 | .891 | .870 | .894 | .866 | .846 | .877 (@1) |
| fdk | **.964** | .930 | .924 | .933 | .934 | .933 | .916 | .898 | .896 | .899 | .920 | .852 | .964 (@1) |

Apple peaks at exactly +64 (with shoulders at 56/72), and fdk peaks at 0. The 1024-sample row reproduces the 0 row one frame later, which is a known-answer check of the sweep. Apple's lower absolute level is expected from the metric: frame-energy correlation is taken against the reference's regular-band dequantized energy, and FAAC's own -b 128 window choices differ. The line-level check in E2 (step1 at Apple's forced windows) is the real alignment test.

### E1 continued: scoring alignment and known answer

**Measurements.** `python3 /tmp/ladder_e/all_lags.py` decoded with ffmpeg to stereo float PCM and cross-correlated a 30,000-sample source segment. `python3 /tmp/ladder_e/e2_score.py` trimmed the measured delay, converted with the established `faac-benchmark/scripts/score_clip.py` path, and scored serially with zimtohrli. Re-emit was driven by `probe/ladder/reemit_tool` from the archived Apple `*.bin` intermediates; `python3 /tmp/ladder_e/e3_controls.py` checked decoded PCM and byte identities. The `score_clip.py` import of unused ViSQOL failed in this sandbox, so `/tmp/ladder_e/visqol.py` raises `ImportError` to let its existing zimtohrli backend run. All audio scoring uses that same path.

| clip | aligned step1 lag | Apple re-emit lag | Apple decoded-WAV MOS | re-emit MOS | PCM / MOS known answer |
|---|---:|---:|---:|---:|---|
| Severance | +64 | +2112 | 4.9237 | 4.9237 | PASS |
| 21classic | +64 | +2112 | 4.8969 | 4.8969 | PASS |
| velvet | +64 | +2112 | 4.7807 | 4.7807 | PASS |
| Greensleeves | +64 | +2112 | 4.9026 | 4.9026 | PASS |
| German | +64 | +2112 | 4.9246 | 4.9246 | PASS |

`python3 /tmp/ladder_e/known_answer_pcm.py` confirmed that the re-emit PCM equals Apple’s decoded reference PCM exactly after 2112 samples on **all five clips** (maximum absolute difference 0 and zero differing samples over each source length). Both decoded WAVs scored identically on every clip. Scoring the Apple MP4 directly changes Greensleeves and German by 0.0001, a conversion-path difference; the E2 MP4 reference values below use that direct path. The prior energy sweep addendum establishes +64 as the alignment; its lower frame-energy coefficient is not used as the E2 gate.

### E2: Apple step1 on the aligned grid

**Measurements.** `python3 /tmp/ladder_e/e2_generate.py` prepended 64 zero samples per channel, encoded `FAAC_STEP1=<Apple intermediate> FAAC_STEP1_OFFSET=1 FAAC_STEP1_SPEC_DUMP=<path> build-ladder/frontend/faac -b 128`, and also encoded an unshifted control. `python3 probe/ladder/prequant_check.py <spec> <Apple dump> 2` produced the next table; fdk/Severance was rerun with the same command against its unshifted spec and dump. Each cell is per-line Pearson correlation.

| clip | grid | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |
|---|---|---:|---:|---:|---:|
| Severance | Apple +64 | 0.9994 | 0.9923 | 0.9761 | 0.9558 |
| Severance | Apple 0 | 0.1571 | 0.1890 | -0.1965 | -0.2381 |
| 21classic | Apple +64 | 0.9997 | 0.9962 | 0.9736 | 0.9559 |
| 21classic | Apple 0 | -0.3516 | 0.3605 | 0.1905 | -0.0684 |
| velvet | Apple +64 | 0.9989 | 0.9716 | 0.9608 | 0.9440 |
| velvet | Apple 0 | 0.8025 | 0.1321 | 0.1551 | -0.1104 |
| Greensleeves | Apple +64 | 0.9997 | 0.9910 | 0.9725 | 0.9561 |
| Greensleeves | Apple 0 | -0.3846 | 0.0744 | -0.0029 | -0.0652 |
| German | Apple +64 | 0.9998 | 0.9944 | 0.9809 | 0.9617 |
| German | Apple 0 | -0.2800 | 0.0468 | -0.0138 | -0.0264 |
| Severance | fdk 0 | 0.9884 | 0.9819 | 0.9689 | 0.9540 |

**Gate: PASS.** The aligned Apple correlations reach 0.9440–0.9998 by region across clips, near or above the fdk control; unshifted Apple correlations are markedly worse. Velvet >12 kHz is 0.9440, slightly below fdk/Severance’s 0.9540, while velvet’s other regions are 0.9608–0.9989. This is not a clearly lower aligned spectrum overall.

The same script reports signed FAAC/reference dequantized magnitude ratio (median [IQR]) by `|q|`. On Severance, Apple +64 gives small 0.904 [0.711, 1.130], mid 1.003 [0.957, 1.049], large 0.999 [0.985, 1.013]; Apple 0 gives 0.040 [−0.737, 0.779], 0.066 [−0.832, 0.900], 0.112 [−0.712, 0.866]. The rerun fdk control gives 0.815 [0.588, 1.108], 0.943 [0.751, 1.074], 0.997 [0.882, 1.064].

The same `prequant_check.py` runs give the signed prequant/reference ratio median [IQR] by region and `|q|` below. The full residual-dB and line-count outputs are `/tmp/ladder_e/*.{prequant,lines}`.

| clip | Apple grid | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |
|---|---|---|---|---|---|
| Severance | +64 | 0.999 [0.920, 1.077] | 0.961 [0.775, 1.121] | 0.867 [0.677, 1.079] | 0.821 [0.634, 1.041] |
| Severance | 0 | -0.079 [-0.943, 0.880] | 0.140 [-0.721, 0.859] | 0.056 [-0.653, 0.738] | -0.024 [-0.668, 0.648] |
| 21classic | +64 | 0.998 [0.912, 1.081] | 0.945 [0.756, 1.117] | 0.825 [0.658, 1.045] | 0.777 [0.630, 0.991] |
| 21classic | 0 | -0.057 [-0.939, 0.924] | 0.203 [-0.664, 0.881] | 0.022 [-0.639, 0.685] | 0.000 [-0.618, 0.614] |
| velvet | +64 | 0.997 [0.871, 1.131] | 0.941 [0.755, 1.146] | 0.863 [0.677, 1.083] | 0.929 [0.722, 1.204] |
| velvet | 0 | 0.096 [-0.811, 0.912] | 0.113 [-0.679, 0.813] | 0.085 [-0.597, 0.719] | -0.077 [-0.777, 0.663] |
| Greensleeves | +64 | 1.003 [0.937, 1.081] | 0.992 [0.838, 1.156] | 0.936 [0.738, 1.152] | 0.869 [0.682, 1.107] |
| Greensleeves | 0 | -0.102 [-1.005, 0.924] | 0.098 [-0.865, 0.965] | -0.016 [-0.813, 0.797] | 0.015 [-0.688, 0.711] |
| German | +64 | 1.005 [0.960, 1.062] | 1.007 [0.898, 1.140] | 1.000 [0.839, 1.197] | 0.987 [0.794, 1.236] |
| German | 0 | -0.115 [-1.016, 0.933] | 0.091 [-0.948, 1.049] | -0.031 [-0.971, 0.932] | -0.002 [-0.877, 0.873] |

| clip | Apple grid | small `|q|` 1–2 | mid 3–8 | large >8 |
|---|---|---|---|---|
| Severance | +64 | 0.904 [0.711, 1.130] | 1.003 [0.957, 1.049] | 0.999 [0.985, 1.013] |
| Severance | 0 | 0.040 [-0.737, 0.779] | 0.066 [-0.832, 0.900] | 0.112 [-0.712, 0.866] |
| 21classic | +64 | 0.877 [0.691, 1.100] | 1.001 [0.955, 1.047] | 0.999 [0.987, 1.011] |
| 21classic | 0 | 0.040 [-0.692, 0.749] | 0.210 [-0.822, 0.942] | 0.206 [-0.742, 0.961] |
| velvet | +64 | 0.908 [0.714, 1.152] | 1.002 [0.945, 1.068] | 1.003 [0.991, 1.020] |
| velvet | 0 | 0.035 [-0.694, 0.738] | 0.140 [-0.767, 0.904] | 0.738 [-0.273, 1.082] |
| Greensleeves | +64 | 0.946 [0.749, 1.178] | 1.000 [0.947, 1.057] | 1.000 [0.988, 1.014] |
| Greensleeves | 0 | 0.015 [-0.856, 0.875] | -0.026 [-0.861, 0.832] | -0.237 [-0.911, 0.602] |
| German | +64 | 1.003 [0.805, 1.249] | 1.004 [0.945, 1.066] | 1.003 [0.989, 1.017] |
| German | 0 | 0.008 [-1.002, 1.006] | -0.015 [-0.875, 0.861] | -0.107 [-0.882, 0.802] |

`python3 probe/ladder/line_level.py <step1 dump> <Apple dump> 1` produced the match and ratio tables below. The aggregate includes all regular-band lines emitted by that script. Percentages are weighted from its region and `|q|` rows by `python3 /tmp/ladder_e/e2_tables.py`.

| clip | regular lines | exact | ±1 | larger |
|---|---:|---:|---:|---:|
| Severance | 808,832 | 99.1% | 0.9% | 0.0% |
| 21classic | 782,848 | 99.4% | 0.6% | 0.0% |
| velvet | 517,440 | 97.9% | 1.9% | 0.2% |
| Greensleeves | 604,352 | 97.5% | 2.5% | 0.0% |
| German | 570,368 | 97.4% | 2.5% | 0.0% |

Severance by frequency and reference `|q|` (same `line_level.py` output):

| region | `|q|` | lines | exact | ±1 | larger |
|---|---:|---:|---:|---:|---:|
| 0-2k | 0 | 18,891 | 99.9% | 0.1% | 0.0% |
| 0-2k | 1 | 22,774 | 99.8% | 0.1% | 0.1% |
| 0-2k | 2-4 | 23,224 | 100.0% | 0.0% | 0.0% |
| 0-2k | >4 | 15,615 | 100.0% | 0.0% | 0.0% |
| 2-6k | 0 | 77,696 | 99.6% | 0.4% | 0.0% |
| 2-6k | 1 | 56,541 | 99.7% | 0.2% | 0.1% |
| 2-6k | 2-4 | 19,767 | 100.0% | 0.0% | 0.0% |
| 2-6k | >4 | 5,108 | 100.0% | 0.0% | 0.0% |
| 6-12k | 0 | 178,541 | 99.4% | 0.6% | 0.0% |
| 6-12k | 1 | 52,219 | 98.7% | 1.1% | 0.3% |
| 6-12k | 2-4 | 7,127 | 99.7% | 0.3% | 0.0% |
| 6-12k | >4 | 1,729 | 100.0% | 0.0% | 0.0% |
| >12k | 0 | 305,334 | 98.9% | 1.1% | 0.0% |
| >12k | 1 | 21,422 | 91.5% | 8.3% | 0.1% |
| >12k | 2-4 | 2,403 | 99.3% | 0.7% | 0.0% |
| >12k | >4 | 441 | 100.0% | 0.0% | 0.0% |

The signed dequantized step1/reference ratio from `line_level.py` is 1.000 [1.000, 1.000] in **each region of all five clips**; the per-region nonzero-line counts are in `/tmp/ladder_e/*_aligned.lines`.

`python3 /tmp/ladder_e/e2_score.py` scored the aligned MP4, Apple reference and FAAC 112/128/144 controls; `python3 /tmp/ladder_e/e3_score.py` rescored each old misaligned step1 MP4. Bits adjustment uses each clip’s measured slope `(MOS144−MOS112)/log2(bytes144/bytes112)` and subtracts `slope*log2(bytes_variant/bytes_faac128)`.

| clip | Apple ref MOS | aligned step1 MOS | old step1 MOS | FAAC-128 MOS | adj Apple | adj aligned | adj old |
|---|---:|---:|---:|---:|---:|---:|---:|
| Severance | 4.9237 | 4.9240 | 4.9000 | 4.8458 | +0.0717 | +0.0688 | +0.0425 |
| 21classic | 4.8969 | 4.8954 | 4.8623 | 4.8129 | +0.0762 | +0.0744 | +0.0392 |
| velvet | 4.7807 | 4.5886 | 4.5679 | 4.4807 | +0.2346 | +0.0487 | +0.0118 |
| Greensleeves | 4.9027 | 4.8517 | 4.8225 | 4.8526 | +0.0357 | -0.0122 | -0.0556 |
| German | 4.9245 | 4.9132 | 4.8985 | 4.9228 | +0.0028 | -0.0088 | -0.0331 |
| **mean** | | | | | **+0.0842** | **+0.0342** | **+0.0010** |

The old misaligned adjusted mean is +0.0010 when recomputed from the five raw rows. The earlier Stage C prose stated −0.0110, which does not equal its own per-clip table; the rescores here resolve that arithmetic discrepancy.

### E3: hybrid q-swap, controls first

**Measurements.** `python3 /tmp/ladder_e/e3_controls.py` ran `hybrid_merge.py K0/K1`, wrote ADTS with `probe/ladder/reemit_tool`, compared K0 bytes to each reference’s own re-emit, and compared K1 ffmpeg-decoded stereo float PCM after 2048 priming samples to step1’s decoded PCM. Both controls pass for every clip/reference over the full source length (zero differing samples). In transition windows with different ICS layout, the existing hybrid fallback uses reference ICS; K1 specifically uses step1 ICS so its known answer is exact. Layout fallback ICS counts, from `*.merge.log`:

| clip | Apple K0 | Apple K1 | Apple fallback ICS | fdk K0 | fdk K1 | fdk fallback ICS |
|---|---|---|---:|---|---|---:|
| Severance | PASS | PASS | 4 | PASS | PASS | 2 |
| 21classic | PASS | PASS | 2 | PASS | PASS | 2 |
| velvet | PASS | PASS | 350 | PASS | PASS | 350 |
| Greensleeves | PASS | PASS | 124 | PASS | PASS | 104 |
| German | PASS | PASS | 80 | PASS | PASS | 88 |

`python3 /tmp/ladder_e/e3_generate.py` generated Z, S, ZS, M, LO and HI, recomputing books in the writer. `python3 /tmp/ladder_e/e3_score.py` decoded each ADTS via ffmpeg, trimmed Apple by 2112 or fdk by 2048 samples, cropped to source length, and scored serially with `score_clip.py`. ADTS byte sizes are compared only among E3 arms. Δ = arm − K1 after each clip’s FAAC ladder slope byte adjustment. Gap recovered = adjusted Δ / adjusted (K0−K1). Pooled rows use mean MOS/adjusted Δ, sum bytes/changed lines, and ratio of summed adjusted gaps. All signs below were calculated as arm minus K1 from raw MOS and bytes by `python3 /tmp/ladder_e/derive.py`.

#### apple hybrid arms

| clip | arm | MOS | ADTS bytes | adj Δ vs K1 | gap recovered | lines changed |
|---|---|---:|---:|---:|---:|---:|
| Severance | K0 | 4.9237 | 164,827 | +0.0025 | +100.0% | 7,471 |
| Severance | K1 | 4.9240 | 166,086 | +0.0000 | +0.0% | 0 |
| Severance | Z | 4.9190 | 164,117 | -0.0006 | -23.7% | 4,699 |
| Severance | S | 4.9234 | 166,231 | -0.0009 | -36.7% | 2,380 |
| Severance | ZS | 4.9229 | 164,870 | +0.0016 | +64.3% | 7,079 |
| Severance | M | 4.9245 | 165,623 | +0.0015 | +61.0% | 392 |
| Severance | LO | 4.9240 | 165,676 | +0.0009 | +36.4% | 59 |
| Severance | HI | 4.9237 | 164,827 | +0.0025 | +100.0% | 7,412 |
| 21classic | K0 | 4.8969 | 161,500 | +0.0025 | +100.0% | 4,879 |
| 21classic | K1 | 4.8954 | 162,211 | +0.0000 | +0.0% | 0 |
| 21classic | Z | 4.8941 | 161,276 | -0.0000 | -0.6% | 3,439 |
| 21classic | S | 4.8948 | 162,418 | -0.0009 | -35.7% | 1,199 |
| 21classic | ZS | 4.8967 | 161,619 | +0.0021 | +85.3% | 4,638 |
| 21classic | M | 4.8965 | 162,093 | +0.0013 | +51.0% | 241 |
| 21classic | LO | 4.8955 | 162,199 | +0.0001 | +4.7% | 12 |
| 21classic | HI | 4.8969 | 161,500 | +0.0025 | +100.0% | 4,867 |
| velvet | K0 | 4.7807 | 178,031 | +0.1912 | +100.0% | 10,642 |
| velvet | K1 | 4.5886 | 177,804 | +0.0000 | +0.0% | 0 |
| velvet | Z | 4.7804 | 178,887 | +0.1877 | +98.1% | 6,575 |
| velvet | S | 4.7836 | 180,127 | +0.1862 | +97.4% | 502 |
| velvet | ZS | 4.7822 | 178,942 | +0.1893 | +99.0% | 7,077 |
| velvet | M | 4.7832 | 179,257 | +0.1891 | +98.9% | 3,565 |
| velvet | LO | 4.7867 | 180,074 | +0.1895 | +99.1% | 698 |
| velvet | HI | 4.7763 | 178,043 | +0.1868 | +97.7% | 9,944 |
| Greensleeves | K0 | 4.9026 | 144,724 | +0.0528 | +100.0% | 15,084 |
| Greensleeves | K1 | 4.8517 | 145,403 | +0.0000 | +0.0% | 0 |
| Greensleeves | Z | 4.8953 | 144,320 | +0.0466 | +88.3% | 6,341 |
| Greensleeves | S | 4.8911 | 145,705 | +0.0386 | +73.0% | 4,386 |
| Greensleeves | ZS | 4.8970 | 144,798 | +0.0470 | +89.0% | 10,727 |
| Greensleeves | M | 4.8968 | 145,267 | +0.0455 | +86.1% | 4,357 |
| Greensleeves | LO | 4.8910 | 145,326 | +0.0395 | +74.8% | 431 |
| Greensleeves | HI | 4.9018 | 144,728 | +0.0520 | +98.5% | 14,653 |
| German | K0 | 4.9246 | 125,188 | +0.0149 | +100.0% | 14,739 |
| German | K1 | 4.9132 | 126,649 | +0.0000 | +0.0% | 0 |
| German | Z | 4.9274 | 125,341 | +0.0173 | +116.3% | 5,265 |
| German | S | 4.9312 | 125,914 | +0.0197 | +132.7% | 3,118 |
| German | ZS | 4.9280 | 125,448 | +0.0177 | +118.7% | 8,383 |
| German | M | 4.9307 | 125,558 | +0.0201 | +135.0% | 6,356 |
| German | LO | 4.9311 | 125,815 | +0.0199 | +133.6% | 816 |
| German | HI | 4.9244 | 125,184 | +0.0147 | +98.7% | 13,923 |
| **pooled** | **K0** | **4.8857** | **774,270** | **+0.0528** | **+100.0%** | **52,815** |
| **pooled** | **K1** | **4.8346** | **778,153** | **+0.0000** | **+0.0%** | **0** |
| **pooled** | **Z** | **4.8832** | **773,941** | **+0.0502** | **+95.1%** | **26,319** |
| **pooled** | **S** | **4.8848** | **780,395** | **+0.0485** | **+92.0%** | **11,585** |
| **pooled** | **ZS** | **4.8854** | **775,677** | **+0.0515** | **+97.6%** | **37,904** |
| **pooled** | **M** | **4.8863** | **777,798** | **+0.0515** | **+97.6%** | **14,911** |
| **pooled** | **LO** | **4.8857** | **779,090** | **+0.0500** | **+94.7%** | **2,016** |
| **pooled** | **HI** | **4.8846** | **774,282** | **+0.0517** | **+97.9%** | **50,799** |

#### fdk hybrid arms

| clip | arm | MOS | ADTS bytes | adj Δ vs K1 | gap recovered | lines changed |
|---|---|---:|---:|---:|---:|---:|
| Severance | K0 | 4.8921 | 165,575 | +0.1254 | +100.0% | 94,700 |
| Severance | K1 | 4.7657 | 165,124 | +0.0000 | +0.0% | 0 |
| Severance | Z | 4.7673 | 159,882 | +0.0135 | +10.8% | 22,652 |
| Severance | S | 4.7742 | 168,836 | +0.0003 | +0.2% | 22,383 |
| Severance | ZS | 4.7782 | 164,259 | +0.0144 | +11.5% | 45,035 |
| Severance | M | 4.8752 | 166,455 | +0.1065 | +85.0% | 49,665 |
| Severance | LO | 4.8534 | 165,988 | +0.0858 | +68.4% | 44,224 |
| Severance | HI | 4.8036 | 164,690 | +0.0389 | +31.0% | 50,476 |
| 21classic | K0 | 4.8462 | 159,393 | +0.1165 | +100.0% | 87,547 |
| 21classic | K1 | 4.7286 | 158,576 | +0.0000 | +0.0% | 0 |
| 21classic | Z | 4.7255 | 154,562 | +0.0026 | +2.2% | 18,940 |
| 21classic | S | 4.7413 | 161,925 | +0.0081 | +6.9% | 19,640 |
| 21classic | ZS | 4.7405 | 158,349 | +0.0122 | +10.5% | 38,580 |
| 21classic | M | 4.8263 | 159,566 | +0.0963 | +82.7% | 48,967 |
| 21classic | LO | 4.8124 | 159,225 | +0.0829 | +71.2% | 44,411 |
| 21classic | HI | 4.7620 | 158,663 | +0.0333 | +28.6% | 43,136 |
| velvet | K0 | 4.6044 | 164,938 | +0.4422 | +100.0% | 44,510 |
| velvet | K1 | 4.1968 | 173,598 | +0.0000 | +0.0% | 0 |
| velvet | Z | 4.5704 | 163,865 | +0.4126 | +93.3% | 24,449 |
| velvet | S | 4.5725 | 171,076 | +0.3856 | +87.2% | 10,094 |
| velvet | ZS | 4.5784 | 165,162 | +0.4153 | +93.9% | 34,543 |
| velvet | M | 4.5960 | 169,816 | +0.4141 | +93.6% | 9,967 |
| velvet | LO | 4.5919 | 169,989 | +0.4093 | +92.6% | 8,069 |
| velvet | HI | 4.5798 | 165,006 | +0.4173 | +94.4% | 36,441 |
| Greensleeves | K0 | 4.8922 | 143,568 | +0.1707 | +100.0% | 78,604 |
| Greensleeves | K1 | 4.7313 | 147,073 | +0.0000 | +0.0% | 0 |
| Greensleeves | Z | 4.7718 | 142,950 | +0.0520 | +30.5% | 20,458 |
| Greensleeves | S | 4.7677 | 147,294 | +0.0358 | +21.0% | 12,098 |
| Greensleeves | ZS | 4.7762 | 144,217 | +0.0528 | +31.0% | 32,556 |
| Greensleeves | M | 4.8646 | 145,730 | +0.1370 | +80.3% | 46,048 |
| Greensleeves | LO | 4.8411 | 146,628 | +0.1110 | +65.1% | 29,037 |
| Greensleeves | HI | 4.8134 | 143,284 | +0.0927 | +54.3% | 49,567 |
| German | K0 | 4.8944 | 130,542 | +0.2032 | +100.0% | 80,426 |
| German | K1 | 4.6954 | 132,376 | +0.0000 | +0.0% | 0 |
| German | Z | 4.7154 | 131,186 | +0.0227 | +11.2% | 15,664 |
| German | S | 4.7220 | 132,798 | +0.0256 | +12.6% | 10,689 |
| German | ZS | 4.7144 | 131,585 | +0.0208 | +10.2% | 26,353 |
| German | M | 4.8768 | 131,532 | +0.1833 | +90.2% | 54,073 |
| German | LO | 4.8147 | 132,492 | +0.1190 | +58.6% | 17,679 |
| German | HI | 4.7995 | 130,578 | +0.1082 | +53.3% | 62,747 |
| **pooled** | **K0** | **4.8259** | **764,016** | **+0.2116** | **+100.0%** | **385,787** |
| **pooled** | **K1** | **4.6236** | **776,747** | **+0.0000** | **+0.0%** | **0** |
| **pooled** | **Z** | **4.7101** | **752,445** | **+0.1007** | **+47.6%** | **102,163** |
| **pooled** | **S** | **4.7155** | **781,929** | **+0.0911** | **+43.0%** | **74,904** |
| **pooled** | **ZS** | **4.7175** | **763,572** | **+0.1031** | **+48.7%** | **177,067** |
| **pooled** | **M** | **4.8078** | **773,099** | **+0.1875** | **+88.6%** | **208,720** |
| **pooled** | **LO** | **4.7827** | **774,322** | **+0.1616** | **+76.4%** | **143,420** |
| **pooled** | **HI** | **4.7517** | **762,221** | **+0.1381** | **+65.3%** | **242,367** |

**Z/S line character, measured.** `python3 /tmp/ladder_e/zs_character.py` walks each reference’s regular bands, corresponding step1 quantized lines, and `FAAC_STEP1_SPEC_DUMP` spectrum; `python3 /tmp/ladder_e/character_summary.py` pools counts. Frequencies are band-center regions; band peak/avg is maximum absolute prequant coefficient divided by mean absolute coefficient in that band, reported as the range of per-clip medians. “Isolated” means exactly one changed line in its band; “whole” means every regular line in the band changed. The percentages are shares of changed lines.

| reference | rule | changed lines | 0–2k | 2–6k | 6–12k | >12k | band peak/avg median range | isolated | whole |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| apple | Z | 26,319 | 1.7% | 9.1% | 21.0% | 68.2% | 2.92–3.47 | 14.9% | 0.00% |
| apple | S | 11,585 | 3.8% | 16.9% | 36.9% | 42.4% | 2.90–3.09 | 30.6% | 0.00% |
| fdk | Z | 102,163 | 13.7% | 25.6% | 26.5% | 34.2% | 2.93–3.47 | 29.3% | 0.13% |
| fdk | S | 74,904 | 20.4% | 35.0% | 30.4% | 14.1% | 2.94–3.64 | 42.5% | 0.00% |

**Inference.** Apple’s +64 spectrum and quantized lines now nearly reproduce its reference; the prior misaligned Apple result cannot diagnose its quantizer. For fdk, M and LO recover much more of the K0−K1 gap than Z/S on most clips. Velvet’s 350 fallback ICS in each reference make its hybrid arms unusually close to K0 by construction; its arm gains should not be attributed wholly to each line-swap rule. Small Apple K0−K1 gaps on Severance and 21classic also make their per-clip recovery percentages sensitive to small score changes.

## Stage F: Apple decision swaps

### F0: force transition windows

**Measurement.** `libfaac/frame.c` now applies the matched reference window
sequence before `FilterBank` for LONG_SHORT and SHORT_LONG as well as the
ordinary long and short sequences. `BlockSwitch` still runs for an unmatched
record; its result is overridden for a matched record. `python3
/tmp/ladder_f/f0_generate.py` encoded the five +64-sample inputs using
`FAAC_STEP1=<Apple bin> FAAC_STEP1_OFFSET=1`, decoded with
`FAAD_LADDER_DUMP=1 FAAD_DUMP=<path>`, and compared window sequence, shape,
grouping and `max_sfb` against the Apple dump at dump-frame offset +1.

| clip | paired ICS | unforced ICS | F0 decode |
|---|---:|---:|---|
| Severance | 940 | 0 | PASS |
| 21classic | 908 | 0 | PASS |
| velvet | 940 | 0 | PASS |
| Greensleeves | 820 | 0 | PASS |
| German | 738 | 0 | PASS |

`python3 /tmp/ladder_f/f0_score.py` used ffmpeg stereo float decode, removed
the measured 64-sample +64-input lag, cropped to source length, and ran
`faac-benchmark/scripts/score_clip.py` serially with zimtohrli. It rescored
Apple and FAAC 112/128/144 MP4s in this job. `python3
/tmp/ladder_f/f0_derive.py` computed each clip's ladder slope and
bits-adjusted delta versus FAAC-128 from those scores and file sizes.

| clip | F0 A MOS | F0 A bytes | Apple MOS | Apple bytes | FAAC-128 MOS | adj A | adj Apple |
|---|---:|---:|---:|---:|---:|---:|---:|
| Severance | 4.9240 | 165,185 | 4.9237 | 164,154 | 4.8458 | +0.0697 | +0.0717 |
| 21classic | 4.8954 | 161,756 | 4.8969 | 161,500 | 4.8129 | +0.0744 | +0.0762 |
| velvet | 4.7876 | 180,306 | 4.7807 | 178,939 | 4.4807 | +0.2364 | +0.2346 |
| Greensleeves | 4.8890 | 145,124 | 4.9027 | 146,220 | 4.8526 | +0.0250 | +0.0357 |
| German | 4.9313 | 125,695 | 4.9245 | 126,356 | 4.9228 | +0.0111 | +0.0028 |

All F0 streams decoded cleanly: the decoder logs in `/tmp/ladder_f/` report
zero non-END terminations and zero concealment. The E3 fallback is now zero,
so the requested F arm (K1 plus reference ICS only at a fallback) has no
ICS to replace and was skipped.

### F1: FAAC decisions and KF/KA gate — KF FAIL; stopped

**Measurement.** `python3 /tmp/ladder_f/f1_generate.py` encoded each +64 input
with normal `faac -b 128`, dumped it through the same FAAD decoder, and
produced `/tmp/ladder_f/*_F1.bin` with `parse_dump.py`. The FAAC-normal and
F0 dumps have the same decoded-frame numbering (offset 0). `python3
/tmp/ladder_f/f1_lag.py` independently measured **zero PCM sample lag**
between the two encodes on all five clips.

`python3 /tmp/ladder_f/controls.py` reran KA from Apple's binary at offset
1 and KF from FAAC's own binary at offset 0 on the same +64 input. It
extracted ADTS from each MP4 with FAAD, compared bytes, decoded stereo
float PCM with ffmpeg, and found the first differing PCM sample. The frame
column is zero-based `sample // 1024`.

| clip | KA ADTS/PCM | KF ADTS | KF PCM max absolute difference | first differing sample | first differing frame |
|---|---|---|---:|---:|---:|
| Severance | PASS / exact | FAIL | 0.02277935 | 0 | 0 |
| 21classic | PASS / exact | FAIL | 0.06492334 | 32,769 | 32 |
| velvet | PASS / exact | FAIL | 0.22699441 | 0 | 0 |
| Greensleeves | PASS / exact | FAIL | 0.11354631 | 0 | 0 |
| German | PASS / exact | FAIL | 0.01622593 | 0 | 0 |

**Inference.** Transition forcing removes the observed ICS-layout fallback,
but KF still does not reproduce FAAC's natural encode. The cause of KF's
spectral difference was not diagnosed in this stage. Per the required
control gate, F2 WIN/BW/CLS/SF/MS/TNS/ALLF arms, their scores, and the
decision-statistics table were **not run**. There is no arm attribution to
report until KF passes.

### F1b: KF diagnosis

**Measurements.** `python3 /tmp/ladder_f/dump_kf.py` decoded the original KF streams with `FAAD_LADDER_DUMP=1 FAAD_DUMP=<path>`. `python3 /tmp/ladder_f/diff_ics.py` compared those dumps to the FAAC-normal dumps from F1 at frame offset 0. The table reports the percent of paired ICS (window, gain, TNS, pulse, any quantized-line change) or paired bands (class, book, SF, M/S); PNS energy and IS position use only paired bands of the named class. Class means ZERO/regular/PNS/IS, while “book” compares the exact regular Huffman book too. Pulse is the dump’s pulse-present flag. All streams here are one CPE, element 0.

| component | unit | Severance | 21classic | velvet | Greensleeves | German |
|---|---|---:|---:|---:|---:|---:|
| window sequence | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| window shape | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| grouping | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| max_sfb | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| band class | bands | 0.00% | 0.00% | 0.01% | 0.00% | 0.00% |
| exact Huffman book | bands | 0.13% | 0.24% | 4.51% | 5.68% | 3.09% |
| SF | bands | 0.00% | 0.00% | 0.01% | 0.00% | 0.00% |
| global gain | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| M/S mask | bands | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| TNS | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| PNS energy | PNS bands | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| IS position | IS bands | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| pulse present | ICS | 0.00% | 0.00% | 0.00% | 0.00% | 0.00% |
| any q-line mismatch | ICS | 7.55% | 7.82% | 49.89% | 46.22% | 48.92% |

The same script counts quantized lines and, separately, lines in bands whose **entire recorded syntax** matched (window/shape/grouping, max_sfb, class, exact book, SF, global gain, M/S, TNS, pulse). Thus the latter differences cannot be explained by a different transmitted decision.

| clip | paired ICS | paired bands | differing q lines / regular lines | differing q lines with all syntax matching | first differing dump frame / element / component |
|---|---:|---:|---:|---:|---|
| Severance | 940 | 41,190 | 459 / 563,968 (0.081%) | 393 / 562,892 (0.070%) | 1 / CPE ch0 / book and q |
| 21classic | 908 | 39,112 | 798 / 577,500 (0.138%) | 709 / 575,244 (0.123%) | 34 / CPE ch0 / book and q |
| velvet | 940 | 25,468 | 30,197 / 468,800 (6.441%) | 25,261 / 442,400 (5.710%) | 1 / CPE ch0 / q |
| Greensleeves | 820 | 34,634 | 24,984 / 402,648 (6.205%) | 20,740 / 362,080 (5.728%) | 1 / CPE ch0 / q |
| German | 738 | 34,868 | 12,864 / 342,984 (3.751%) | 10,995 / 319,444 (3.442%) | 1 / CPE ch0 / q |

**Cause 1, measured and verified: intensity stereo changes the left spectrum and its quantizer bias.** `python3 /tmp/ladder_f/q_context.py` found that 109/459, 624/798, 30,123/30,197, 24,834/24,984, and 11,827/12,864 original regular-line differences (clip order above) sat in the left channel with an IS right partner. `stereo.c::apply_is` replaces the left spectrum with a scaled sum/difference and sets `cl->sf[band]` to the left energy bias before `BlocQuant`; the old step1 applied neither. The self-reference probe now derives that transform and bias from the dumped IS decision. `python3 /tmp/ladder_f/verify_is_bias.py` and `diff_is_bias.py` measured the staged reduction below. KA was byte/PCM exact on each staged rerun.

| clip | original differing regular q lines | after self IS transform + SF bias | after M/S-ZERO fix |
|---|---:|---:|---:|
| Severance | 459 | 350 | 0 |
| 21classic | 798 | 174 | 0 |
| velvet | 30,197 | 74 | 0 |
| Greensleeves | 24,984 | 150 | 0 |
| German | 12,864 | 1,037 | 0 |

**Cause 2, measured and verified: M/S happens before band zeroing.** `python3 /tmp/ladder_f/remaining_context.py` found that every residual line after the IS-bias stage had M/S enabled and the opposite channel’s final book ZERO: 350, 174, 74, 150, and 1,037 lines. In `stereo.c`, `apply_ms_full` changes both spectra before `quantize.c::assign_band_codebooks` can zero one side. The previous `Step1ApplyMS` skipped such a band because it looked only at final regular books. In FAAC self mode it now applies M/S when one side was later zeroed, in either channel. `python3 /tmp/ladder_f/verify_self_full.py` first cleared four clips; the remaining ten Severance lines were the mirror (right regular, left ZERO). Adding that case cleared Severance too.

**Other candidates.** The initial dump comparison measured identical PNS energy, IS position, SF, global gain, TNS, pulse, and M/S mask. The two fixes above removed every decoded and ADTS difference without changing `BlocQuant`, PNS, the rate loop, or TNS. This rules those paths out as necessary causes of the observed KF mismatch on these five clips; it does not establish their behavior on other inputs.

**Final controls: PASS.** `CCACHE_DISABLE=1 meson compile -C build-ladder` rebuilt the final code. `python3 /tmp/ladder_f/verify_clean.py` ran KF with `FAAC_STEP1_SELF_IS=1` and FAAC’s own binary at offset 0, and KA with Apple’s binary at offset 1 and the self mode unset. It extracted ADTS and decoded stereo float PCM for all five clips. **KF and KA were byte-identical to their respective targets on all five; decoded PCM maximum absolute difference was 0 in every comparison.** The explicit self mode preserves the established Apple KA spectrum path. Since KF is exact, a separate KF MOS and byte-adjusted score was not needed. F2 was not run in this diagnosis task.

### F2: single-decision swaps

**Measurement and controls.** `python3 /tmp/ladder_f/f2_generate.py` merged the Apple dump at step1 offset +1 with the +64-input FAAC-normal dump at offset 0. `FAAC_STEP1_ORIGIN` is a per-band mask; `libfaac/step1.c` applies FAAC’s self-reference IS transform/SF bias and M/S-before-zeroing only to marked bands. For a CPE, either channel marking a band selects the FAAC stereo treatment for that band. A/no-swap retained Apple decisions; ALLF used FAAC decisions in every ICS. `CCACHE_DISABLE=1 meson compile -C build-ladder` rebuilt the probe, and `python3 /tmp/ladder_f/f2_controls.py` extracted ADTS and compared it byte for byte. Both controls passed on **all five clips**: A = KA and ALLF = KF, byte-identical (thus decoded PCM exact). The controls were rerun after the final per-band change and before arm scoring.

| control | Severance | 21classic | velvet | Greensleeves | German |
|---|---|---|---|---|---|
| A/no-swap = KA ADTS | PASS | PASS | PASS | PASS | PASS |
| ALLF/per-band = KF ADTS | PASS | PASS | PASS | PASS | PASS |

**Measurement.** `python3 /tmp/ladder_f/f2_encode.py` encoded and ffmpeg-decoded each mixed arm: all 30 streams decoded with no ffmpeg error output. `python3 /tmp/ladder_f/f2_score.py` decoded stereo float PCM, dropped the +64 input samples, cropped to the source length, and scored all arms and the FAAC 112/128/144 controls serially through `faac-benchmark/scripts/score_clip.py` (zimtohrli). Bytes are MP4 bytes. `python3 /tmp/ladder_f/f2_derive.py` computed the per-clip slope `(MOS144−MOS112)/log2(bytes144/bytes112)` and adjusted Δ = `(MOS_arm−MOS_A) − slope·log2(bytes_arm/bytes_A)`. Share = `−adjusted Δ / −adjusted Δ_ALLF`; single-arm shares need not sum to 100% because decisions interact. Pooled and clean rows use mean MOS/Δ, summed bytes and units, and ratio of summed adjusted losses. `python3 /tmp/ladder_f/f2_units.py` counted changed ICS and bands from aligned dumps; `f2_generate.py` counted changes in each isolated arm. A direct assertion checked all 40 clip-arm Δ signs against raw MOS and bytes. The units column reports changed window/TNS ICS for WIN/TNS, changed bands for BW/CLS/SF/MS, and both measures for ALLF; zero in the other unit does not mean the arm made no change.

| clip | arm | MOS | bytes | adj Δ vs A | gap share | units changed |
|---|---|---:|---:|---:|---:|---:|
| Severance | A | 4.9240 | 165,185 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| Severance | WIN | 4.9225 | 165,216 | -0.00157 | +2.4% | 10 ICS / 0 bands |
| Severance | BW | 4.9240 | 165,054 | +0.00029 | -0.4% | 928 ICS / 1,856 bands |
| Severance | CLS | 4.9233 | 168,588 | -0.00823 | +12.6% | 0 ICS / 7,929 bands |
| Severance | SF | 4.8263 | 153,721 | -0.07113 | +109.1% | 0 ICS / 29,653 bands |
| Severance | MS | 4.9129 | 173,706 | -0.02968 | +45.5% | 0 ICS / 18,312 bands |
| Severance | TNS | 4.9240 | 165,197 | -0.00003 | +0.0% | 2 ICS / 0 bands |
| Severance | ALLF | 4.8503 | 161,422 | -0.06519 | +100.0% | 940 ICS / 42,106 bands |
| 21classic | A | 4.8954 | 161,756 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| 21classic | WIN | 4.8822 | 161,794 | -0.01325 | +17.2% | 78 ICS / 0 bands |
| 21classic | BW | 4.8954 | 161,644 | +0.00015 | -0.2% | 830 ICS / 1,660 bands |
| 21classic | CLS | 4.8958 | 163,187 | -0.00156 | +2.0% | 0 ICS / 4,854 bands |
| 21classic | SF | 4.7783 | 149,556 | -0.09968 | +129.6% | 0 ICS / 27,918 bands |
| 21classic | MS | 4.8924 | 169,775 | -0.01375 | +17.9% | 0 ICS / 15,748 bands |
| 21classic | TNS | 4.8954 | 161,756 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| 21classic | ALLF | 4.8102 | 155,853 | -0.07694 | +100.0% | 908 ICS / 40,355 bands |
| velvet | A | 4.7876 | 180,306 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| velvet | WIN | 4.5092 | 164,076 | -0.21460 | +97.3% | 900 ICS / 0 bands |
| velvet | BW | 4.7876 | 180,306 | +0.00000 | +0.0% | 4 ICS / 8 bands |
| velvet | CLS | 4.7471 | 178,453 | -0.03351 | +15.2% | 0 ICS / 513 bands |
| velvet | SF | 4.7813 | 180,492 | -0.00700 | +3.2% | 0 ICS / 542 bands |
| velvet | MS | 4.5894 | 181,067 | -0.20105 | +91.1% | 0 ICS / 570 bands |
| velvet | TNS | 4.7874 | 180,387 | -0.00050 | +0.2% | 14 ICS / 0 bands |
| velvet | ALLF | 4.4973 | 162,652 | -0.22060 | +100.0% | 940 ICS / 41,401 bands |
| Greensleeves | A | 4.8890 | 145,124 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| Greensleeves | WIN | 4.8502 | 138,407 | -0.01963 | +76.1% | 752 ICS / 0 bands |
| Greensleeves | BW | 4.8890 | 145,116 | +0.00002 | -0.1% | 64 ICS / 128 bands |
| Greensleeves | CLS | 4.8882 | 148,800 | -0.01092 | +42.3% | 0 ICS / 1,005 bands |
| Greensleeves | SF | 4.8888 | 145,943 | -0.00248 | +9.6% | 0 ICS / 1,768 bands |
| Greensleeves | MS | 4.8879 | 145,284 | -0.00155 | +6.0% | 0 ICS / 1,852 bands |
| Greensleeves | TNS | 4.8890 | 145,128 | -0.00001 | +0.0% | 2 ICS / 0 bands |
| Greensleeves | ALLF | 4.8517 | 141,058 | -0.02581 | +100.0% | 820 ICS / 40,555 bands |
| German | A | 4.9313 | 125,695 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| German | WIN | 4.9251 | 128,661 | -0.01320 | +129.1% | 676 ICS / 0 bands |
| German | BW | 4.9313 | 125,685 | +0.00002 | -0.2% | 54 ICS / 108 bands |
| German | CLS | 4.9261 | 126,190 | -0.00638 | +62.4% | 0 ICS / 505 bands |
| German | SF | 4.9292 | 123,466 | +0.00327 | -32.0% | 0 ICS / 1,655 bands |
| German | MS | 4.7463 | 125,727 | -0.18508 | +1809.5% | 0 ICS / 496 bands |
| German | TNS | 4.9309 | 125,673 | -0.00035 | +3.4% | 18 ICS / 0 bands |
| German | ALLF | 4.9237 | 126,800 | -0.01023 | +100.0% | 738 ICS / 38,184 bands |
| Pooled | A | 4.8855 | 778,066 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| Pooled | WIN | 4.8178 | 758,154 | -0.05245 | +65.8% | 2,416 ICS / 0 bands |
| Pooled | BW | 4.8855 | 777,805 | +0.00010 | -0.1% | 1,880 ICS / 3,760 bands |
| Pooled | CLS | 4.8761 | 785,218 | -0.01212 | +15.2% | 0 ICS / 14,806 bands |
| Pooled | SF | 4.8408 | 753,178 | -0.03540 | +44.4% | 0 ICS / 61,536 bands |
| Pooled | MS | 4.8058 | 795,559 | -0.08622 | +108.1% | 0 ICS / 36,978 bands |
| Pooled | TNS | 4.8853 | 778,141 | -0.00018 | +0.2% | 36 ICS / 0 bands |
| Pooled | ALLF | 4.7866 | 747,785 | -0.07975 | +100.0% | 4,346 ICS / 202,601 bands |
| Clean | A | 4.9097 | 326,941 | +0.00000 | +0.0% | 0 ICS / 0 bands |
| Clean | WIN | 4.9024 | 327,010 | -0.00741 | +10.4% | 88 ICS / 0 bands |
| Clean | BW | 4.9097 | 326,698 | +0.00022 | -0.3% | 1,758 ICS / 3,516 bands |
| Clean | CLS | 4.9096 | 331,775 | -0.00489 | +6.9% | 0 ICS / 12,783 bands |
| Clean | SF | 4.8023 | 303,277 | -0.08541 | +120.2% | 0 ICS / 57,571 bands |
| Clean | MS | 4.9026 | 343,481 | -0.02171 | +30.6% | 0 ICS / 34,060 bands |
| Clean | TNS | 4.9097 | 326,953 | -0.00001 | +0.0% | 2 ICS / 0 bands |
| Clean | ALLF | 4.8302 | 317,275 | -0.07107 | +100.0% | 1,848 ICS / 82,461 bands |

**Decision statistics, measured.** `python3 /tmp/ladder_f/f2_stats.py` compared each Apple frame `n+1` with FAAC frame `n` on the same +64 grid. Short/TNS percentages use ICS; class and M/S percentages use coded bands. `max_sfb` and coded bandwidth are means across ICS; bandwidth converts the SFB edge using the 48-kHz tables in `probe/ladder/line_level.py`. Region SF means include regular bands only. A short window has a different SFB scale, so bandwidth is the comparable coverage measure.

| clip | ref | short % | mean max_sfb | mean edge kHz | ZERO / REG / PNS / IS % | SF 0–2 / 2–6 / 6–12 / >12 kHz | M/S % | TNS % |
|---|---|---:|---:|---:|---|---|---:|---:|
| Severance | Apple | 0.4 | 45.9 | 20.25 | 18.2/81.8/0.0/0.0 | 156.5/150.5/147.2/141.0 | 92.0 | 0.4 |
| Severance | FAAC | 0.9 | 43.7 | 18.77 | 0.4/85.7/13.9/0.0 | 154.9/152.5/147.5/140.2 | 52.4 | 0.0 |
| 21classic | Apple | 0.2 | 45.9 | 20.25 | 14.8/85.2/0.0/0.0 | 161.2/152.3/143.3/140.5 | 86.7 | 0.7 |
| 21classic | FAAC | 4.2 | 42.7 | 18.84 | 0.4/88.0/11.4/0.2 | 159.1/154.4/144.8/140.6 | 50.2 | 0.0 |
| velvet | Apple | 25.5 | 37.6 | 20.44 | 15.5/84.4/0.0/0.1 | 149.2/154.3/154.3/154.6 | 49.0 | 12.4 |
| velvet | FAAC | 99.1 | 13.3 | 20.98 | 0.6/58.6/15.4/25.4 | 149.7/149.4/150.2/153.2 | 0.8 | 0.0 |
| Greensleeves | Apple | 11.5 | 42.2 | 20.34 | 11.4/68.7/0.0/19.8 | 148.5/146.8/138.5/139.0 | 37.1 | 38.0 |
| Greensleeves | FAAC | 88.5 | 16.6 | 20.74 | 0.3/61.3/7.9/30.5 | 142.7/142.6/138.6/131.8 | 8.3 | 0.0 |
| German | Apple | 7.9 | 43.4 | 20.31 | 39.0/49.8/0.0/11.2 | 142.9/140.5/137.5/138.9 | 77.6 | 46.6 |
| German | FAAC | 83.7 | 18.0 | 20.63 | 5.0/55.0/2.5/37.5 | 141.6/140.2/138.1/133.7 | 14.6 | 4.9 |

**Dominant-arm characterization, measured.** The pooled M/S swap loses 108.1% of the adjusted A→ALLF gap; on clean clips, the SF swap loses 120.2%; velvet’s WIN swap loses 97.3%. These are isolated-swap effects, not additive attribution. `python3 /tmp/ladder_f/f2_character.py` counted M/S disagreement in the *same-layout* subset by region and signal clip. The table gives common bands and percent with differing M/S masks; unlike the overall decision table, it excludes frames with different windows/grouping.

| clip / signal | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |
|---|---:|---:|---:|---:|
| Severance / clean | 39.3% (7,430) | 40.1% (5,110) | 31.5% (3,720) | 72.1% (4,182) |
| 21classic / clean | 36.6% (6,640) | 38.9% (4,565) | 34.3% (3,320) | 67.8% (3,735) |
| velvet / transient | 57.3% (143) | 47.4% (133) | 52.4% (164) | 41.9% (129) |
| Greensleeves / speech | 37.0% (527) | 77.1% (367) | 65.6% (276) | 88.1% (303) |
| German / speech | 8.2% (474) | 10.9% (339) | 18.4% (272) | 42.8% (285) |

In the matched-layout subset, `f2_character.py` also found the frequent clean-clip M/S disagreements in regular/regular long-window bands below 6 kHz, while >12 kHz includes many Apple ZERO/FAAC PNS pairs. Velvet’s disagreements are predominantly short-window regular/regular or regular/IS pairs; the two speech clips have many regular/IS and regular/ZERO mismatches. The overall M/S shares in the decision table cover all frames, including the unmatched-window majority of the three non-clean clips.

**Clean-clip SF characterization, measured.** `python3 /tmp/ladder_f/f2_stats.py` compared FAAC minus Apple SF in bands regular in both references and with identical windows/grouping. Entries are differing-band %, mean signed SF difference, and mean absolute SF difference; SF retains Apple global gain in the isolated arm.

| clip | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |
|---|---:|---:|---:|---:|
| Severance | 91.1% / -1.63 / 3.35 (n=14,807) | 88.8% / +2.15 / 3.30 (n=10,019) | 89.3% / +1.83 / 3.49 (n=6,185) | 92.3% / +1.87 / 4.14 (n=1,889) |
| 21classic | 91.0% / -2.08 / 3.35 (n=13,268) | 89.2% / +2.11 / 3.20 (n=9,041) | 83.4% / +1.66 / 2.29 (n=6,554) | 83.1% / +0.91 / 1.95 (n=2,787) |

**Inference.** The clean-clip lead is strongly associated with SF decisions on this grid; velvet’s loss is strongly associated with window decisions. The pooled M/S arm is dominated by velvet and an especially large German isolated loss even though German ALLF is close to A. The German 1,809.5% share is evidence of interaction with the other FAAC decisions, so it is not a standalone estimate of M/S’s contribution in the fully FAAC stream. No encoder fix is proposed here.


## Linux reproduction (Jules)

The reference ladder setup and controls were reproduced on Linux (Ubuntu 24.04 x86_64).

### Setup and Script Porting

- **Setup script (`probe/ladder/jules_setup.sh`)**: Created `probe/ladder/jules_setup.sh` to automate environment setup from a fresh VM:
  1. Builds `libfaac` on this branch with Meson (`build_ladder`).
  2. Compiles `probe/ladder/reemit_main.c` into `probe/ladder/reemit_tool`.
  3. Checks out `origin/faad-ladder-dump` (via git worktree at `/tmp/faad-ladder-dump`) and builds the FAAD3 decoder with `-Dstats=true`.
  4. Sets up `/opt/faac-benchmark`, installs requirements, fetches datasets via `setup_datasets.py`, and maps source WAVs in `/opt/faac-benchmark/data/external/audio/`.
  5. Verifies `zimtohrli` MOS scorer and `ffmpeg` dependencies.
- **Ported Scripts**: Updated all Stage E/F scripts in `probe/ladder/scripts/` to remove hardcoded `/tmp` and `/Users` paths. Work directory is configurable via `sys.argv[1]` or `LADDER_WORK` env var (defaulting to `./ladder_work`), and binaries/data paths are configurable via environment variables (`FAAC_BIN`, `FAAD_BIN`, `FAAC_BENCHMARK_DATA`, `SCORE_CLIP`, `PYTHON_BIN`).

### Known-Answer Controls

| Control | Status | Description |
|---|---|---|
| **Control 0** | **PASS** | Apple ref -> FAAD_LADDER_DUMP -> `parse_dump.py` -> `reemit_tool` decodes to float PCM identical to Apple's own decode after 2112 samples delay (max absolute error 0.0, zero differing samples). |
| **KA Control** | **PASS** | Step1 with Apple decisions (+64 prepended zero samples, `FAAC_STEP1_OFFSET=1`, transition windows forced) reproduces committed Stage F0 behavior with exact matching byte counts across all 5 clips. |
| **KF Control** | **PASS** | FAAC normal -b 128 encode of +64 input, dumped and fed back through step1 with `FAAC_STEP1_SELF_IS=1` at offset 0, produces 100% byte-identical ADTS and 0.0 max absolute PCM error to FAAC's normal encode on all 5 clips. |

### Stage F0 Bytes and Linux MOS Scores

MOS scores evaluated using `score_clip.py` (`zimtohrli` backend) on Linux against source WAVs:

| clip | F0 bytes target | Linux FAAC-112 MOS (bytes) | Linux FAAC-128 MOS (bytes) | Linux FAAC-144 MOS (bytes) | macOS FAAC-128 MOS | MOS diff ($\Delta$) |
|---|---:|---:|---:|---:|---:|---:|
| Severance | 165,185 | 4.7834 (141,718) | 4.8457 (161,441) | 4.8743 (181,310) | 4.8458 | -0.0001 |
| 21classic | 161,756 | 4.7780 (136,850) | 4.8132 (155,936) | 4.8330 (175,144) | 4.8129 | +0.0003 |
| velvet | 180,306 | 4.4206 (144,052) | 4.4807 (162,456) | 4.5798 (182,280) | 4.4807 | 0.0000 |
| Greensleeves | 145,124 | 4.7915 (123,884) | 4.8525 (141,091) | 4.8910 (158,478) | 4.8526 | -0.0001 |
| German | 125,695 | 4.8738 (111,214) | 4.9229 (126,798) | 4.9483 (142,534) | 4.9228 | +0.0001 |

Linux FAAC-128 MOS scores match macOS MOS scores within 0.0003 across all 5 ladder clips.
