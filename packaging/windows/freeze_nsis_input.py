"""Copy a caller-pinned built helper and produce a fixed NSIS input include.
No compiler or installer is invoked. Use existing NSIS 3.13 separately only when authorized.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re


def freeze(helper, expected_sha256, directory):
    if not re.fullmatch('[0-9a-f]{64}', expected_sha256):
        raise ValueError('explicit built-helper SHA-256 required')
    data = Path(helper).read_bytes()
    if not 0 < len(data) <= 32 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != expected_sha256:
        raise ValueError('built helper bytes do not match caller pin')
    target = Path(directory).resolve()
    # Reject all NSIS quoting/interpolation characters in generated absolute paths.
    if any(c in str(target) for c in ('"', '$', '\r', '\n')):
        raise ValueError('unsafe NSIS input path')
    target.mkdir(parents=True, exist_ok=False)
    with (target / 'SonicLatch-helper.exe').open('xb') as stream:
        stream.write(data)
    content = ('; Fixed helper input. Final payload catalog is inside this helper.\n'
               '!define SONICLATCH_HELPER_FILE "' + str(target / 'SonicLatch-helper.exe') + '"\n'
               '!define SONICLATCH_INSTALLER_OUTPUT "' + str(target / 'SonicLatch-0.1.0-Setup.exe') + '"\n')
    (target / 'product_package.generated.nsh').write_text(content, encoding='utf-8')
    (target / 'INPUT-HASH.json').write_text(json.dumps({'helperSha256': expected_sha256,
        'helperSize': len(data), 'publicationAuthorized': False}, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--helper', required=True);p.add_argument('--sha256', required=True)
    p.add_argument('--output-directory', required=True)
    a = p.parse_args();freeze(a.helper, a.sha256, a.output_directory)
