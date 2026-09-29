import os
import sys
import pathlib,os,subprocess,re,json,numpy as np,soundfile as sf
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));sc=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py');py=os.environ.get('PYTHON_BIN', sys.executable);clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'};scores=json.loads((root/'g1_scores.json').read_text())
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames
 for arm in ('SFt','SFs'):
  raw=root/(name+'_G_'+arm+'.f32');wav=root/(name+'_G_'+arm+'_aligned.wav');x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64;sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
  env=os.environ.copy();env['PYTHONPATH']=str(old);p=subprocess.run([py,sc,str(src),str(wav)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True);mos=float(re.search(r'MOS: ([\d.]+)',p.stdout).group(1));b=(root/(name+'_G_'+arm+'.m4a')).stat().st_size;scores[name][arm]={'mos':mos,'bytes':b};print(name,arm,mos,b,flush=True)
(root/'g1_scores.json').write_text(json.dumps(scores,indent=2))
