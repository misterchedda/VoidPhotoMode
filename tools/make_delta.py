"""Generate a bounded copy/literal HUD patch from two locally owned assets.
Usage: python make_delta.py original.iggy patched.iggy ../src/generated/hud_patch_data.inc
Neither full asset is included in the generated patch.
"""
import sys,struct
from pathlib import Path
def fingerprint(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value
def delta(original,target):
    # Small aligned anchors avoid expensive global diffing of opaque ABC data.
    anchors={original[i:i+32]:i for i in range(0,len(original)-31,4)}
    output=bytearray(struct.pack('<4sIIQQ',b'VPD1',len(original),len(target),fingerprint(original),fingerprint(target)))
    pending=bytearray();pos=0;copied=0
    def flush():
        if pending:output.extend(struct.pack('<BI',1,len(pending)));output.extend(pending);pending.clear()
    while pos<len(target):
        start=anchors.get(target[pos:pos+32])
        if start is None:pending.append(target[pos]);pos+=1;continue
        count=32
        while pos+count<len(target) and start+count<len(original) and target[pos+count]==original[start+count]:count+=1
        flush();output.extend(struct.pack('<BII',0,start,count));copied+=count;pos+=count
    flush()
    print(f'Patch {len(output)} bytes; copied {copied}/{len(target)} bytes from installed original')
    return output
if __name__=='__main__':
    original,target,destination=map(Path,sys.argv[1:])
    data=delta(original.read_bytes(),target.read_bytes())
    destination.write_text('// Generated copy/literal patch. Requires the exact installed original HUD.\nstatic constexpr unsigned char kHudPatch[]={\n'+''.join(','.join(f'0x{x:02x}' for x in data[i:i+20])+',\n' for i in range(0,len(data),20))+'};\n')
