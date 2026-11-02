"""Create a new project-local virtual data disk; never overwrite an existing disk."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
directory=ROOT/'vmware'
vmrun=Path(r'C:\Program Files\VMware\VMware Workstation\vmrun.exe')
if vmrun.exists():
    result=subprocess.run([str(vmrun),'list'],capture_output=True,text=True,timeout=10)
    if result.returncode or str(directory/'NeuroOS.vmx').lower() in result.stdout.lower():
        raise SystemExit('Power off the guest first')
elif any(directory.glob('*.vmx.lck')):raise SystemExit('Power off the guest first')
flat=directory/'data-flat.vmdk'
descriptor=directory/'data.vmdk'
if flat.exists() or descriptor.exists():raise SystemExit('Data disk already exists; nothing changed')
with flat.open('xb') as disk:disk.truncate(64*1024*1024)
with descriptor.open('x',encoding='ascii') as disk:
    disk.write('''# Disk DescriptorFile
version=1
encoding="UTF-8"
CID=fffffffe
parentCID=ffffffff
createType="monolithicFlat"
RW 131072 FLAT "data-flat.vmdk" 0
ddb.adapterType = "ide"
ddb.geometry.cylinders = "130"
ddb.geometry.heads = "16"
ddb.geometry.sectors = "63"
''')
print('Created 64 MiB virtual data disk:',descriptor)
