import os
import sys,json,pathlib,os,subprocess
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];faac=os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac'));index=json.loads((root/'g2_index.json').read_text());status=json.loads((root/'g2_encode.json').read_text());prepared={}
sys.path.insert(0,str(root));from g_make import make_one
for item in index:
 k=item['id']
 if not status[k].get('scorable',False):continue
 up=root/(k+'_G_units.json');saved=json.loads(up.read_text());make_one(k,item['stem'],root/(k+'_apple.dump'),root/(k+'_normal.dump'),['SFt','SFs']);saved.update(json.loads(up.read_text()));up.write_text(json.dumps(saved,indent=2))
 prepared[k]={'stem':item['stem'],'SFt':False,'SFs':False}
 for arm in ('SFt','SFs'):
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(k+'_G_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(k+'_G_'+arm+'_origin.bin')))
  out=root/(k+'_G_'+arm+'.m4a')
  with open(root/(k+'_G_'+arm+'.log'),'w') as log:p=subprocess.run([faac,'--overwrite','-b','128','-o',str(out),str(root/(k+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:print(k,arm,'ENC_FAIL',flush=True);continue
  raw=root/(k+'_G_'+arm+'.f32');log=root/(k+'_G_'+arm+'_ffmpeg.log')
  with log.open('w') as fh:q=subprocess.run(['ffmpeg','-v','error','-y','-i',str(out),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],stdout=fh,stderr=subprocess.STDOUT)
  prepared[k][arm]=(q.returncode==0 and log.stat().st_size==0);print(k,arm,'CLEAN' if prepared[k][arm] else 'DECODE_FAIL',out.stat().st_size,flush=True)
 (root/'g3_wide_prepare.json').write_text(json.dumps(prepared,indent=2))
