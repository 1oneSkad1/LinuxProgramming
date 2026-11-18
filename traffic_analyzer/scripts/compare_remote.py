"""Read-only checksum comparison of a local file with its HTTP source."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

def digest(stream):
    h=hashlib.sha256()
    for chunk in iter(lambda:stream.read(1048576),b''):h.update(chunk)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--local',type=Path,required=True);p.add_argument('--url',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    with a.local.open('rb') as f:local=digest(f)
    with urllib.request.urlopen(a.url,timeout=30) as r:
        headers=dict(r.headers);remote=digest(r)
    report={'local_file':str(a.local),'url':a.url,'local_bytes':a.local.stat().st_size,'response_headers':headers,'local_sha256':local,'server_sha256':remote,'identical':local==remote,
        'scope':'Byte-for-byte SHA-256 comparison only; packet structure is checked by traffic_analyzer.'}
    a.output.write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2),flush=True)
    if local!=remote:raise SystemExit(1)

if __name__=='__main__':main()
