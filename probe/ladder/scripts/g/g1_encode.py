import sys
import os,subprocess,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];names=('Severance','21classic','velvet','Greensleeves','German')
arms=('rWIN','rBW','rCLS','rSF','rMS','rTNS','SFt','SFs','rSF+rWIN','rSF+rBW','rSF+rCLS','rSF+rMS','rSF+rTNS','rWIN+rBW','rWIN+rCLS','rWIN+rMS','rWIN+rTNS','rMS+rCLS')
for name in names:
 for arm in arms:
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(name+'_G_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(name+'_G_'+arm+'_origin.bin')))
  out=root/(name+'_G_'+arm+'.m4a')
  with open(root/(name+'_G_'+arm+'.log'),'w') as log:p=subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'--overwrite','-b','128','-o',str(out),str(old/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:raise RuntimeError(f'ENC FAIL {name} {arm}')
  pcm=root/(name+'_G_'+arm+'.f32')
  with open(root/(name+'_G_'+arm+'_decode.log'),'w') as log:p=subprocess.run(['ffmpeg','-v','error','-y','-i',str(out),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(pcm)],stdout=log,stderr=subprocess.STDOUT)
  clean=p.returncode==0 and (root/(name+'_G_'+arm+'_decode.log')).stat().st_size==0
  print(name,arm,'CLEAN' if clean else 'DECODE_FAIL',out.stat().st_size,flush=True)
  if not clean:raise RuntimeError(f'DECODE FAIL {name} {arm}')
