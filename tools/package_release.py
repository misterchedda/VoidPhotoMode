"""Validate the staged player package and produce one install ZIP (stdlib only)."""
from pathlib import Path
import hashlib
import json
import sys
import zipfile

EXPECTED = {
    'dinput8.dll',
    'VoidPhotoMode/VoidPhotoBackend.dll',
    'VoidPhotoMode/README.txt',
    'VoidPhotoMode/LICENSE',
    'VoidPhotoMode/THIRD_PARTY_NOTICES.txt',
    'VoidPhotoMode/third_party/MinHook/LICENSE.txt',
}
MANIFEST = 'VoidPhotoMode/SHA256SUMS.json'


def package(stage, destination):
    stage = Path(stage).resolve()
    destination = Path(destination).resolve()
    if stage == destination or stage in destination.parents:
        raise ValueError('Output ZIP must be outside the staging directory')
    actual = {p.relative_to(stage).as_posix() for p in stage.rglob('*') if p.is_file()}
    if actual - {MANIFEST} != EXPECTED:
        raise ValueError(f'Unexpected package layout: missing={EXPECTED-actual}, extra={actual-EXPECTED-{MANIFEST}}')
    for relative in EXPECTED:
        p = stage / relative
        if p.is_symlink() or not p.stat().st_size:
            raise ValueError(f'Empty file or symlink: {relative}')
    hashes = {name: hashlib.sha256((stage / name).read_bytes()).hexdigest() for name in sorted(EXPECTED)}
    (stage / MANIFEST).write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(EXPECTED | {MANIFEST}):
            archive.write(stage / name, name)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None:
            raise ValueError('ZIP integrity check failed')
    print(f'Validated {len(EXPECTED)} install files: {destination}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: python tools/package_release.py STAGING_DIRECTORY OUTPUT.zip')
    package(*sys.argv[1:])
