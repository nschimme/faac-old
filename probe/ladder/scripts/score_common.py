import sys
import os,subprocess,pathlib,numpy as np,soundfile as sf,re
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));venv=os.environ.get('PYTHON_BIN', sys.executable);scorer=os.environ.get('SCORE_CLIP', '/opt/faac-benchmark/scripts/score_clip.py')
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def run(cmd,**kw):return subprocess.run(cmd,check=True,**kw)
def wav_from_aac(aac,wav,skip,n):
 raw=wav.with_suffix('.f32');run(['ffmpeg','-v','error','-y','-i',str(aac),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)])
 x=np.fromfile(raw,dtype='<f4').reshape(-1,2)
 assert len(x)>=skip+n,(aac,len(x),skip,n)
 sf.write(str(wav),x[skip:skip+n],48000,subtype='FLOAT')
def score(src,deg):
 env=os.environ.copy();env['PYTHONPATH']=str(root)
 p=run([venv,scorer,str(src),str(deg)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 m=re.search(r'MOS: ([0-9.]+)',p.stdout)
 if not m:raise RuntimeError(p.stdout)
 return float(m.group(1))
