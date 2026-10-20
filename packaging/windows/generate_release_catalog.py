"""Connect a pinned product package to the existing native Catalog type.
Fixture generation/validation stays unchanged. No build, execution or publication.
"""
from pathlib import Path
import argparse
import json
import product_package as product


def cpp(value):
    if type(value) is not str or any(ord(c) < 32 or ord(c) > 126 for c in value):
        raise product.PackageError('native ASCII identity required')
    return json.dumps(value, ensure_ascii=True)


def known(bundle):
    doc = product.strict_json(bundle.manifest)
    rows = product.validate_document(doc)
    owned = ','.join('{' + cpp(row['relativePath']) + ',Ref{' + cpp(row['sha256']) +
                     ',' + str(row['size']) + '}}' for row in rows)
    return ('KnownRelease{' + ','.join(map(cpp, (bundle.identity,
            product.sha(product.canonical(doc['sourceContext'])), doc['productId'],
            doc['moduleId'], doc['productVersion']))) + ',{' + owned + '}}')


def generate(*, archive, manifest_sha256, corresponding_source, output,
             enable_installation=False, predecessors=()):
    bundle = product.verify(archive, manifest_sha256)
    doc = product.strict_json(bundle.manifest)
    source = product.source_identity(corresponding_source, doc['sourceArchive']['sha256'])
    if source != doc['sourceArchive']:
        raise product.PackageError('corresponding-source filename/bytes differ from manifest')
    previous = [product.verify(path, pin) for path, pin in predecessors]
    if len(previous) > 32 or len({old.identity for old in previous}) != len(previous):
        raise product.PackageError('duplicate/excess predecessor inventory')
    lines = ['// GENERATED product catalog from actual pinned payload and corresponding source.\n',
             '// Publication is not authorized by this file. Rebuild helper after every manifest change.\n']
    payloads = {}
    for path, data in bundle.payload:
        key = product.sha(data)
        if key not in payloads:
            name = 'release_payload_' + str(len(payloads)); payloads[key] = (name, data)
            if data:
                lines.append('inline const unsigned char ' + name + '[]={\n')
                for start in range(0, len(data), 32):
                    lines.append(','.join(str(b) for b in data[start:start+32]) + ',\n')
                lines.append('};\n')
    lines.append('inline const Catalog releaseCatalog=[] { Catalog c;\n')
    lines.append('c.packageApproved=' + ('true' if enable_installation else 'false') + ';\n')
    lines.append('c.productId="SonicLatch";\nc.current=' + known(bundle) + ';\n')
    for old in previous:
        lines.append('c.acceptedPrevious.push_back(' + known(old) + ');\n')
    for key, (name, data) in payloads.items():
        value = 'Bytes(reinterpret_cast<const char*>(' + name + '),sizeof(' + name + '))' if data else 'Bytes{}'
        lines.append('c.blobs.emplace(' + cpp(key) + ',' + value + ');\n')
    lines.append('return c;}();\n')
    raw = ''.join(lines).encode('utf-8'); target = Path(output)
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as stream: stream.write(raw)
    return {'catalogSha256': product.sha(raw), 'manifestSha256': bundle.identity,
            'sourceArchive': source, 'productVersion': doc['productVersion'],
            'dllSha256': next(product.sha(data) for path, data in bundle.payload
                              if path == 'SonicLatch/SonicLatch.dll'),
            'installationEnabled': enable_installation, 'publicationAuthorized': False}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', required=True); p.add_argument('--manifest-sha256', required=True)
    p.add_argument('--corresponding-source', required=True); p.add_argument('--output', required=True)
    p.add_argument('--enable-installation', action='store_true',
                   help='Existing explicit execution opt-in; not publication approval')
    p.add_argument('--predecessor', nargs=2, action='append', default=[], metavar=('ARCHIVE', 'MANIFEST_SHA256'))
    a = p.parse_args()
    print(json.dumps(generate(archive=a.archive, manifest_sha256=a.manifest_sha256,
          corresponding_source=a.corresponding_source, output=a.output,
          enable_installation=a.enable_installation, predecessors=a.predecessor), indent=2))
