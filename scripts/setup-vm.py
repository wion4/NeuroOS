"""Materialize a clean local VM configuration; preserve any existing VM/disk."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
config = root / 'vmware/NeuroOS.vmx'
if not config.exists():
    with config.open('xb') as output:
        output.write((config.with_suffix('.vmx.template')).read_bytes())
    print('Created local VM configuration:', config)
else:
    print('Existing VM configuration preserved:', config)
if not (config.parent / 'data.vmdk').exists() and not (config.parent / 'data-flat.vmdk').exists():
    subprocess.run([sys.executable, str(root / 'scripts/setup-data-disk.py')], check=True)
else:
    print('Existing virtual disk preserved.')
