import sys
import os,subprocess,pathlib,numpy as np
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3];data=pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'))
clips={'Severance':'Severance__1.31-1.51_.16b48k','21classic':'21-classic.441.16b48k','velvet':'velvet.16b48k','Greensleeves':'24-Greensleeves-Korean-male-speech.441.16b48k','German':'12-German-male-speech.441.16b48k'}
def run(cmd,**kw):subprocess.run(cmd,check=True,**kw)
def decode(aac,f32):run(['ffmpeg','-v','error','-y','-i',str(aac),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(f32)])
for name,stem in clips.items():
 for ref in ('apple','fdk'):
  step=root/(name+('_aligned' if ref=='apple' else '_fdk')+'_step1.m4a')
  dump=root/(name+('_aligned' if ref=='apple' else '_fdk')+'.dump')
  if ref=='fdk' and not step.exists():
   env=os.environ.copy();env.update(FAAC_STEP1=str(root / f'{name}_fdk.bin'),FAAC_STEP1_OFFSET='1')
   with open(root/(name+'_fdk_step1.log'),'w') as log:run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'-b','128','-o',str(step),str(data/(stem+'.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
   env=os.environ.copy();env.update(FAAD_LADDER_DUMP='1',FAAD_DUMP=str(dump))
   run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-q','-o',str(root/(name+'_fdk_faad.wav')),str(step)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  refdump=repo/'probe/ladder/survey'/ref/(stem+'.dump')
  own=root/(name+'_'+ref+'_own.aac')
  run([str(repo/'probe/ladder/reemit_tool'),f'{name}_{ref}.bin',str(own)],stderr=subprocess.DEVNULL)
  for arm in ('K0','K1'):
   prefix=root/(name+'_'+ref+'_'+arm)
   with open(str(prefix)+'.merge.log','w') as log:run(['python3',str(repo/'probe/ladder/hybrid_merge.py'),arm,str(dump),str(refdump),str(prefix)+'.bin'],stdout=log)
   run([str(repo/'probe/ladder/reemit_tool'),str(prefix)+'.bin',str(prefix)+'.aac'],stderr=subprocess.DEVNULL)
  k0=root/(name+'_'+ref+'_K0.aac'); k1=root/(name+'_'+ref+'_K1.aac')
  pass0=k0.read_bytes()==own.read_bytes()
  f1=root/(name+'_'+ref+'_K1.f32');fs=root/(name+'_'+ref+'_step1.f32');decode(k1,f1);decode(step,fs)
  a=np.fromfile(f1,dtype='<f4').reshape(-1,2)[2048:];b=np.fromfile(fs,dtype='<f4').reshape(-1,2);n=min(len(a),len(b));pass1=bool(np.array_equal(a[:n],b[:n]));extra=(len(a),len(b))
  print(name,ref,'K0',pass0,'K1',pass1,'samples',extra,'maxerr',float(np.max(np.abs(a[:n]-b[:n]))),'fallback',open(str(root/(name+'_'+ref+'_K1.merge.log'))).read().strip().split('fallback ICS=')[-1],flush=True)
  if not(pass0 and pass1):raise SystemExit('CONTROL FAIL')
