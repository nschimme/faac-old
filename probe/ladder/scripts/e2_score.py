from score_common import *
for name,stem in clips.items():
 src=data/(stem+'.wav');n=sf.info(src).frames
 items={'faac112':root/(name+'_faac112.m4a'),'faac128':root/(name+'_faac128.m4a'),'faac144':root/(name+'_faac144.m4a'),'apple_ref':repo/'probe/ladder/ref/apple'/(stem+'.m4a'),'fdk_ref':repo/'probe/ladder/ref/fdk'/(stem+'.m4a')}
 step=root/(name+'_aligned_step1.m4a');aligned=root/(name+'_aligned_step1_score.wav');wav_from_aac(step,aligned,64,n);items['apple_aligned_step1']=aligned
 own=root/(name+'_apple_own.aac');ownwav=root/(name+'_apple_own_score.wav');wav_from_aac(own,ownwav,2112,n);items['apple_own_reemit']=ownwav
 for label,deg in items.items():
  print(name,label,score(src,deg),deg.stat().st_size,flush=True)
