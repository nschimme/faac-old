import os
import sys,pathlib,os,subprocess,re,numpy as np,soundfile as sf,json
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));sc=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py');py=os.environ.get('PYTHON_BIN', sys.executable)
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def score(src,deg):
 env=os.environ.copy();env['PYTHONPATH']=str(root);p=subprocess.run([py,sc,str(src),str(deg)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
 if not m:raise RuntimeError(p.stdout)
 return float(m.group(1))
results={}
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames;results[name]={}
 for arm in ('A','WIN','BW','CLS','SF','MS','TNS','ALLF'):
  enc=root/(name+'_F2_'+arm+'.m4a');raw=root/(name+'_F2_'+arm+'.f32');wav=root/(name+'_F2_'+arm+'_aligned.wav')
  if not raw.exists():subprocess.run(['ffmpeg','-v','error','-y','-i',str(enc),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],check=True)
  x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64,(name,arm,len(x),n);sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
  mos=score(src,wav);results[name][arm]={'mos':mos,'bytes':enc.stat().st_size};print(name,arm,mos,enc.stat().st_size,flush=True)
 for rate in (112,128,144):
  enc=root/(name+f'_faac{rate}.m4a');mos=score(src,enc);results[name][f'faac{rate}']={'mos':mos,'bytes':enc.stat().st_size};print(name,f'faac{rate}',mos,enc.stat().st_size,flush=True)
 (root/'f2_scores.json').write_text(json.dumps(results,indent=2))
