import sys
import os, sys, subprocess, pathlib, wave

root = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(os.environ.get('LADDER_WORK', './ladder_work'))
repo = pathlib.Path(__file__).resolve().parents[3]
data = pathlib.Path(os.environ.get('FAAC_BENCHMARK_DATA', '/opt/faac-benchmark/data/external/audio'))
faac_bin = os.environ.get('FAAC_BIN', str(repo / 'build_ladder/frontend/faac'))
faad_bin = os.environ.get('FAAD_BIN', '/tmp/faad-ladder-dump/build_faad/frontend/faad')

sys.path.insert(0, str(repo / 'probe/ladder'))
import parse_dump as pd

clips = {
    'Severance': 'Severance__1.31-1.51_.16b48k',
    '21classic': '21-classic.441.16b48k',
    'velvet': 'velvet.16b48k',
    'Greensleeves': '24-Greensleeves-Korean-male-speech.441.16b48k',
    'German': '12-German-male-speech.441.16b48k'
}

root.mkdir(parents=True, exist_ok=True)
for name, stem in clips.items():
    src = data / (stem + '.wav')
    dst = root / (name + '_plus.wav')
    if not dst.exists():
        with wave.open(str(src), 'rb') as f:
            p = f.getparams()
            x = f.readframes(f.getnframes())
        with wave.open(str(dst), 'wb') as f:
            f.setparams(p)
            f.writeframes(b'\0' * 64 * p.nchannels * p.sampwidth + x)

    out = root / f'{name}_F1_normal.m4a'
    dump = root / f'{name}_F1_normal.dump'
    with open(root / f'{name}_F1_normal.log', 'w') as log:
        subprocess.run([faac_bin, '-b', '128', '-o', str(out), str(dst)], stdout=log, stderr=subprocess.STDOUT, check=True)
    env = os.environ.copy()
    env.update(FAAD_DUMP=str(dump), FAAD_LADDER_DUMP='1')
    with open(root / f'{name}_F1_decode.log', 'w') as log:
        subprocess.run([faad_bin, '-q', '-o', str(root / f'{name}_F1_faad.wav'), str(out)], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(['python3', str(repo / 'probe/ladder/parse_dump.py'), str(dump), str(root / f'{name}_F1.bin')], stdout=subprocess.DEVNULL, check=True)

    a = pd.parse(str(dump))
    f0_dump = root / f'{name}_F0.dump'
    if f0_dump.exists():
        b = pd.parse(str(f0_dump))
        scores = []
        for off in (-1, 0, 1, 2):
            match = total = 0
            for af, ch in a.items():
                for k, ai in ch.items():
                    bi = b.get(af + off, {}).get(k)
                    if not bi:
                        continue
                    total += 1
                    match += int((ai.win_seq, ai.window_shape, ai.num_groups, ai.group_len) == (bi.win_seq, bi.window_shape, bi.num_groups, bi.group_len))
            scores.append((off, match, total))
        print(name, 'offset scores', scores, flush=True)
    else:
        print(name, 'generated F1 normal encode and dump', flush=True)
