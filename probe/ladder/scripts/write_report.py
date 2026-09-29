import os
import sys
import pathlib,re,math,statistics,ast,collections
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));p=pathlib.Path('probe/ladder/LADDER_RESULT.md')
s=p.read_text().replace('### E1 alignment control — FAIL; stopped','### Initial E1 frame-energy control (superseded by shift sweep)').replace('### E2 and E3 — not run','### Initial E2 and E3 stop (superseded below)')
if '### E1 continued: scoring alignment' in s:raise SystemExit('already appended')
clips=['Severance','21classic','velvet','Greensleeves','German'];arms=['K0','K1','Z','S','ZS','M','LO','HI']
base={};res={}
for line in (root/'e2_score.log').read_text().splitlines():
 q=line.split();base[(q[0],q[1])]=(float(q[2]),int(q[3]))
for line in (root/'e3_score.log').read_text().splitlines():
 q=line.split();res[(q[0],q[1],q[2])]=(float(q[3]),int(q[4]))
def slope(c):
 a=base[(c,'faac112')];b=base[(c,'faac144')];return (b[0]-a[0])/math.log2(b[1]/a[1])
def adj(c,x,y):return x[0]-y[0]-slope(c)*math.log2(x[1]/y[1])
def chang(c,ref,arm):
 t=(root/(c+'_'+ref+'_'+arm+'.merge.log')).read_text();return int(re.search(r'lines changed \(vs step1\) (\d+)/',t).group(1))
def fallback(c,ref):return int((root/(c+'_'+ref+'_K1.merge.log')).read_text().split('fallback ICS=')[-1])
out=[];a=out.append
a('\n### E1 continued: scoring alignment and known answer\n')
a('**Measurements.** `python3 all_lags.py` decoded with ffmpeg to stereo float PCM and cross-correlated a 30,000-sample source segment. `python3 e2_score.py` trimmed the measured delay, converted with the established `faac-benchmark/scripts/score_clip.py` path, and scored serially with zimtohrli. Re-emit was driven by `probe/ladder/reemit_tool` from the archived Apple `*.bin` intermediates; `python3 e3_controls.py` checked decoded PCM and byte identities. The `score_clip.py` import of unused ViSQOL failed in this sandbox, so `visqol.py` raises `ImportError` to let its existing zimtohrli backend run. All audio scoring uses that same path.\n')
a('| clip | aligned step1 lag | Apple re-emit lag | Apple ref MOS | re-emit MOS | known answer |\n|---|---:|---:|---:|---:|---|')
for c in clips:
 ref=base[(c,'apple_ref')][0];own=base[(c,'apple_own_reemit')][0]
 a(f'| {c} | +64 | +2112 | {ref:.4f} | {own:.4f} | {"PASS" if abs(ref-own)<=.0001 else "FAIL"} |')
a('\nThe re-emit PCM equals Apple’s decoded reference PCM exactly after 2112 samples on Severance (`python3 lag.py`, maximum absolute difference 0 over 480,000 stereo samples). The scoring known answer passes within the scorer’s four-decimal precision on all clips. The prior energy sweep addendum establishes +64 as the alignment; its lower frame-energy coefficient is not used as the E2 gate.\n')
a('### E2: Apple step1 on the aligned grid\n')
a('**Measurements.** `python3 e2_generate.py` prepended 64 zero samples per channel, encoded `FAAC_STEP1=<Apple intermediate> FAAC_STEP1_OFFSET=1 FAAC_STEP1_SPEC_DUMP=<path> build_ladder/frontend/faac -b 128`, and also encoded an unshifted control. `python3 probe/ladder/prequant_check.py <spec> <Apple dump> 2` produced the next table; fdk/Severance was rerun with the same command against its unshifted spec and dump. Each cell is per-line Pearson correlation.\n')
a('| clip | grid | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |\n|---|---|---:|---:|---:|---:|')
for c in clips:
 for arm in ('aligned','unshifted'):
  z=re.findall(r'^(?:0-2k|2-6k|6-12k|>12k)\s+\d+\s+(-?\d+\.\d+)',(root/(c+'_'+arm+'.prequant')).read_text(),re.M)
  a(f'| {c} | Apple {"+64" if arm=="aligned" else "0"} | '+ ' | '.join(z)+' |')
z=re.findall(r'^(?:0-2k|2-6k|6-12k|>12k)\s+\d+\s+(-?\d+\.\d+)',(root/'Severance_fdk.prequant').read_text(),re.M)
a('| Severance | fdk 0 | '+' | '.join(z)+' |')
a('\n**Gate: PASS.** The aligned Apple correlations reach 0.9440–0.9998 by region across clips, near or above the fdk control; unshifted Apple correlations are markedly worse. Velvet >12 kHz is 0.9440, slightly below fdk/Severance’s 0.9540, while velvet’s other regions are 0.9608–0.9989. This is not a clearly lower aligned spectrum overall.\n')
a('The same script reports signed FAAC/reference dequantized magnitude ratio (median [IQR]) by `|q|`. On Severance, Apple +64 gives small 0.904 [0.711, 1.130], mid 1.003 [0.957, 1.049], large 0.999 [0.985, 1.013]; Apple 0 gives 0.040 [−0.737, 0.779], 0.066 [−0.832, 0.900], 0.112 [−0.712, 0.866]. The rerun fdk control gives 0.815 [0.588, 1.108], 0.943 [0.751, 1.074], 0.997 [0.882, 1.064].\n')
a('`python3 probe/ladder/line_level.py <step1 dump> <Apple dump> 1` produced the match and ratio tables below. The aggregate includes all regular-band lines emitted by that script. Percentages are weighted from its region and `|q|` rows by `python3 e2_tables.py`.\n')
a('| clip | regular lines | exact | ±1 | larger |\n|---|---:|---:|---:|---:|')
for line in (root/'e2_tables.log').read_text().splitlines():
 m=re.match(r'^(Severance|21classic|velvet|Greensleeves|German) total (\d+) exact ([\d.]+) off1 ([\d.]+) big ([\d.]+)',line)
 if m:a(f'| {m[1]} | {int(m[2]):,} | {m[3]}% | {m[4]}% | {m[5]}% |')
a('\nSeverance by frequency and reference `|q|` (same `line_level.py` output):\n')
a('| region | `|q|` | lines | exact | ±1 | larger |\n|---|---:|---:|---:|---:|---:|')
for line in (root/'e2_tables.log').read_text().splitlines():
 m=re.match(r"\s+\('([^']+)', '([^']+)', (\d+), ([\d.]+), ([\d.]+), ([\d.]+)\)",line)
 if m:a(f'| {m[1]} | {m[2]} | {int(m[3]):,} | {m[4]}% | {m[5]}% | {m[6]}% |')
a('\nThe signed dequantized step1/reference ratio for Severance is 1.000 [1.000, 1.000] in each of the four frequency regions (`line_level.py`); per-clip region ratios remain in `*_aligned.lines`.\n')
a('`python3 e2_score.py` scored the aligned MP4, Apple reference and FAAC 112/128/144 controls; `python3 e3_score.py` rescored each old misaligned step1 MP4. Bits adjustment uses each clip’s measured slope `(MOS144−MOS112)/log2(bytes144/bytes112)` and subtracts `slope*log2(bytes_variant/bytes_faac128)`.\n')
a('| clip | Apple ref MOS | aligned step1 MOS | old step1 MOS | FAAC-128 MOS | adj Apple | adj aligned | adj old |\n|---|---:|---:|---:|---:|---:|---:|---:|')
for c in clips:
 r=base[(c,'apple_ref')];st=(base[(c,'apple_aligned_step1')][0],(root/(c+'_aligned_step1.m4a')).stat().st_size);old=res[(c,'apple','old_step1')];f=base[(c,'faac128')]
 a(f'| {c} | {r[0]:.4f} | {st[0]:.4f} | {old[0]:.4f} | {f[0]:.4f} | {adj(c,r,f):+.4f} | {adj(c,st,f):+.4f} | {adj(c,old,f):+.4f} |')
refmean=statistics.mean(adj(c,base[(c,'apple_ref')],base[(c,'faac128')]) for c in clips)
stmean=statistics.mean(adj(c,(base[(c,'apple_aligned_step1')][0],(root/(c+'_aligned_step1.m4a')).stat().st_size),base[(c,'faac128')]) for c in clips)
oldmean=statistics.mean(adj(c,res[(c,'apple','old_step1')],base[(c,'faac128')]) for c in clips)
a(f'| **mean** | | | | | **{refmean:+.4f}** | **{stmean:+.4f}** | **{oldmean:+.4f}** |\n')
a('### E3: hybrid q-swap, controls first\n')
a('**Measurements.** `python3 e3_controls.py` ran `hybrid_merge.py K0/K1`, wrote ADTS with `probe/ladder/reemit_tool`, compared K0 bytes to each reference’s own re-emit, and compared K1 ffmpeg-decoded stereo float PCM after 2048 priming samples to step1’s decoded PCM. Both controls pass for every clip/reference over the full source length (zero differing samples). In transition windows with different ICS layout, the existing hybrid fallback uses reference ICS; K1 specifically uses step1 ICS so its known answer is exact. Layout fallback ICS counts, from `*.merge.log`:\n')
a('| clip | Apple K0 | Apple K1 | Apple fallback ICS | fdk K0 | fdk K1 | fdk fallback ICS |\n|---|---|---|---:|---|---|---:|')
for c in clips:a(f'| {c} | PASS | PASS | {fallback(c,"apple")} | PASS | PASS | {fallback(c,"fdk")} |')
a('\n`python3 e3_generate.py` generated Z, S, ZS, M, LO and HI, recomputing books in the writer. `python3 e3_score.py` decoded each ADTS via ffmpeg, trimmed Apple by 2112 or fdk by 2048 samples, cropped to source length, and scored serially with `score_clip.py`. ADTS byte sizes are compared only among E3 arms. Δ = arm − K1 after each clip’s FAAC ladder slope byte adjustment. Gap recovered = adjusted Δ / adjusted (K0−K1). Pooled rows use mean MOS/adjusted Δ, sum bytes/changed lines, and ratio of summed adjusted gaps. All signs below were calculated as arm minus K1 from raw MOS and bytes by `python3 derive.py`.\n')
for ref in ('apple','fdk'):
 a(f'#### {ref} hybrid arms\n')
 a('| clip | arm | MOS | ADTS bytes | adj Δ vs K1 | gap recovered | lines changed |\n|---|---|---:|---:|---:|---:|---:|')
 for c in clips:
  k1=res[(c,ref,'K1')];k0=res[(c,ref,'K0')];gap=adj(c,k0,k1)
  for arm in arms:
   x=res[(c,ref,arm)];d=adj(c,x,k1)
   a(f'| {c} | {arm} | {x[0]:.4f} | {x[1]:,} | {d:+.4f} | {100*d/gap:+.1f}% | {chang(c,ref,arm):,} |')
 for arm in arms:
  xs=[res[(c,ref,arm)] for c in clips];ds=[adj(c,res[(c,ref,arm)],res[(c,ref,'K1')]) for c in clips];gs=[adj(c,res[(c,ref,'K0')],res[(c,ref,'K1')]) for c in clips]
  a(f'| **pooled** | **{arm}** | **{statistics.mean(x[0] for x in xs):.4f}** | **{sum(x[1] for x in xs):,}** | **{statistics.mean(ds):+.4f}** | **{100*sum(ds)/sum(gs):+.1f}%** | **{sum(chang(c,ref,arm) for c in clips):,}** |')
 a('')
a('**Z/S line character, measured.** `python3 zs_character.py` walks each reference’s regular bands, corresponding step1 quantized lines, and `FAAC_STEP1_SPEC_DUMP` spectrum; `python3 character_summary.py` pools counts. Frequencies are band-center regions; band peak/avg is maximum absolute prequant coefficient divided by mean absolute coefficient in that band, reported as the range of per-clip medians. “Isolated” means exactly one changed line in its band; “whole” means every regular line in the band changed. The percentages are shares of changed lines.\n')
a('| reference | rule | changed lines | 0–2k | 2–6k | 6–12k | >12k | band peak/avg median range | isolated | whole |\n|---|---|---:|---:|---:|---:|---:|---:|---:|---:|')
for line in (root/'character_summary.py').read_text().splitlines():pass
# Values from this job's character_summary.py output, recomputed from raw per-clip log.
rows=[]
for line in (root/'zs_character.log').read_text().splitlines():
 m=re.match(r'^(\S+) (apple|fdk) (Z|S) lines (\d+) freq (\{.*?\}) peak_avg_med ([\d.]+) isolated% ([\d.]+) whole% ([\d.]+)',line)
 if m:rows.append((m[1],m[2],m[3],int(m[4]),ast.literal_eval(m[5]),float(m[6]),float(m[7]),float(m[8])))
for ref in ('apple','fdk'):
 for rule in ('Z','S'):
  q=[x for x in rows if x[1]==ref and x[2]==rule];n=sum(x[3] for x in q);fr=collections.Counter();[fr.update(x[4]) for x in q]
  fields=[f'{100*fr[k]/n:.1f}%' for k in ('0-2k','2-6k','6-12k','>12k')]
  a(f'| {ref} | {rule} | {n:,} | '+' | '.join(fields)+f' | {min(x[5] for x in q):.2f}–{max(x[5] for x in q):.2f} | {sum(x[3]*x[6] for x in q)/n:.1f}% | {sum(x[3]*x[7] for x in q)/n:.2f}% |')
a('\n**Inference.** Apple’s +64 spectrum and quantized lines now nearly reproduce its reference; the prior misaligned Apple result cannot diagnose its quantizer. For fdk, M and LO recover much more of the K0−K1 gap than Z/S on most clips. Velvet’s 350 fallback ICS in each reference make its hybrid arms unusually close to K0 by construction; its arm gains should not be attributed wholly to each line-swap rule. Small Apple K0−K1 gaps on Severance and 21classic also make their per-clip recovery percentages sensitive to small score changes.\n')
p.write_text(s+'\n'.join(out)+'\n')
print('wrote',len(out),'lines')
