import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3]
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 for arm,target in [('A','KA_target'),('ALLF','KF_target')]:
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(name+'_F2_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(name+'_F2_'+arm+'_origin.bin')))
  out=root/(name+'_F2_'+arm+'.m4a')
  with open(root/(name+'_F2_'+arm+'.log'),'w') as log: subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'--overwrite','-b','128','-o',str(out),str(root/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  raw=root/(name+'_F2_'+arm+'.aac')
  subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-a',str(raw),str(out)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
  goal=root/(name+'_'+target+'.aac')
  print(name,arm,'byte_equal',raw.read_bytes()==goal.read_bytes(),'bytes',raw.stat().st_size,goal.stat().st_size,flush=True)
