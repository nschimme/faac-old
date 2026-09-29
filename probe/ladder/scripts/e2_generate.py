import sys
import os,subprocess,wave,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work')); repo=pathlib.Path(__file__).resolve().parents[3]; data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'))
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
for name,stem in clips.items():
 src=data/(stem+'.wav'); dst=root/(name+'_plus.wav')
 if not dst.exists():
  with wave.open(str(src),'rb') as f: p=f.getparams(); x=f.readframes(f.getnframes())
  with wave.open(str(dst),'wb') as f: f.setparams(p);f.writeframes(b'\0'*64*p.nchannels*p.sampwidth+x)
 for arm in ('aligned','unshifted'):
  out=root/(name+'_'+arm+'_step1.m4a'); spec=root/(name+'_'+arm+'.spec')
  if not out.exists():
   env=os.environ.copy();env.update(FAAC_STEP1=str(root / f'{name}_apple.bin'),FAAC_STEP1_OFFSET='1',FAAC_STEP1_SPEC_DUMP=str(spec))
   with open(root/(name+'_'+arm+'_step1.log'),'w') as log: subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'-b','128','-o',str(out),str(dst if arm=='aligned' else src)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  chk=root/(name+'_'+arm+'.prequant')
  if not chk.exists():
   with open(chk,'w') as log: subprocess.run(['python3',str(repo/'probe/ladder/prequant_check.py'),str(spec),str(repo/'probe/ladder/survey/apple'/(stem+'.dump')),'2'],stdout=log,check=True)
  dump=root/(name+'_'+arm+'.dump')
  if not dump.exists():
   env=os.environ.copy();env['FAAD_LADDER_DUMP']='1'; env['FAAD_DUMP']=str(dump)
   subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-q','-o',str(root/(name+'_'+arm+'_faad.wav')),str(out)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
  lines=root/(name+'_'+arm+'.lines')
  if not lines.exists() or lines.stat().st_size == 0:
   with open(lines,'w') as log: subprocess.run(['python3',str(repo/'probe/ladder/line_level.py'),str(dump),str(repo/'probe/ladder/survey/apple'/(stem+'.dump')),'1'],stdout=log,check=True)
 print(name,'done',flush=True)
