from score_common import *
for ref in ('apple','fdk'):
 for name,stem in clips.items():
  src=data/(stem+'.wav');n=sf.info(src).frames;skip=2112 if ref=='apple' else 2048
  if ref=='apple':
   old=root/(name+'_step1c_apple.m4a')
   print(name,ref,'old_step1',score(src,old),old.stat().st_size,flush=True)
  for arm in ('K0','K1','Z','S','ZS','M','LO','HI'):
   prefix=root/(name+'_'+ref+'_'+arm);aac=pathlib.Path(str(prefix)+'.aac');wav=pathlib.Path(str(prefix)+'_score.wav')
   wav_from_aac(aac,wav,skip,n)
   print(name,ref,arm,score(src,wav),aac.stat().st_size,flush=True)
