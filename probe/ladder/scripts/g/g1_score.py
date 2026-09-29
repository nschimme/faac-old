import sys
import os,subprocess,pathlib,re,json,numpy as np,soundfile as sf
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));sc=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py');py=os.environ.get('PYTHON_BIN', sys.executable)
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
arms=('F','A','rWIN','rBW','rCLS','rSF','rMS','rTNS','SFt','SFs','rSF+rWIN','rSF+rBW','rSF+rCLS','rSF+rMS','rSF+rTNS','rWIN+rBW','rWIN+rCLS','rWIN+rMS','rWIN+rTNS','rMS+rCLS')
def score(src,deg):
 env=os.environ.copy();env['PYTHONPATH']=str(old);p=subprocess.run([py,sc,str(src),str(deg)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True);m=re.search(r'MOS: ([\d.]+)',p.stdout)
 if not m:raise RuntimeError(p.stdout)
 return float(m.group(1))
result={}
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames;result[name]={}
 for arm in arms+tuple('f'+x for x in ('WIN','BW','CLS','SF','MS','TNS')):
  enc=(root/(name+'_G_'+arm+'.m4a')) if not arm.startswith('f') else (old/(name+'_F2_'+arm[1:]+'.m4a'))
  raw=root/(name+'_G_'+arm+'.f32');wav=root/(name+'_G_'+arm+'_aligned.wav')
  if not raw.exists():subprocess.run(['ffmpeg','-v','error','-y','-i',str(enc),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],check=True)
  x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64,(name,arm,len(x),n);sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
  mos=score(src,wav);result[name][arm]={'mos':mos,'bytes':enc.stat().st_size};print(name,arm,mos,enc.stat().st_size,flush=True)
 for rate in (112,128,144):
  enc=root/(name+f'_faac{rate}.m4a');mos=score(src,enc);result[name][f'faac{rate}']={'mos':mos,'bytes':enc.stat().st_size};print(name,f'faac{rate}',mos,enc.stat().st_size,flush=True)
 enc=repo/'probe/ladder/ref/apple'/(stem+'.m4a');mos=score(src,enc);result[name]['apple']={'mos':mos,'bytes':enc.stat().st_size};print(name,'apple',mos,enc.stat().st_size,flush=True)
 (root/'g1_scores.json').write_text(json.dumps(result,indent=2))
