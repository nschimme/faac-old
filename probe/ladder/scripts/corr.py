import importlib.util,sys,math
p=importlib.util.spec_from_file_location('mo','probe/ladder/mdct_offset.py');m=importlib.util.module_from_spec(p);p.loader.exec_module(m)
def corr(a,b):
 ma=sum(a)/len(a);mb=sum(b)/len(b);x=[v-ma for v in a];y=[v-mb for v in b];return sum(u*v for u,v in zip(x,y))/math.sqrt(sum(u*u for u in x)*sum(v*v for v in y))
for ref in ('apple','fdk'):
 r=m.ref_energy('probe/ladder/survey/'+ref+'/Severance__1.31-1.51_.16b48k.dump')
 for arm in ('unshifted','plus','minus') if ref=='apple' else ('unshifted',):
  f=m.faac_energy(str(root/('Severance_'+arm+'.energy')))
  z=[]
  for off in range(-3,4):
   pairs=[(f[i],r[i+off]) for i in f if i+off in r and i>2]
   z.append((off,corr(*zip(*pairs))))
  print(ref,arm,'peak',max(z,key=lambda x:x[1]),'scores',z)
