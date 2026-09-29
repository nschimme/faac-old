import ast,re,collections
rows=[]
for line in open('zs_character.log'):
 m=re.match(r'^(\S+) (apple|fdk) (Z|S) lines (\d+) freq (\{.*?\}) peak_avg_med ([\d.]+|None) isolated% ([\d.]+) whole% ([\d.]+) near80% ([\d.]+) bands (\d+)',line)
 if m:rows.append((m[1],m[2],m[3],int(m[4]),ast.literal_eval(m[5]),float(m[6]),float(m[7]),float(m[8]),float(m[9]),int(m[10])))
for ref in ('apple','fdk'):
 for arm in ('Z','S'):
  a=[x for x in rows if x[1]==ref and x[2]==arm];n=sum(x[3] for x in a);c=collections.Counter();
  for x in a:c.update(x[4])
  print(ref,arm,'lines',n,'freq%',[round(100*c[k]/n,1) for k in ('0-2k','2-6k','6-12k','>12k')],'band_peak/avg median range',min(x[5] for x in a),max(x[5] for x in a),'isolated%',round(sum(x[3]*x[6] for x in a)/n,1),'whole%',round(sum(x[3]*x[7] for x in a)/n,2),'near80%',round(sum(x[3]*x[8] for x in a)/n,2))
