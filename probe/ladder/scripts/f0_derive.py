import math
rows={}
for line in open('f0_score.log'):
 p=line.split();rows.setdefault(p[0],{})[p[1]]=(float(p[2]),int(p[3]))
for name,r in rows.items():
 s=(r['faac144'][0]-r['faac112'][0])/math.log2(r['faac144'][1]/r['faac112'][1]);base=r['faac128'];
 def adj(x):return x[0]-base[0]-s*math.log2(x[1]/base[1])
 print(name,'A',r['A'],'Apple',r['apple'],'FAAC128',base,'slope',round(s,5),'adjA',round(adj(r['A']),4),'adjApple',round(adj(r['apple']),4))
