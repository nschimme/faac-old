import os
import sys,pathlib,os,subprocess,re,math,numpy as np,soundfile as sf
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));sc=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py');py=os.environ.get('PYTHON_BIN', sys.executable)
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def score(src,deg):
 env=os.environ.copy();env['PYTHONPATH']=str(root);p=subprocess.run([py,sc,str(src),str(deg)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True);return float(re.search(r'MOS: ([\d.]+)',p.stdout).group(1))
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames;enc=root/(name+'_F0.m4a');raw=root/(name+'_F0.f32');wav=root/(name+'_F0_aligned.wav')
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(enc),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],check=True)
 x=np.fromfile(raw,dtype='<f4').reshape(-1,2);assert len(x)>=n+64;sf.write(wav,x[64:64+n],48000,subtype='FLOAT')
 items={'faac112':root/(name+'_faac112.m4a'),'faac128':root/(name+'_faac128.m4a'),'faac144':root/(name+'_faac144.m4a'),'apple':repo/'probe/ladder/ref/apple'/(stem+'.m4a'),'A':wav}
 for label,deg in items.items():print(name,label,score(src,deg),enc.stat().st_size if label=='A' else deg.stat().st_size,flush=True)
