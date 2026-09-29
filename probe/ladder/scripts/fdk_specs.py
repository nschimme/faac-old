import os
from score_common import *
for name,stem in clips.items():
 env=os.environ.copy();env.update(FAAC_STEP1=str(root / f'{name}_fdk.bin'),FAAC_STEP1_OFFSET='1',FAAC_STEP1_SPEC_DUMP=str(root/(name+'_fdk.spec')))
 with open(root/(name+'_fdk_spec.log'),'w') as log:run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'-b','128','-o',str(root/(name+'_fdk_spec.m4a')),str(data/(stem+'.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
 print(name,flush=True)
