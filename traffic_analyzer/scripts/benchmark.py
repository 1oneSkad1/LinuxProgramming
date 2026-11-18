"""Repeat sequential/threaded runs; compare analytical CSVs before timing claims."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import time

BASE=Path(__file__).resolve().parents[1]
CHECK_FILES=('summary.csv','hosts.csv','timeseries.csv','top_hosts.csv')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--input',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--sample-files',type=int,default=24,help='0 uses full input; otherwise copy evenly spaced files')
    a=p.parse_args()
    if a.repeats<3 or a.sample_files<0:p.error('repeats must be >=3 and sample-files >=0')
    files=sorted(x for x in a.input.glob('pkt_*') if x.is_file() and not x.is_symlink() and len(x.name)==20)
    if not files:p.error('No packet files')
    a.output.mkdir(parents=True,exist_ok=False)
    source=a.input.resolve()
    if a.sample_files and len(files)>a.sample_files:
        chosen=[files[round(i*(len(files)-1)/(a.sample_files-1))] for i in range(a.sample_files)] if a.sample_files>1 else files[:1]
        source=(a.output/'sample_input').resolve();source.mkdir()
        for file in chosen:shutil.copyfile(file,source/file.name)
        files=list(source.iterdir())
    metadata={'platform':platform.platform(),'cpu_count':os.cpu_count(),'cpuinfo':Path('/proc/cpuinfo').read_text().splitlines()[:8],
        'meminfo':Path('/proc/meminfo').read_text().splitlines()[:4],
        'mounts':subprocess.run(['df','-T',str(source)],capture_output=True,text=True).stdout,
        'compiler':subprocess.run(['gcc','--version'],capture_output=True,text=True).stdout.splitlines()[0],
        'compile_flags':'-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -pthread',
        'input':str(source),'files':len(files),'input_bytes':sum(x.stat().st_size for x in files),
        'cache_policy':'One unmeasured warm-up; caches not dropped; copy may warm sampled input; timed order randomized with seed 2026',
        'repeats':a.repeats,'measurement':'elapsed includes scanning through reports; excludes writing final metrics CSV; max RSS from /proc/self/status VmHWM (getrusage also recorded)'}
    (a.output/'environment.json').write_text(json.dumps(metadata,indent=2))
    hashes=None;records=[]
    def run(mode,workers,label):
        nonlocal hashes
        out=a.output/label
        cmd=[str(BASE/'traffic_analyzer'),'--input',str(source),'--output',str(out),'--mode',mode,'--workers',str(workers),'--quiet']
        completed=subprocess.run(cmd,capture_output=True,text=True)
        if completed.returncode:raise RuntimeError(completed.stdout+completed.stderr)
        current={name:digest(out/name) for name in CHECK_FILES}
        if hashes is None:hashes=current
        if current!=hashes:raise RuntimeError(f'Analytical output mismatch: {label}')
        with (out/'run_metrics.csv').open() as f:row=next(csv.DictReader(f))
        row['label']=label;return row
    print('Warm-up and correctness reference...',flush=True)
    run('sequential',1,'warmup')
    schedule=[(mode,workers,r) for mode,workers in [('sequential',1),('threaded',1),('threaded',2),('threaded',4),('threaded',8)] for r in range(a.repeats)]
    random.Random(2026).shuffle(schedule)
    for mode,workers,r in schedule:
        label=f'{mode}_{workers}_r{r+1}'
        row=run(mode,workers,label);records.append(row)
        print(f"{label}: {row['elapsed_seconds']} s, RSS {row['max_rss_kib']} KiB",flush=True)
    with (a.output/'runs.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
    baseline=statistics.median(float(x['elapsed_seconds']) for x in records if x['mode']=='sequential')
    summary=[]
    for mode,n in [('sequential',1),('threaded',1),('threaded',2),('threaded',4),('threaded',8)]:
        rs=[x for x in records if x['mode']==mode and int(x['workers'])==n]
        ts=[float(x['elapsed_seconds']) for x in rs];med=statistics.median(ts)
        summary.append({'mode':mode,'workers':n,'median_seconds':med,'min_seconds':min(ts),'max_seconds':max(ts),
            'speedup':baseline/med,'median_mib_per_second':statistics.median(float(x['input_mib_per_second']) for x in rs),
            'max_rss_kib':max(int(x['max_rss_kib']) for x in rs),'results_equal':True})
    with (a.output/'comparison.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
    print(json.dumps(summary,indent=2),flush=True)

if __name__=='__main__':main()
