import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for n in ('Severance','21classic','velvet','Greensleeves','German'):
 d=root/(n+'_KF.dump');env=os.environ.copy();env.update(FAAD_DUMP=str(d),FAAD_LADDER_DUMP='1')
 with open(root/(n+'_KF_decode.log'),'w') as log:subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-q','-o',str(root/(n+'_KF_faad.wav')),str(root/(n+'_KF.m4a'))],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print(n,'dump bytes',d.stat().st_size,flush=True)
