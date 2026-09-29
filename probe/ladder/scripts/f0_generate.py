import sys
import os,subprocess,wave,pathlib,sys
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'));sys.path.insert(0,'probe/ladder');import parse_dump as pd
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
for name,stem in clips.items():
 src=data/(stem+'.wav');dst=root/(name+'_plus.wav')
 if not dst.exists():
  with wave.open(str(src),'rb') as f:p=f.getparams();x=f.readframes(f.getnframes())
  with wave.open(str(dst),'wb') as f:f.setparams(p);f.writeframes(b'\0'*64*p.nchannels*p.sampwidth+x)
 out=root/(name+'_F0.m4a');dump=root/(name+'_F0.dump')
 if not out.exists():
  env=os.environ.copy();env.update(FAAC_STEP1=str(root / f'{name}_apple.bin'),FAAC_STEP1_OFFSET='1')
  with open(root/(name+'_F0.log'),'w') as log:subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'-b','128','-o',str(out),str(dst)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 if not dump.exists():
  env=os.environ.copy();env.update(FAAD_DUMP=str(dump),FAAD_LADDER_DUMP='1')
  with open(root/(name+'_F0_decode.log'),'w') as log:subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-q','-o',str(root/(name+'_F0_faad.wav')),str(out)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 s=pd.parse(str(dump));r=pd.parse(str(repo/'probe/ladder/survey/apple'/(stem+'.dump')));n=bad=0
 for sf,ch in s.items():
  for k,si in ch.items():
   ri=r.get(sf+1,{}).get(k)
   if ri is None:continue
   n+=1
   if (si.win_seq,si.window_shape,si.max_sfb,si.num_groups,si.group_len)!=(ri.win_seq,ri.window_shape,ri.max_sfb,ri.num_groups,ri.group_len):bad+=1
 print(name,'paired ICS',n,'unforced',bad,'bytes',out.stat().st_size,flush=True)
