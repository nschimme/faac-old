#!/usr/bin/env python3
"""Serial reference syntax survey; never scores or retains decoded audio."""
from pathlib import Path
import os, subprocess, collections, json
ROOT = Path(__file__).resolve().parent
CORPUS = Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'))
FAAD = Path(os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad'))
STEMS = ['Severance__1.31-1.51_.16b48k', '21-classic.441.16b48k',
         'velvet.16b48k', '24-Greensleeves-Korean-male-speech.441.16b48k',
         '12-German-male-speech.441.16b48k']
summary=[]
for stem in STEMS:
    src=CORPUS/(stem+'.wav')
    for ref in ('apple','fdk'):
        out=ROOT/'ref'/ref/(stem+'.m4a')
        out.parent.mkdir(parents=True,exist_ok=True)
        if ref=='fdk' and not out.exists():
            subprocess.run(['fdkaac','-p','2','-b','128000','-o',str(out),str(src)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        dump=ROOT/'survey'/ref/(stem+'.dump')
        dump.parent.mkdir(parents=True,exist_ok=True)
        if dump.exists(): dump.unlink()
        wav=ROOT/'survey'/ref/(stem+'.wav')
        env={**os.environ,'FAAD_DUMP':str(dump),'FAAD_LADDER_DUMP':'1'}
        p=subprocess.run([str(FAAD),'--strict','--no-gapless','-q','-o',str(wav),str(out)],env=env,capture_output=True,text=True)
        wav.unlink(missing_ok=True)
        counts=collections.Counter()
        frames=set()
        if dump.exists():
            for line in dump.open():
                if line.startswith('I '):
                    fields=line.split('|')[0].split()
                    frames.add(int(fields[1]))
                    counts['ics']+=1
                    counts['shape_'+fields[4]]+=1
                    counts['short' if fields[3]=='2' else 'long']+=1
                    counts['pulse']+=int(fields[8])
                    counts['tns']+=int(fields[9])
                elif line.startswith('C '):
                    for b in line.split('|',1)[1].split():
                        if ':' in b:
                            cb=int(b.split(':')[0])
                            if cb==13: counts['pns_bands']+=1
                            if cb in (14,15): counts['is_bands']+=1
        summary.append({'ref':ref,'clip':stem,'bytes':out.stat().st_size,'frames':len(frames),'decode_ok':p.returncode==0,'counts':dict(counts),'diagnostics':p.stderr})
(ROOT/'survey'/'summary.json').write_text(json.dumps(summary,indent=2))
for s in summary:
    print(s['ref'],s['clip'],s['bytes'],s['frames'],s['decode_ok'],s['counts'],flush=True)
