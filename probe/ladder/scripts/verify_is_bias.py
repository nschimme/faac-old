import sys
import os,subprocess,pathlib,numpy as np
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[3]
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 for label,binfile,off in [('KF_IS_BIAS',root/(name+'_F1.bin'),0),('KA_is_bias_post',root/(name+'_apple.bin'),1)]:
  out=root/(name+'_'+label+'.m4a');env=os.environ.copy();env.update(FAAC_STEP1=str(binfile),FAAC_STEP1_OFFSET=str(off))
  if label=='KF_IS_BIAS':env['FAAC_STEP1_SELF_IS']='1'
  with open(root/(name+'_'+label+'.log'),'w') as log:subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'-b','128','-o',str(out),str(root/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  raw=root/(name+'_'+label+'.aac');subprocess.run([os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'),'-a',str(raw),str(out)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
  pcm=root/(name+'_'+label+'.f32');subprocess.run(['ffmpeg','-v','error','-y','-i',str(out),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(pcm)],check=True)
  targetraw=root/(name+('_KF_target.aac' if label=='KF_IS_BIAS' else '_KA_target.aac'))
  targetpcm=root/(name+('_KF_target.f32' if label=='KF_IS_BIAS' else '_KA_target.f32'))
  a=np.fromfile(pcm,dtype='<f4');b=np.fromfile(targetpcm,dtype='<f4');n=min(len(a),len(b));d=np.abs(a[:n]-b[:n]);where=np.flatnonzero(d)
  print(name,label,'ADTS',raw.read_bytes()==targetraw.read_bytes(),'max_abs',float(np.max(d)),'first_frame',int(where[0]//2048) if len(where) else None,'diff_float_values',len(where),flush=True)
