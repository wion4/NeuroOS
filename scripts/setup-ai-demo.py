"""Download the pinned optional MIT demo assets into ignored tools/, verify SHA-256."""
import hashlib
import json
from pathlib import Path
import urllib.request
ROOT=Path(__file__).resolve().parents[1]
def main():
    info=json.loads((ROOT/'models.lock.json').read_text())['stories260k']
    target=ROOT/'tools/models/stories260k';target.mkdir(parents=True,exist_ok=True)
    for name,record in info['files'].items():
        path=target/name
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest()==record['sha256']:
            print('Verified cached asset:',name);continue
        url=f"{info['repository']}/resolve/{info['revision']}/stories260K/{name}"
        with urllib.request.urlopen(url,timeout=45) as response:data=response.read(record['bytes']+1)
        if len(data)!=record['bytes'] or hashlib.sha256(data).hexdigest()!=record['sha256']:
            raise RuntimeError('Demo asset checksum failed: '+name)
        path.write_bytes(data);print('Downloaded and verified:',name)
if __name__=='__main__':main()
