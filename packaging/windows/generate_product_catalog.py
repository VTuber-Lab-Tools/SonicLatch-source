"""Freeze existing candidate manifest/hash/bytes into the native helper.

Source-generation only. No download, build, execution, signing, installation,
registry operation or publication. Default output cannot perform installation.
"""
from pathlib import Path
import argparse
import hashlib
import json
import candidate_package as candidate


def cpp(value):
    # Product identity/path fields in this schema are ASCII. Reject rather than
    # rely on execution-charset-specific C++ string conversion.
    if not isinstance(value, str) or any(ord(c) < 32 or ord(c) > 126 for c in value):
        raise ValueError('ASCII native catalog identity required')
    return json.dumps(value, ensure_ascii=True)


def known(bundle, product_id):
    doc = candidate.strict_json(bundle.manifest)
    rows = candidate.validate_document(doc)
    source = candidate.sha(candidate.canonical(doc['sourceContext']))
    owned = ','.join('{' + cpp(row['relativePath']) + ',Ref{' + cpp(row['sha256']) +
                     ',' + str(row['size']) + '}}' for row in rows)
    return ('KnownRelease{' + ','.join(map(cpp, (bundle.identity, source, product_id,
            doc['moduleId'], doc['productVersion']))) + ',{' + owned + '}}')


def generate(*, bundle_path, manifest_sha256, product_id, output,
             enable_installation=False, predecessors=()):
    if product_id != 'SonicLatch':
        raise ValueError('only SonicLatch machine product supported')
    bundle = candidate.verify(bundle_path, manifest_sha256)
    doc = candidate.strict_json(bundle.manifest)
    if enable_installation and doc['sourceContext']['syntheticPayload']:
        raise ValueError('synthetic payload cannot enable product installation')
    previous = [candidate.verify(path, pin) for path, pin in predecessors]
    if len(previous) > 32 or len({b.identity for b in previous}) != len(previous):
        raise ValueError('duplicate/excess predecessor catalog')
    lines = ['// GENERATED from caller-pinned candidate bytes. Publication remains unauthorized.\n',
             '// Regenerate after final DLL/source hash changes; rebuild helper and NSIS together.\n']
    payloads = {}
    for path, data in bundle.payload:
        key = candidate.sha(data)
        if key not in payloads:
            name = 'catalog_payload_' + str(len(payloads))
            payloads[key] = (name, data)
            if data:
                lines.append('inline const unsigned char ' + name + '[]={\n')
                for start in range(0, len(data), 32):
                    lines.append(','.join(str(b) for b in data[start:start+32]) + ',\n')
                lines.append('};\n')
    lines.append('inline const Catalog releaseCatalog=[] { Catalog c;\n')
    lines.append('c.packageApproved=' + ('true' if enable_installation else 'false') + ';\n')
    lines.append('c.productId=' + cpp(product_id) + ';\nc.current=' + known(bundle, product_id) + ';\n')
    for old in previous:
        lines.append('c.acceptedPrevious.push_back(' + known(old, product_id) + ');\n')
    for key, (name, data) in payloads.items():
        expr = 'Bytes(reinterpret_cast<const char*>(' + name + '),sizeof(' + name + '))' if data else 'Bytes{}'
        lines.append('c.blobs.emplace(' + cpp(key) + ',' + expr + ');\n')
    lines.append('return c;}();\n')
    encoded = ''.join(lines).encode('utf-8')
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # New output only: never silently overwrite a reviewed catalog.
    with destination.open('xb') as stream:
        stream.write(encoded)
    return {'catalogSha256': hashlib.sha256(encoded).hexdigest(),
            'manifestSha256': bundle.identity,
            'dllSha256': next(candidate.sha(data) for path, data in bundle.payload
                              if path == 'SonicLatch/SonicLatch.dll'),
            'productVersion': doc['productVersion'],
            'installationEnabled': enable_installation,
            'publicationAuthorized': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--product-id', default='SonicLatch')
    parser.add_argument('--enable-installation', action='store_true',
                        help='Only after the exact payload is approved for installer execution; never publication approval')
    parser.add_argument('--predecessor', nargs=2, action='append', default=[], metavar=('BUNDLE', 'MANIFEST_SHA256'))
    args = parser.parse_args()
    print(json.dumps(generate(bundle_path=args.bundle, manifest_sha256=args.manifest_sha256,
          product_id=args.product_id, output=args.output, enable_installation=args.enable_installation,
          predecessors=args.predecessor), indent=2))
