import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for n in ('Severance','21classic','velvet','Greensleeves','German'):
 env=os.environ.copy();env.update(FAAD_DUMP=str(root/(n+'_KF_IS.dump')),FAAD_LADDER_DUMP='1')
 subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-q','-o',str(root/(n+'_KF_IS_faad.wav')),str(root/(n+'_KF_IS.m4a'))],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
 print(n,flush=True)
