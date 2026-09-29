import os
import numpy as np, scipy.signal as ss, soundfile as sf
src=sf.read(str(pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio')) / 'Severance__1.31-1.51_.16b48k.wav'),dtype='float32')[0][:,0]
for name in ('Severance_aligned_step1','Severance_apple_reemit','Severance_apple_ref'):
 x=np.fromfile(''+name+'.f32',dtype='<f4').reshape(-1,2)[:,0]
 a=src[10000:40000]; b=x[5000:45000]
 c=ss.correlate(b,a,mode='valid',method='fft')
 lag=int(np.argmax(c))+5000-10000
 print(name,'lag',lag,'decoded samples',len(x))
a=np.fromfile('Severance_apple_reemit.f32',dtype='<f4').reshape(-1,2)
b=np.fromfile('Severance_apple_ref.f32',dtype='<f4').reshape(-1,2)
print('reemit vs ref max_abs',np.max(np.abs(a[2112:2112+len(b)]-b)),'compared samples',len(b))
