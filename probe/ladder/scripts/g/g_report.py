import os
import sys
import json,math,statistics,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));report=pathlib.Path('probe/ladder/LADDER_RESULT.md')
assert '## Stage G: reverse arms and 49-clip check' not in report.read_text()
g1=json.loads((root/'g1_derived.json').read_text());g2=json.loads((root/'g2_derived.json').read_text());g3=json.loads((root/'g3_derived.json').read_text());scores=json.loads((root/'g2_scores.json').read_text());controls=json.loads((root/'g2_controls.json').read_text());enc=json.loads((root/'g2_encode.json').read_text());index=json.loads((root/'g2_index.json').read_text())
lines=[]
def add(s=''):lines.append(s)
add('## Stage G: reverse arms and 49-clip check');add()
add('### Controls and scoring method');add()
add('**Measurement.** `python3 g_make.py` generated the five-clip reverse arms from Apple frame `n+1` and FAAC frame `n`; `FAAC_STEP1_ORIGIN` marks the bands retaining FAAC decisions. Apple-origin bands use the KA pre-quantization path, while FAAC-origin bands use the verified KF path. `python3 g1_controls.py` compared extracted ADTS byte for byte: F = KF and A/no-swap = KA on all five clips. `python3 g1_encode.py` encoded and ffmpeg-decoded all reverse, pair, and SF-decomposition arms cleanly. `python3 g2_controls.py` repeated A = KA and F = normal FAAC as byte-level ADTS controls on every +64-input clip, then checked A/F decoding. `python3 g2_encode.py` checked the four wider SF/WIN arms and Apple/112/128/144 references with ffmpeg. Every one of the 49 clips passed both byte controls and all decode checks; none was excluded from scoring.')
add();add('| control | five G1 clips | 49 G2 clips |');add('|---|---:|---:|');add('| F = KF ADTS, byte-identical | 5/5 PASS | 49/49 PASS |');add('| A = KA ADTS, byte-identical | 5/5 PASS | 49/49 PASS |');add('| A/F and scored arms decode cleanly | PASS | 49/49 PASS |');add('| Apple and rate-control references decode cleanly | checked by serial scorer | 49/49 PASS |');add()
add('**Measurement.** `python3 g1_score.py`, `g3_score.py`, `g2_score.py`, and `g3_wide_score.py` used the F2 scorer (`faac-benchmark/scripts/score_clip.py`, zimtohrli) serially. For +64-input arms, ffmpeg decoded stereo float PCM, removed 64 samples, and cropped to source length. Bytes below are MP4 sizes. Per clip, `slope=(MOS144−MOS112)/log2(bytes144/bytes112)`; adjusted Δ between X and Y is `(MOS_X−MOS_Y)−slope·log2(bytes_X/bytes_Y)`. All derived signs were checked against raw MOS and bytes by `g1_derive.py`, `g2_derive.py`, and `g3_derive.py`. Positive reverse Δ means the Apple decision helps inside F.')
add();add('### G1: reverse single-decision arms');add()
add('**Measurement.** `python3 g1_derive.py` calculated adjusted Δ versus F and the percent of the adjusted A−F gap. Units are changed window/TNS ICS for WIN/TNS, changed ICS/bands for BW, and changed bands for CLS/SF/MS; A is the all-Apple endpoint. Pooled and clean rows have mean MOS/Δ, summed bytes/units, and a ratio of summed gains to summed gaps.')
add();add('| clip | arm | MOS | bytes | adj Δ vs F | gap share | units changed |');add('|---|---|---:|---:|---:|---:|---|')
for clip in ('Severance','21classic','velvet','Greensleeves','German','Pooled','Clean'):
 for arm in ('F','rWIN','rBW','rCLS','rSF','rMS','rTNS','A'):
  r=next(x for x in g1['arms'] if x['clip']==clip and x['arm']==arm);units='—' if arm=='A' else f"{r['ics']:,} ICS / {r['bands']:,} bands"
  add(f"| {clip} | {arm} | {r['mos']:.4f} | {r['bytes']:,} | {r['delta']:+.5f} | {r['share']:+.1f}% | {units} |")
add();add('### Forward/reverse interaction');add()
add('**Measurement.** `python3 g1_score.py` rescored the six archived F2 forward-arm streams in this job. `g1_derive.py` sets forward loss = adjusted `A−fX`, reverse gain = adjusted `rX−F`, and sum share = `(forward loss+reverse gain)/(A−F)`. “Separable” means positive gain within 70–130% of positive forward loss; “needs partner” means positive forward loss with gain below 70%; “compensated” means reverse gain below zero. Near-zero effects should not be interpreted from the verdict alone.')
add();add('| scope | decision | forward loss | reverse gain | sum / A−F | verdict |');add('|---|---|---:|---:|---:|---|')
for clip in ('Severance','21classic','velvet','Greensleeves','German','Pooled','Clean'):
 for x in ('WIN','BW','CLS','SF','MS','TNS'):
  r=next(z for z in g1['interaction'] if z['clip']==clip and z['decision']==x)
  add(f"| {clip} | {x} | {r['forward_loss']:+.5f} | {r['reverse_gain']:+.5f} | {r['sum_share']:+.1f}% | {r['verdict']} |")
add();add('**Measurement, reverse pair arms.** `g_make.py`, `g1_encode.py`, `g1_score.py`, and `g1_derive.py` tested SF+every other decision, WIN+every other decision, and the requested MS+CLS coupling (MS+SF is SF+MS). Values are adjusted gain versus F; pair gains are not expected to equal sums of single gains.')
add();add('| scope | pair arm | adj gain vs F | A−F share |');add('|---|---|---:|---:|')
for clip in ('Severance','21classic','velvet','Greensleeves','German','Pooled','Clean'):
 for arm in ('rSF+rWIN','rSF+rBW','rSF+rCLS','rSF+rMS','rSF+rTNS','rWIN+rBW','rWIN+rCLS','rWIN+rMS','rWIN+rTNS','rMS+rCLS'):
  r=next(z for z in g1['arms'] if z['clip']==clip and z['arm']==arm);add(f"| {clip} | {arm} | {r['delta']:+.5f} | {r['share']:+.1f}% |")
add();add('**Inference.** WIN is separable on the pooled set and on velvet. SF needs partners: on the clean pair, rSF recovers 66.3% of A−F, while rSF+rMS reaches 84.7%. Pooled rSF+rWIN reaches 87.8%. M/S alone is compensated in reverse; rMS+rCLS improves its pooled gain from −21.8% to −8.8% of the gap, but does not make it positive. These are contextual, non-additive swaps; no encoder fix is proposed.')
add();add('### G2: 49-clip SF/WIN check');add()
add('**Measurement.** `python3 g2_prepare.py` made +64 WAVs, dumped both bitstreams with `FAAD_LADDER_DUMP`, and encoded the 112/128/144 FAAC controls. `g2_make.py`, `g2_controls.py`, and `g2_encode.py` produced and gated A, F, forward SF/WIN and reverse SF/WIN for all 49 clips. `g2_score.py` scored them serially; `g2_derive.py` computed the adjusted Δ and sorted the worst five by Δ. Forward arms and F compare with A; reverse arms compare with F. A-versus-Apple directly tests the stage premise with the same bits adjustment. Wins/losses are positive/negative adjusted Δ.')
add();add('| arm / comparison | clips | mean adj Δ | median adj Δ | wins | losses | ties |');add('|---|---:|---:|---:|---:|---:|---:|')
for arm in ('F_vs_A','fSF','fWIN','rSF','rWIN','A_vs_Apple'):
 r=[x for x in g2 if x['arm']==arm];v=[x['delta'] for x in r]
 add(f"| {arm} | {len(v)} | {statistics.mean(v):+.5f} | {statistics.median(v):+.5f} | {sum(x>0 for x in v)} | {sum(x<0 for x in v)} | {sum(x==0 for x in v)} |")
add();add('**Measurement: worst five by adjusted Δ.** `python3 g2_derive.py`; names are source stems, and negative values mean the row’s arm lost to its comparison base.');add();add('| arm | five lowest clips (adj Δ) |');add('|---|---|')
for arm in ('F_vs_A','fSF','fWIN','rSF','rWIN','A_vs_Apple'):
 r=sorted((x for x in g2 if x['arm']==arm),key=lambda x:x['delta'])[:5];add('| '+arm+' | '+'; '.join(f"{x['stem']} ({x['delta']:+.4f})" for x in r)+' |')
close=sum(abs(x['delta'])<=0.02 for x in g2 if x['arm']=='A_vs_Apple');add();add(f'**Stage-premise measurement.** A is within ±0.02 adjusted MOS of Apple on {close}/49 clips (`g2_derive.py`). The mean and median A−Apple differences are in the table; this quantifies how far the all-Apple-decision step1 remains from Apple on the wider sample.')
add();add('### G3: SF regional allocation versus shape');add()
add('**Measurement.** `python3 g_make.py` and `g3_fix.py` built SFt (A plus rounded per-frame/channel/region FAAC−Apple mean SF offset) and SFs (FAAC SF minus that offset, retaining Apple’s regional allocation) for bands regular in both references with matching windows. Apple’s global gain is retained. `g3_score.py` scored the five clips serially; `g3_derive.py` compared each arm to A after byte adjustment and divided its A-relative loss by the forward SF arm’s loss. The regions are 0–2, 2–6, 6–12, and >12 kHz.')
add();add('| clip | arm | MOS | bytes | adj Δ vs A | SF-loss share |');add('|---|---|---:|---:|---:|---:|')
for clip in ('Severance','21classic','velvet','Greensleeves','German','Pooled','Clean'):
 for arm in ('SFt','SFs'):
  r=next(x for x in g3 if x['clip']==clip and x['arm']==arm);add(f"| {clip} | {arm} | {r['mos']:.4f} | {r['bytes']:,} | {r['delta_vs_A']:+.5f} | {r['share_SF_loss']:+.1f}% |")
if (root/'g3_wide_derived.json').exists():
 wide=json.loads((root/'g3_wide_derived.json').read_text());add();add('**Measurement, 49 clips.** `g3_wide_prepare.py` generated and decode-checked SFt/SFs on every G2-passing clip; `g3_wide_score.py` scored them serially and `g3_derive.py` verified their adjusted signs and pooled SF-loss shares.');add();add('| arm | clips | mean adj Δ vs A | median adj Δ | share of forward SF loss |');add('|---|---:|---:|---:|---:|')
 for arm in ('SFt','SFs'):
  r=[x for x in wide if x['arm']==arm];add(f"| {arm} | {len(r)} | {statistics.mean(x['delta_vs_A'] for x in r):+.5f} | {statistics.median(x['delta_vs_A'] for x in r):+.5f} | {-100*sum(x['delta_vs_A'] for x in r)/sum(x['sf_loss'] for x in r):+.1f}% |")
add();add('**Measured feature correlations.** `FAAC_STEP1_SPEC_DUMP` exposed the A-path FAAC MDCT spectrum; the probe-only `FAAC_G_MASK_DUMP` in `libfaac/quantize.c` exposed the normal quantizer’s band energy, peak energy and masking target. `python3 g3_dumps.py` verified that both dumps left the relevant ADTS streams byte-identical. `g3_features.py` correlated `(FAAC SF−Apple SF−rounded regional offset)` with these features in regular, same-layout bands. The masking target is FAAC’s quantizer target, not a separately measured perceptual threshold. Pearson and Spearman correlations are pooled over bands.')
add();add('| subset | feature | bands | Pearson r | Spearman ρ |');add('|---|---|---:|---:|---:|')
featlog=(root/'g3_features.log').read_text().splitlines();group=None
for ln in featlog:
 if ln.startswith('GROUP '):group=ln[6:];continue
 t=ln.split()
 if group and len(t)==4 and t[0] in ('relative_neighbor_energy','peak_avg','flatness','log_normal_peak_energy','log_normal_band_energy','log_mask_target','log_energy'):
  add(f'| {group} | {t[0]} | {int(t[1]):,} | {float(t[2]):+.4f} | {float(t[3]):+.4f} |')
add();add('**Inference.** On the clean pair, neither regional allocation (61.8%) nor shape (67.4%) reaches the 70% rule, so both matter. Across the five clips, SFs reaches 73.0%, but the small SF denominator on velvet and the speech clips makes their shares unstable. Relative neighboring-band energy and peak/average are the strongest measured correlates of the shape residual; correlation alone does not identify a causal encoder change.')
with report.open('a') as f:f.write('\n'+'\n'.join(lines)+'\n')
print('appended',len(lines),'lines')
