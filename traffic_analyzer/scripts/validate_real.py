"""Full-day sequential/threaded equality and a separately implemented sample oracle."""
import argparse
import collections
import csv
import hashlib
import ipaddress
import json
from pathlib import Path
import struct
import subprocess

BASE=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--reuse-existing',action='store_true',help='Reuse already finished runs with matching input/mode metadata')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=a.reuse_existing)
    outputs=[]
    for mode,n in [('sequential',1),('threaded',4)]:
        out=a.output/mode
        if a.reuse_existing and (out/'run_metrics.csv').exists():
            with (out/'run_metrics.csv').open() as f:old=next(csv.DictReader(f))
            assert old['input']==str(a.input.resolve()) and old['mode']==mode and int(old['workers'])==n
            inputs=[x for x in a.input.glob('pkt_*') if x.is_file()]
            assert int(old['input_bytes'])==sum(x.stat().st_size for x in inputs)
            assert int(old['files'])==len(inputs)
            stamp=(out/'run_metrics.csv').stat().st_mtime_ns
            assert (BASE/'traffic_analyzer').stat().st_mtime_ns<=stamp
            assert all(x.stat().st_mtime_ns<=stamp for x in inputs)
            assert all((out/name).is_file() for name in ['summary.csv','hosts.csv','timeseries.csv','top_hosts.csv','errors.log'])
            print(f'Reuse recorded completed run: {mode} ({old["status"]})',flush=True)
            outputs.append(out);continue
        print(f'Start full input: {mode}, workers={n}',flush=True)
        cmd=[str(BASE/'traffic_analyzer'),'--input',str(a.input.resolve()),'--output',str(out.resolve()),'--mode',mode,'--workers',str(n)]
        run=subprocess.run(cmd,text=True,capture_output=True)
        (a.output/f'{mode}.log').write_text(run.stdout+run.stderr)
        print(run.stdout+run.stderr,flush=True)
        if run.returncode not in (0,1):raise RuntimeError(f'Full input analysis failed ({run.returncode}), inspect {out}')
        outputs.append(out)
    hashes={}
    for name in ['summary.csv','hosts.csv','timeseries.csv','top_hosts.csv','errors.log']:
        values=[hashlib.sha256((out/name).read_bytes()).hexdigest() for out in outputs]
        if values[0]!=values[1]:raise RuntimeError(f'Mismatch: {name}')
        hashes[name]=values[0]
    print('Full-day analytical outputs match. Checking independent sample oracle...',flush=True)
    files=sorted(a.input.glob('pkt_*'));samples=[]
    for file in [files[0],files[len(files)//2],files[-1]]:
        raw=file.read_bytes();offset=0;packets=total=0
        hosts=collections.defaultdict(lambda:[0,0,0,0]);times=collections.defaultdict(lambda:[0,0])
        while offset<len(raw):
            sec,usec,real,stored=struct.unpack_from('<IIHH',raw,offset);offset+=12
            data=raw[offset:offset+stored];offset+=stored
            if len(data)!=stored:raise ValueError('truncated oracle input')
            if not stored or data[0]>>4!=4:continue
            assert stored>=20 and 20<=4*(data[0]&15)<=stored and usec<1000000
            src=str(ipaddress.IPv4Address(data[12:16]));dst=str(ipaddress.IPv4Address(data[16:20]))
            hosts[src][0]+=1;hosts[src][1]+=real;hosts[dst][2]+=1;hosts[dst][3]+=real
            times[sec//60*60][0]+=1;times[sec//60*60][1]+=real;packets+=1;total+=real
        out=(a.output/('oracle_'+file.name)).resolve()
        if not (a.reuse_existing and out.exists()):
            subprocess.run([str(BASE/'traffic_analyzer'),'--input',str(file.resolve()),'--output',str(out)],check=True,capture_output=True)
        with (out/'hosts.csv').open() as f:
            actual={r['ip']:[int(r[k]) for k in ['tx_packets','tx_bytes','rx_packets','rx_bytes']] for r in csv.DictReader(f)}
        assert actual==dict(hosts)
        with (out/'timeseries.csv').open() as f:
            actual_times={int(r['epoch_start']):[int(r['packets']),int(r['bytes'])] for r in csv.DictReader(f)}
        assert actual_times==dict(times)
        samples.append({'file':file.name,'packets':packets,'bytes':total,'hosts':len(hosts),'independent_oracle_equal':True})
    with (outputs[0]/'hosts.csv').open() as f:
        all_hosts=list(csv.DictReader(f))
    with (outputs[0]/'summary.csv').open() as f:summary=next(csv.DictReader(f))
    assert sum(int(r['tx_bytes']) for r in all_hosts)==int(summary['bytes'])
    assert sum(int(r['rx_bytes']) for r in all_hosts)==int(summary['bytes'])
    assert sum(int(r['tx_packets']) for r in all_hosts)==int(summary['packets'])
    report={'full_input_analytical_outputs_equal':True,'sha256':hashes,'summary':summary,'independent_samples':samples,'host_conservation_checks':True}
    (a.output/'validation.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2),flush=True)

if __name__=='__main__':main()
