import os
import sys
import numpy as np,scipy.signal as ss,pathlib
root=pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
for name in ('Severance','21classic','velvet','Greensleeves','German'):
 a=np.fromfile(root/(name+'_F0.f32'),dtype='<f4').reshape(-1,2)[:,0]
 b=np.fromfile(root/(name+'_KF_target.f32'),dtype='<f4').reshape(-1,2)[:,0]
 lo=48000;hi=96000;x=a[lo:hi];y=b[lo-1024:hi+1024]
 c=ss.correlate(y,x,mode='valid',method='fft');lag=int(np.argmax(c))-1024
 print(name,'F1 vs F0 PCM lag',lag,'samples',flush=True)
