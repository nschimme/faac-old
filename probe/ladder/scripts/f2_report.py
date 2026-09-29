import os
import sys
import json,ast,re,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));report=pathlib.Path('probe/ladder/LADDER_RESULT.md')
rows=json.loads((root/'f2_derived.json').read_text());names=['Severance','21classic','velvet','Greensleeves','German','Pooled','Clean'];arms=['A','WIN','BW','CLS','SF','MS','TNS','ALLF']
assert '### F2: single-decision swaps' not in report.read_text()
lines=[]
def add(s=''):lines.append(s)
add('### F2: single-decision swaps')
add()
add('**Measurement and controls.** `python3 f2_generate.py` merged the Apple dump at step1 offset +1 with the +64-input FAAC-normal dump at offset 0. `FAAC_STEP1_ORIGIN` is a per-band mask; `libfaac/step1.c` applies FAAC’s self-reference IS transform/SF bias and M/S-before-zeroing only to marked bands. For a CPE, either channel marking a band selects the FAAC stereo treatment for that band. A/no-swap retained Apple decisions; ALLF used FAAC decisions in every ICS. `CCACHE_DISABLE=1 meson compile -C build-ladder` rebuilt the probe, and `python3 f2_controls.py` extracted ADTS and compared it byte for byte. Both controls passed on **all five clips**: A = KA and ALLF = KF, byte-identical (thus decoded PCM exact). The controls were rerun after the final per-band change and before arm scoring.')
add()
add('| control | Severance | 21classic | velvet | Greensleeves | German |')
add('|---|---|---|---|---|---|')
add('| A/no-swap = KA ADTS | PASS | PASS | PASS | PASS | PASS |')
add('| ALLF/per-band = KF ADTS | PASS | PASS | PASS | PASS | PASS |')
add()
add('**Measurement.** `python3 f2_encode.py` encoded and ffmpeg-decoded each mixed arm: all 30 streams decoded with no ffmpeg error output. `python3 f2_score.py` decoded stereo float PCM, dropped the +64 input samples, cropped to the source length, and scored all arms and the FAAC 112/128/144 controls serially through `faac-benchmark/scripts/score_clip.py` (zimtohrli). Bytes are MP4 bytes. `python3 f2_derive.py` computed the per-clip slope `(MOS144−MOS112)/log2(bytes144/bytes112)` and adjusted Δ = `(MOS_arm−MOS_A) − slope·log2(bytes_arm/bytes_A)`. Share = `−adjusted Δ / −adjusted Δ_ALLF`; single-arm shares need not sum to 100% because decisions interact. Pooled and clean rows use mean MOS/Δ, summed bytes and units, and ratio of summed adjusted losses. `python3 f2_units.py` counted changed ICS and bands from aligned dumps; `f2_generate.py` counted changes in each isolated arm. A direct assertion checked all 40 clip-arm Δ signs against raw MOS and bytes. The units column reports changed window/TNS ICS for WIN/TNS, changed bands for BW/CLS/SF/MS, and both measures for ALLF; zero in the other unit does not mean the arm made no change.')
add()
add('| clip | arm | MOS | bytes | adj Δ vs A | gap share | units changed |')
add('|---|---|---:|---:|---:|---:|---:|')
for n in names:
 for arm in arms:
  r=next(x for x in rows if x['clip']==n and x['arm']==arm)
  delta=0 if abs(r['adj_delta'])<1e-12 else r['adj_delta'];share=0 if abs(r['share'])<0.05 else r['share']
  add(f"| {n} | {arm} | {r['mos']:.4f} | {r['bytes']:,} | {delta:+.5f} | {share:+.1f}% | {r['ics']:,} ICS / {r['bands']:,} bands |")
add()
add('**Decision statistics, measured.** `python3 f2_stats.py` compared each Apple frame `n+1` with FAAC frame `n` on the same +64 grid. Short/TNS percentages use ICS; class and M/S percentages use coded bands. `max_sfb` and coded bandwidth are means across ICS; bandwidth converts the SFB edge using the 48-kHz tables in `probe/ladder/line_level.py`. Region SF means include regular bands only. A short window has a different SFB scale, so bandwidth is the comparable coverage measure.')
add()
add('| clip | ref | short % | mean max_sfb | mean edge kHz | ZERO / REG / PNS / IS % | SF 0–2 / 2–6 / 6–12 / >12 kHz | M/S % | TNS % |')
add('|---|---|---:|---:|---:|---|---|---:|---:|')
for l in (root/'f2_stats.txt').read_text().splitlines():
 m=re.match(r'^(\S+) (Apple|FAAC) ICS (\d+) short% ([\d.]+) max_sfb ([\d.]+) bandwidth_Hz (\d+) class% (\{.*?\}) sf (\{.*?\}) MS% ([\d.]+) TNS% ([\d.]+)$',l)
 if not m:continue
 n,ref,ics,short,maxsf,bw,cls,sf,ms,tns=m.groups();cls=ast.literal_eval(cls);sf=ast.literal_eval(sf)
 cl='/'.join(f'{cls[k]:.1f}' for k in ('ZERO','REG','PNS','IS'));ss='/'.join(f'{sf[k]:.1f}' if sf[k] is not None else '—' for k in ('0-2k','2-6k','6-12k','>12k'))
 add(f'| {n} | {ref} | {short} | {maxsf} | {int(bw)/1000:.2f} | {cl} | {ss} | {ms} | {tns} |')
add()
add('**Dominant-arm characterization, measured.** The pooled M/S swap loses 108.1% of the adjusted A→ALLF gap; on clean clips, the SF swap loses 120.2%; velvet’s WIN swap loses 97.3%. These are isolated-swap effects, not additive attribution. `python3 f2_character.py` counted M/S disagreement in the *same-layout* subset by region and signal clip. The table gives common bands and percent with differing M/S masks; unlike the overall decision table, it excludes frames with different windows/grouping.')
add()
add('| clip / signal | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |')
add('|---|---:|---:|---:|---:|')
for l in (root/'f2_character.txt').read_text().splitlines():
 if ' MS regions ' not in l:continue
 n,raw=l.split(' MS regions ',1);d=ast.literal_eval(raw);label={'Severance':'Severance / clean','21classic':'21classic / clean','velvet':'velvet / transient','Greensleeves':'Greensleeves / speech','German':'German / speech'}[n]
 vals=[]
 for r in ('0-2k','2-6k','6-12k','>12k'):
  nband,apple,faac,chg=d[r];vals.append(f'{chg:.1f}% ({nband:,})')
 add(f'| {label} | '+ ' | '.join(vals)+' |')
add()
add('In the matched-layout subset, `f2_character.py` also found the frequent clean-clip M/S disagreements in regular/regular long-window bands below 6 kHz, while >12 kHz includes many Apple ZERO/FAAC PNS pairs. Velvet’s disagreements are predominantly short-window regular/regular or regular/IS pairs; the two speech clips have many regular/IS and regular/ZERO mismatches. The overall M/S shares in the decision table cover all frames, including the unmatched-window majority of the three non-clean clips.')
add()
add('**Clean-clip SF characterization, measured.** `python3 f2_stats.py` compared FAAC minus Apple SF in bands regular in both references and with identical windows/grouping. Entries are differing-band %, mean signed SF difference, and mean absolute SF difference; SF retains Apple global gain in the isolated arm.')
add()
add('| clip | 0–2 kHz | 2–6 kHz | 6–12 kHz | >12 kHz |')
add('|---|---:|---:|---:|---:|')
for l in (root/'f2_stats.txt').read_text().splitlines():
 if ' SF-diff ' not in l:continue
 n,raw=l.split(' SF-diff ',1)
 if n not in ('Severance','21classic'):continue
 d=ast.literal_eval(raw);vals=[]
 for r in ('0-2k','2-6k','6-12k','>12k'):
  count,pct,signed,absolute=d[r];vals.append(f'{pct:.1f}% / {signed:+.2f} / {absolute:.2f} (n={count:,})')
 add(f'| {n} | '+' | '.join(vals)+' |')
add()
add('**Inference.** The clean-clip lead is strongly associated with SF decisions on this grid; velvet’s loss is strongly associated with window decisions. The pooled M/S arm is dominated by velvet and an especially large German isolated loss even though German ALLF is close to A. The German 1,809.5% share is evidence of interaction with the other FAAC decisions, so it is not a standalone estimate of M/S’s contribution in the fully FAAC stream. No encoder fix is proposed here.')
with report.open('a') as f:f.write('\n'+'\n'.join(lines)+'\n')
print('appended',len(lines),'lines')
