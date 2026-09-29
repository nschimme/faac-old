import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];names=('Severance','21classic','velvet','Greensleeves','German')
for name in names:
 for arm,target in (('F','KF_target'),('A','KA_target')):
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(name+'_G_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(name+'_G_'+arm+'_origin.bin')))
  out=root/(name+'_G_'+arm+'.m4a')
  with open(root/(name+'_G_'+arm+'.log'),'w') as log:subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'--overwrite','-b','128','-o',str(out),str(old/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  raw=root/(name+'_G_'+arm+'.aac')
  subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-a',str(raw),str(out)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
  goal=old/(name+'_'+target+'.aac')
  same=raw.read_bytes()==goal.read_bytes();print(name,arm,'PASS' if same else 'FAIL',raw.stat().st_size,goal.stat().st_size,flush=True)
  if not same:raise RuntimeError('control failed '+name+' '+arm)
