import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3]
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 for arm in ('WIN','BW','CLS','SF','MS','TNS'):
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(name+'_F2_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(name+'_F2_'+arm+'_origin.bin')))
  out=root/(name+'_F2_'+arm+'.m4a')
  with open(root/(name+'_F2_'+arm+'.log'),'w') as log:p=subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'--overwrite','-b','128','-o',str(out),str(root/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode: print(name,arm,'ENC_FAIL',p.returncode,flush=True);continue
  pcm=root/(name+'_F2_'+arm+'.f32')
  with open(root/(name+'_F2_'+arm+'_decode.log'),'w') as log:p=subprocess.run(['ffmpeg','-v','error','-y','-i',str(out),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(pcm)],stdout=log,stderr=subprocess.STDOUT)
  print(name,arm,'decode',p.returncode,'bytes',out.stat().st_size,flush=True)
