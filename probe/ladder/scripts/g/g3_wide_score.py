import sys
import os,subprocess,pathlib,re,json,numpy as np,soundfile as sf
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));sc=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py');py=os.environ.get('PYTHON_BIN', sys.executable);index=json.loads((root/'g2_index.json').read_text());prepared=json.loads((root/'g3_wide_prepare.json').read_text());scores={}
for item in index:
 k=item['id']
 if k not in prepared or not all(prepared[k][a] for a in ('SFt','SFs')):print(k,'SKIP',flush=True);continue
 src=pathlib.Path(item['src']);n=sf.info(src).frames;scores[k]={}
 for arm in ('SFt','SFs'):
  raw=root/(k+'_G_'+arm+'.f32');wav=root/(k+'_G_'+arm+'_aligned.wav');x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64;sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
  env=os.environ.copy();env['PYTHONPATH']=str(old);p=subprocess.run([py,sc,str(src),str(wav)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True);mos=float(re.search(r'MOS: ([\d.]+)',p.stdout).group(1));b=(root/(k+'_G_'+arm+'.m4a')).stat().st_size;scores[k][arm]={'mos':mos,'bytes':b};print(k,arm,mos,b,flush=True)
 (root/'g3_wide_scores.json').write_text(json.dumps(scores,indent=2))
