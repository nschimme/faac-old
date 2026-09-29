import sys
import os,subprocess,pathlib,json
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];faac=os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac'));index=json.loads((root/'g2_index.json').read_text());controls=json.loads((root/'g2_controls.json').read_text());status={}
for item in index:
 k=item['id'];c=controls[k];status[k]={'stem':item['stem'],'arms':{},'reason':[]}
 if not all(c[z] for z in ('KA','KF','clean_A','clean_F')):status[k]['reason'].append('control failed');continue
 for tag,path in [('apple',pathlib.Path(item['ref']))]+[(f'base{r}',root/(k+f'_base{r}.m4a')) for r in (112,128,144)]:
  log=root/(k+'_'+tag+'_ffmpeg.log')
  with log.open('w') as fh:q=subprocess.run(['ffmpeg','-v','error','-i',str(path),'-f','null','-'],stdout=fh,stderr=subprocess.STDOUT)
  if q.returncode or log.stat().st_size:status[k]['reason'].append(tag+' non-clean decode')
 for arm in ('fSF','fWIN','rSF','rWIN'):
  env=os.environ.copy();env.update(FAAC_STEP1=str(root/(k+'_G_'+arm+'.bin')),FAAC_STEP1_OFFSET='1',FAAC_STEP1_ORIGIN=str(root/(k+'_G_'+arm+'_origin.bin')))
  out=root/(k+'_G_'+arm+'.m4a')
  with open(root/(k+'_G_'+arm+'.log'),'w') as log:p=subprocess.run([faac,'--overwrite','-b','128','-o',str(out),str(root/(k+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
  if p.returncode:status[k]['reason'].append(arm+' encode failure');status[k]['arms'][arm]=False;continue
  raw=root/(k+'_G_'+arm+'.f32');log=root/(k+'_G_'+arm+'_ffmpeg.log')
  with log.open('w') as fh:q=subprocess.run(['ffmpeg','-v','error','-y','-i',str(out),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(raw)],stdout=fh,stderr=subprocess.STDOUT)
  clean=q.returncode==0 and log.stat().st_size==0;status[k]['arms'][arm]=clean
  if not clean:status[k]['reason'].append(arm+' non-clean decode')
 status[k]['scorable']=not status[k]['reason'];print(k,item['stem'],status[k]['arms'],status[k]['reason'],flush=True)
 (root/'g2_encode.json').write_text(json.dumps(status,indent=2))
