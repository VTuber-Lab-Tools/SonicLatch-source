"""Make a product package from explicit pinned inputs, never by directory discovery."""
from pathlib import Path
import argparse
import json
import product_package as product


def prepare(spec_path, output):
    spec_file = Path(spec_path)
    # The input spec is a local build instruction, not a signed authorization.
    spec = product.strict_json(spec_file.read_bytes())
    if type(spec) is not dict or set(spec) != {'sourceContext', 'sourceArchivePath', 'sourceArchiveSha256', 'payload'}:
        raise product.PackageError('explicit release-input spec fields required')
    if type(spec['payload']) is not list:
        raise product.PackageError('explicit payload list required')
    inputs = {}
    for row in spec['payload']:
        if type(row) is not dict or set(row) != {'relativePath', 'inputPath', 'sha256'} or row['relativePath'] in inputs:
            raise product.PackageError('duplicate/invalid release input')
        inputs[row['relativePath']] = (row['inputPath'], row['sha256'])
    bundle = product.build(context=spec['sourceContext'], inputs=inputs,
                           source_archive=spec['sourceArchivePath'], source_sha256=spec['sourceArchiveSha256'], output=output)
    doc = product.strict_json(bundle.manifest)
    return {'archive': str(output), 'manifestSha256': bundle.identity, 'sourceArchive': doc['sourceArchive'],
            'payload': doc['files'], 'publicationAuthorized': False}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs', required=True); p.add_argument('--output', required=True)
    a = p.parse_args(); print(json.dumps(prepare(a.inputs, a.output), indent=2))
