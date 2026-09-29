import os
import sys,json,pathlib,os,subprocess
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];old=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
sys.path.insert(0,str(root));from g_make import make_one,clips
for name,stem in clips.items():
 up=root/(name+'_G_units.json');saved=json.loads(up.read_text());make_one(name,stem,repo/'probe/ladder/survey/apple'/(stem+'.dump'),old/(name+'_F1_normal.dump'),['SFt','SFs']);saved.update(json.loads(up.read_text()));up.write_text(json.dumps(saved,indent=2))
 for arm in ('SFt','SFs'):
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(name+'_G_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(name+'_G_'+arm+'_origin.bin')))
  out=root/(name+'_G_'+arm+'.m4a')
  with open(root/(name+'_G_'+arm+'.log'),'w') as log:p=subprocess.run([os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac')),'--overwrite','-b','128','-o',str(out),str(old/(name+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:raise RuntimeError(name+' '+arm+' enc fail')
  raw=root/(name+'_G_'+arm+'.f32');log=root/(name+'_G_'+arm+'_decode.log')
  with log.open('w') as fh:p=subprocess.run(['ffmpeg','-v','error','-y','-i',str(out),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],stdout=fh,stderr=subprocess.STDOUT)
  if p.returncode or log.stat().st_size:raise RuntimeError(name+' '+arm+' decode fail')
  print(name,arm,'CLEAN',out.stat().st_size,flush=True)
