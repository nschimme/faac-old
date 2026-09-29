import sys
import os,subprocess,pathlib,json
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'));repo=pathlib.Path(__file__).resolve().parents[4];faac=os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac'));faad=os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad');index=json.loads((root/'g2_index.json').read_text());status={}
def enc(k,arm,origin):
 env=os.environ.copy();env.update(FAAC_STEP1=str(root/(k+'_G_A.bin' if arm=='KA' else k+'_G_'+arm+'.bin')),FAAC_STEP1_OFFSET='1')
 if origin:env['FAAC_STEP1_ORIGIN']=str(root/(k+'_G_'+arm+'_origin.bin'))
 out=root/(k+'_G_'+arm+'.m4a')
 with open(root/(k+'_G_'+arm+'.log'),'w') as log:p=subprocess.run([faac,'--overwrite','-b','128','-o',str(out),str(root/(k+'_plus.wav'))],env=env,stdout=log,stderr=subprocess.STDOUT)
 return out,p.returncode
def raw(path,k,tag):
 out=root/(k+'_'+tag+'.aac');p=subprocess.run([faad,'-a',str(out),str(path)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);return out if p.returncode==0 else None
def clean(path,k,tag):
 rawpath=root/(k+'_'+tag+'.f32');log=root/(k+'_'+tag+'_ffmpeg.log')
 with log.open('w') as fh:p=subprocess.run(['ffmpeg','-v','error','-y','-i',str(path),'-f','f32le','-acodec','pcm_f32le','-ac','2','-ar','48000',str(rawpath)],stdout=fh,stderr=subprocess.STDOUT)
 return p.returncode==0 and log.stat().st_size==0
for item in index:
 k=item['id'];s={'stem':item['stem'],'KA':False,'KF':False,'clean_A':False,'clean_F':False,'reason':[]}
 try:
  A,ae=enc(k,'A',True);KA,ke=enc(k,'KA',False);F,fe=enc(k,'F',True)
  if ae or ke or fe:s['reason'].append('encode error')
  else:
   ar=raw(A,k,'A');kar=raw(KA,k,'KA');fr=raw(F,k,'F');normal=raw(root/(k+'_normal.m4a'),k,'normal')
   if None in (ar,kar,fr,normal):s['reason'].append('ADTS extraction error')
   else:
    s['KA']=ar.read_bytes()==kar.read_bytes();s['KF']=fr.read_bytes()==normal.read_bytes()
    if not s['KA']:s['reason'].append('KA byte mismatch')
    if not s['KF']:s['reason'].append('KF byte mismatch')
    s['clean_A']=clean(A,k,'A');s['clean_F']=clean(F,k,'F')
    if not s['clean_A']:s['reason'].append('A non-clean decode')
    if not s['clean_F']:s['reason'].append('F non-clean decode')
 except Exception as e:s['reason'].append(str(e))
 status[k]=s;(root/'g2_controls.json').write_text(json.dumps(status,indent=2));print(k,item['stem'],'KA',s['KA'],'KF',s['KF'],'decode',s['clean_A'],s['clean_F'],'reason',s['reason'],flush=True)
