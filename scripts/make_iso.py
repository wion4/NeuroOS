"""Build the normal kernel and package a UEFI-only El Torito boot ISO."""
import hashlib
import os
import io
from pathlib import Path
import sys

import neuro

sys.path.insert(0, str(neuro.ROOT / 'tools/python'))
import pycdlib


def main():
    os.environ.setdefault('NEUROOS_DESKTOP','1')
    if os.environ.get('NEUROOS_FS3_TEST')=='1':
        raise SystemExit('Experimental stress builds use their test ESP, not the working VM ISO.')
    if any((neuro.ROOT / 'vmware').glob('*.vmx.lck')):
        raise SystemExit('Power off the NeuroOS VMware guest before rebuilding its ISO.')
    target = neuro.build()
    ai_demo=os.environ.get('NEUROOS_AI_DEMO')=='1'
    apps=('ramfs','fsclient','hello','filedemo','libcdemo','floatdemo','compiler','desktop','loader')+('airuntime','aiclient')*int(ai_demo)
    output = neuro.ROOT / 'build/NeuroOS.iso'
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, joliet=3, rock_ridge='1.09', vol_ident='NEUROOS')
    iso.add_directory('/BOOT', rr_name='boot', joliet_path='/boot')
    iso.add_file(str(target / 'esp/boot/kernel.elf'), '/BOOT/KERNEL.ELF;1',
                 rr_name='kernel.elf', joliet_path='/boot/kernel.elf')
    for app in apps:
        iso.add_file(str(target/'esp/boot'/f'{app}.nos'),f'/BOOT/{app.upper()}.NOS;1',
                     rr_name=f'{app}.nos',joliet_path=f'/boot/{app}.nos')
    iso.add_file(str(target/'esp/boot/sdk.pack'),'/BOOT/SDK.PAK;1',rr_name='sdk.pack',joliet_path='/boot/sdk.pack')
    if ai_demo:iso.add_file(str(target/'esp/boot/ai.pack'),'/BOOT/AI.PAK;1',rr_name='ai.pack',joliet_path='/boot/ai.pack')
    iso.add_file(str(target / 'esp/limine.conf'), '/LIMINE.CON;1',
                 rr_name='limine.conf', joliet_path='/limine.conf')
    iso.add_file(str(neuro.ROOT / 'vendor/limine/limine-uefi-cd.bin'), '/BOOT/EFI.IMG;1',
                 rr_name='efi.img', joliet_path='/boot/efi.img')
    iso.add_eltorito('/BOOT/EFI.IMG;1', bootcatfile='/BOOT/BOOT.CAT;1',
                    rr_bootcatname='boot.cat', joliet_bootcatfile='/boot/boot.cat',
                    platform_id=0xef, efi=True, boot_load_size=5760)
    iso.write(str(output))
    iso.close()
    # Verify the payload really packaged is the normal kernel just built.
    check = pycdlib.PyCdlib()
    check.open(str(output))
    payload = io.BytesIO()
    check.get_file_from_iso_fp(payload, rr_path='/boot/kernel.elf')
    assert payload.getvalue() == (target / 'esp/boot/kernel.elf').read_bytes()
    for app in apps:
        content=io.BytesIO();check.get_file_from_iso_fp(content,rr_path=f'/boot/{app}.nos')
        assert content.getvalue()==(target/'esp/boot'/f'{app}.nos').read_bytes()
    content=io.BytesIO();check.get_file_from_iso_fp(content,rr_path='/boot/sdk.pack')
    assert content.getvalue()==(target/'esp/boot/sdk.pack').read_bytes()
    if ai_demo:
        content=io.BytesIO();check.get_file_from_iso_fp(content,rr_path='/boot/ai.pack')
        assert content.getvalue()==(target/'esp/boot/ai.pack').read_bytes()
    config=io.BytesIO();check.get_file_from_iso_fp(config,rr_path='/limine.conf')
    assert config.getvalue()==(target/'esp/limine.conf').read_bytes()
    assert check.eltorito_boot_catalog.validation_entry.platform_id == 0xef
    check.close()
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix('.iso.sha256').write_text(f'{digest}  {output.name}\n')
    print(f'ISO ready: {output} ({output.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
