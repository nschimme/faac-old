import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];faac=os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac'));faad=os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad')
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 for tag,extra,target in [('spec',{'FAAC_STEP1':str(root/(name+'_G_A.bin')),'FAAC_STEP1_OFFSET':'1','FAAC_STEP1_ORIGIN':str(root/(name+'_G_A_origin.bin')),'FAAC_STEP1_SPEC_DUMP':str(root/(name+'_G_spec.txt'))},root/(name+'_G_A.aac')),('mask',{'FAAC_G_MASK_DUMP':str(root/(name+'_G_mask.txt'))},old/(name+'_KF_target.aac'))]:
  dump=root/(name+'_G_'+tag+'.txt');dump.unlink(missing_ok=True);out=root/(name+'_G_'+tag+'.m4a');env=os.environ.copy();env.update(extra)
  with open(root/(name+'_G_'+tag+'.log'),'w') as log:p=subprocess.run([faac,'--overwrite','-b','128','-o',str(out),str(old/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:raise RuntimeError(name+tag+' enc')
  raw=root/(name+'_G_'+tag+'.aac');subprocess.run([faad,'-a',str(raw),str(out)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
  same=raw.read_bytes()==target.read_bytes();print(name,tag,'ADTS',same,'records',sum(1 for _ in dump.open()),flush=True)
  if not same:raise RuntimeError(name+tag+' changed output')
