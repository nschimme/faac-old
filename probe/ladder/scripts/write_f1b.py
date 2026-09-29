import sys, os, pathlib
root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
import pathlib,re
p=pathlib.Path('probe/ladder/LADDER_RESULT.md');s=p.read_text();assert '### F1b: KF diagnosis' not in s
names=['Severance','21classic','velvet','Greensleeves','German'];r={};cur=None
for line in open(str(root / 'diff_ics.log')):
 m=re.match(r'^(\w+) ICS (\d+) bands (\d+) line_diff (\d+) (\d+) all_syntax_matched_line_diff (\d+) (\d+)',line)
 if m:
  cur=m[1];r[cur]={'ics':int(m[2]),'bands':int(m[3]),'line_diff':int(m[4]),'line_total':int(m[5]),'same_diff':int(m[6]),'same_total':int(m[7]),'components':{}};continue
 m=re.match(r'\s+(\w+) (\d+) (\d+) ([\d.]+%|NA) first (.*)',line)
 if m and cur:r[cur]['components'][m[1]]=(int(m[2]),int(m[3]),m[4],m[5])
out=[];a=out.append
a('\n### F1b: KF diagnosis\n')
a('**Measurements.** `python3 dump_kf.py` decoded the original KF streams with `FAAD_LADDER_DUMP=1 FAAD_DUMP=<path>`. `python3 diff_ics.py` compared those dumps to the FAAC-normal dumps from F1 at frame offset 0. The table reports the percent of paired ICS (window, gain, TNS, pulse, any quantized-line change) or paired bands (class, book, SF, M/S); PNS energy and IS position use only paired bands of the named class. Class means ZERO/regular/PNS/IS, while “book” compares the exact regular Huffman book too. Pulse is the dump’s pulse-present flag. All streams here are one CPE, element 0.\n')
a('| component | unit | Severance | 21classic | velvet | Greensleeves | German |\n|---|---|---:|---:|---:|---:|---:|')
for k,label,unit in [('win_seq','window sequence','ICS'),('shape','window shape','ICS'),('grouping','grouping','ICS'),('max_sfb','max_sfb','ICS'),('class','band class','bands'),('book','exact Huffman book','bands'),('sf','SF','bands'),('global_gain','global gain','ICS'),('ms_mask','M/S mask','bands'),('TNS','TNS','ICS'),('PNS_energy','PNS energy','PNS bands'),('IS_position','IS position','IS bands'),('pulse','pulse present','ICS'),('quant_ics','any q-line mismatch','ICS')]:
 a(f'| {label} | {unit} | '+' | '.join(r[n]['components'][k][2] for n in names)+' |')
a('\nThe same script counts quantized lines and, separately, lines in bands whose **entire recorded syntax** matched (window/shape/grouping, max_sfb, class, exact book, SF, global gain, M/S, TNS, pulse). Thus the latter differences cannot be explained by a different transmitted decision.\n')
a('| clip | paired ICS | paired bands | differing q lines / regular lines | differing q lines with all syntax matching | first differing dump frame / element / component |\n|---|---:|---:|---:|---:|---|')
first={'Severance':'1 / CPE ch0 / book and q','21classic':'34 / CPE ch0 / book and q','velvet':'1 / CPE ch0 / q','Greensleeves':'1 / CPE ch0 / q','German':'1 / CPE ch0 / q'}
for n in names:
 z=r[n];a(f'| {n} | {z["ics"]:,} | {z["bands"]:,} | {z["line_diff"]:,} / {z["line_total"]:,} ({100*z["line_diff"]/z["line_total"]:.3f}%) | {z["same_diff"]:,} / {z["same_total"]:,} ({100*z["same_diff"]/z["same_total"]:.3f}%) | {first[n]} |')
a('\n**Cause 1, measured and verified: intensity stereo changes the left spectrum and its quantizer bias.** `python3 q_context.py` found that 109/459, 624/798, 30,123/30,197, 24,834/24,984, and 11,827/12,864 original regular-line differences (clip order above) sat in the left channel with an IS right partner. `stereo.c::apply_is` replaces the left spectrum with a scaled sum/difference and sets `cl->sf[band]` to the left energy bias before `BlocQuant`; the old step1 applied neither. The self-reference probe now derives that transform and bias from the dumped IS decision. `python3 verify_is_bias.py` and `diff_is_bias.py` measured the staged reduction below. KA was byte/PCM exact on each staged rerun.\n')
a('| clip | original differing regular q lines | after self IS transform + SF bias | after M/S-ZERO fix |\n|---|---:|---:|---:|')
vals=[('Severance',459,350,0),('21classic',798,174,0),('velvet',30197,74,0),('Greensleeves',24984,150,0),('German',12864,1037,0)]
for n,x,y,z in vals:a(f'| {n} | {x:,} | {y:,} | {z} |')
a('\n**Cause 2, measured and verified: M/S happens before band zeroing.** `python3 remaining_context.py` found that every residual line after the IS-bias stage had M/S enabled and the opposite channel’s final book ZERO: 350, 174, 74, 150, and 1,037 lines. In `stereo.c`, `apply_ms_full` changes both spectra before `quantize.c::assign_band_codebooks` can zero one side. The previous `Step1ApplyMS` skipped such a band because it looked only at final regular books. In FAAC self mode it now applies M/S when one side was later zeroed, in either channel. `python3 verify_self_full.py` first cleared four clips; the remaining ten Severance lines were the mirror (right regular, left ZERO). Adding that case cleared Severance too.\n')
a('**Other candidates.** The initial dump comparison measured identical PNS energy, IS position, SF, global gain, TNS, pulse, and M/S mask. The two fixes above removed every decoded and ADTS difference without changing `BlocQuant`, PNS, the rate loop, or TNS. This rules those paths out as necessary causes of the observed KF mismatch on these five clips; it does not establish their behavior on other inputs.\n')
a('**Final controls: PASS.** `CCACHE_DISABLE=1 meson compile -C build-ladder` rebuilt the final code. `python3 verify_clean.py` ran KF with `FAAC_STEP1_SELF_IS=1` and FAAC’s own binary at offset 0, and KA with Apple’s binary at offset 1 and the self mode unset. It extracted ADTS and decoded stereo float PCM for all five clips. **KF and KA were byte-identical to their respective targets on all five; decoded PCM maximum absolute difference was 0 in every comparison.** The explicit self mode preserves the established Apple KA spectrum path. Since KF is exact, a separate KF MOS and byte-adjusted score was not needed. F2 was not run in this diagnosis task.\n')
p.write_text(s+'\n'.join(out)+'\n')
