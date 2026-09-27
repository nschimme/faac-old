# Unstacked SBR grid: size and quality check

Measured on the unstacked branch against `ca4c0915` (master) and its own
`260d0c15` starting point. [PR #583](https://github.com/nschimme/faac/pull/583)
used `sbr-dt-coupling` as its base, so its reported `+0.0135` / `+0.0196` MOS
gains are a target from a different comparison, not gains attributable to this
branch against master.

## GCC 13 footprint

Identical Linux x86-64 GCC 13.3 / Meson release builds; bytes are ELF `.text +
.rodata + .data` in `libfaac.so`. Each source tree was copied without its Git
metadata into the same container before building, to avoid host/container clock
skew and keep generated version strings consistent.

| Source | `.text` | `.rodata` | `.data` | Total |
| --- | ---: | ---: | ---: | ---: |
| Master | 63,712 | 11,084 | 1,184 | 75,980 |
| Unstacked starting branch | 68,464 | 11,144 | 1,184 | 80,792 |
| This change | 67,520 | 11,172 | 1,184 | 79,876 |

The change saves **916 bytes** from the starting branch. In the non-LTO static
objects, `sbr_bitstream.c.o` `.text` falls from 4,611 to 3,827 bytes;
`sbr_analysis.c.o` grows from 4,753 to 4,899 bytes for the low bitrate policy.
The full linked result is authoritative because link-time optimization changes
cross-object layout.

The VARVAR split search removal saved 784 linked bytes by itself. Combining the
variable grid-writing paths saved another 80 bytes; sharing all three variable
classes saved another 208 bytes. The transient threshold and low bitrate quality
changes add some code back.

## Forced HE-AAC v1, 32 kHz stereo

49 clips from `audio_32k`, forced HE-AAC v1 at 64 and 96 kbps, FAAD 3.0.0
decode, ViSQOL 3.7.0 audio MOS. Master and candidate were built with the same
Apple Clang release settings for audio comparisons. All 98 final streams decoded
without concealment; the final build's AAC packets matched the scored tuned
build on all 98 streams.

| Rate | Master MOS | Starting branch MOS | Final MOS | Final gain vs master | Worst clip loss vs master | Mean achieved AAC kbps, master → final | Mean stereo coherence error, master → final |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 64 kbps | 4.17695 | 4.17918 | 4.18196 | +0.00500 | −0.01931 | 64.119 → 64.132 | 0.01506 → 0.01508 |
| 96 kbps | 4.23101 | 4.23373 | 4.23601 | +0.00501 | −0.01960 | 95.789 → 95.802 | 0.01556 → 0.01560 |

The worst clip at both rates is `take_your_finger_frin_my_head`; neither loss
reaches 0.02 MOS. Relative to the starting branch, the final threshold improves
the mean by +0.00277 and +0.00228 MOS. The `Girl_In_The_Fire` loss against
master shrinks from −0.034/−0.026 to about −0.015/−0.009 MOS at 64/96 kbps.

## CI-style outliers

Selected cases from [#583's CI report](https://github.com/nschimme/faac/pull/583#issuecomment-5849330098)
were re-encoded with their ABR, CBR, or VBR settings against master. The final
low bitrate policy retains a compact per-channel grid and eliminated the severe
stereo coherence loss in `4-Sound-English-male` at 16 kbps: its ABR/CBR AAC
payload bytes equal master's, and coherence error differs by less than 0.00001.

| Mode | Cases | Mean MOS change | Worst MOS change | Mean AAC payload change | Mean stereo error change |
| --- | ---: | ---: | ---: | ---: | ---: |
| ABR | 5 | −0.0058 | −0.0177 | −0.012% | −0.0012 |
| CBR | 6 | −0.0007 | −0.0100 | −0.013% | −0.0007 |
| VBR | 2 | −0.0043 | −0.0051 | +0.148% | <0.0001 |

All 13 selected streams decoded with zero concealment. These cases do not
replace the full CI MOS gate; it still needs to run on the completed branch.

## Throughput

On the same ARM64 macOS machine, 12 alternating runs on each of the benchmark
suite's long percussive and tonal WAVs, forced HE-AAC v1 at 64 kbps, gave these
wall times per encode:

| Source | Mean | Median |
| --- | ---: | ---: |
| Master | 0.6152 s | 0.6024 s |
| Starting branch | 0.6188 s | 0.6120 s |
| Final candidate | 0.6158 s | 0.6023 s |

The candidate and master are indistinguishable at the observed timing noise;
the candidate's mean was 0.1% slower and its median was essentially equal.

The full 49-clip comparison and selected outlier measurements used the local
`faac-benchmark` checkout at `204b74e`; benchmark outputs and diagnostics are
under `.cache/grid-eval/` and `.cache/grid-ci/`.
