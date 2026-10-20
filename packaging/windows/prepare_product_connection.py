"""Build-only connection for existing Windows sources. Does not launch installers."""
import argparse
import hashlib
import importlib
import json
import re
from pathlib import Path
import sys
import uuid

PINS = {
    'product_package.py': 'f00021a444c07c22388557f840188ca17b6fd6a9bfa9702450a276846e6abb2e',
    'build_release_package.py': '3262d1d9908c8eccefccc619cb2a33efc5da4416325ec0e601a1c98581bcaee0',
    'generate_release_catalog.py': '40f469db91ff1c6c9b5b1775e4f094b019a12a0cb46f48e97b76f33867d88f5d',
}
def require(ok, message):
    if not ok: raise ValueError(message)
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def verify_source_locales(product, source_zip, rows):
    # Exact source-pack layout, not a suffix search through historical sources.
    prefix = 'SonicLatch/data/locale/'
    expected = {}
    with source_zip.open('rb') as stream:
        inventory = product.candidate.Inventory(stream, product.candidate.Limits(
            max_archive_bytes=256 * 1024 * 1024))
        try:
            for key, entry in inventory.entries.items():
                if not key.startswith(prefix.lower()):
                    continue
                path = entry.orig_filename
                require(path.startswith(prefix) and
                        re.fullmatch(r'[a-z]{2}-[A-Z]{2}\.ini', path[len(prefix):]) is not None,
                        'unexpected source locale path: ' + path)
                expected[path] = product.sha(inventory.read_small(key, product.LIMITS.max_file_bytes))
        finally:
            inventory.close()
    actual = {}
    for row in rows:
        path = row['relativePath']
        if not path.startswith(prefix):
            continue
        require(path not in actual, 'duplicate locale input: ' + path)
        actual[path] = row['sha256']
    require(expected and set(actual) == set(expected), 'source/runtime locale path sets differ')
    for row in rows:
        path = row['relativePath']
        if path in expected:
            require(row['sha256'] == expected[path], 'source/runtime locale hash differs: ' + path)
            product.candidate.read_pinned(row['inputPath'], expected[path])
    return {'sourcePrefix': prefix, 'localeCount': len(expected),
            'pathSetAndActualBytesVerified': True}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', required=True, type=Path)
    p.add_argument('--dll', required=True, type=Path)
    p.add_argument('--dll-sha256', required=True)
    p.add_argument('--dll-size', required=True, type=int)
    p.add_argument('--build-binding-json', required=True, type=Path)
    p.add_argument('--build-proof', required=True, type=Path)
    p.add_argument('--source-zip', required=True, type=Path)
    p.add_argument('--source-sha256', required=True)
    p.add_argument('--source-size', required=True, type=int)
    context_input = p.add_mutually_exclusive_group(required=True)
    context_input.add_argument('--source-context-json', type=Path)
    context_input.add_argument('--build-source-manifest', type=Path)
    p.add_argument('--data-inputs-json', required=True, type=Path)
    p.add_argument('--output-dir', required=True, type=Path)
    a = p.parse_args()
    # Caller resolves paths from existing build proof. No recursive payload scan.
    scripts = a.repo / 'packaging/windows'
    for name, pin in PINS.items():
        require(digest(scripts / name) == pin, 'additive source pin mismatch: ' + name)
    require(re.fullmatch(r'[0-9a-f]{64}', a.dll_sha256) is not None and a.dll_size > 0, 'explicit DLL pin required')
    require(a.dll.stat().st_size == a.dll_size and digest(a.dll) == a.dll_sha256, 'DLL differs from explicit pin')
    require(re.fullmatch(r'[0-9a-f]{64}', a.source_sha256) is not None and
            0 < a.source_size <= 256 * 1024 * 1024, 'explicit bounded source hash/size required')
    require(a.source_zip.stat().st_size == a.source_size and
            digest(a.source_zip) == a.source_sha256, 'corresponding source ZIP differs from explicit pin')
    sys.path.insert(0, str(scripts.resolve()))
    product = importlib.import_module('product_package')
    builder = importlib.import_module('build_release_package')
    catalog = importlib.import_module('generate_release_catalog')
    binding = product.strict_json(a.build_binding_json.read_bytes())
    require(type(binding) is dict and set(binding) == {'baseCommit', 'dllSha256', 'dllSize', 'sourceManifestSha256', 'buildProofSha256'}, 'build binding fields')
    require(binding['dllSha256'] == a.dll_sha256 and type(binding['dllSize']) is int and
            binding['dllSize'] == a.dll_size and type(binding['buildProofSha256']) is str and
            re.fullmatch(r'[0-9a-f]{64}', binding['buildProofSha256']) is not None, 'build proof DLL binding differs')
    require(digest(a.build_proof) == binding['buildProofSha256'], 'build proof bytes differ')
    if a.source_context_json is not None:
        context = product.strict_json(a.source_context_json.read_bytes())
        context_evidence = {'mode': 'supplied-context', 'contextFileSha256': digest(a.source_context_json)}
    else:
        # Snapshot identity is explicitly the exact manifest bytes from this build proof.
        # This is not the ZIP hash or a claim that the uncommitted derived source equals HEAD.
        snapshot_hash = digest(a.build_source_manifest)
        context = {'baseCommit': binding['baseCommit'],
                   'sourceSnapshotSha256': snapshot_hash,
                   'buildId': 'candidate-binding-' + uuid.uuid4().hex, 'syntheticPayload': False}
        context_evidence = {'mode': 'build-manifest-bytes',
            'snapshotDefinition': 'SHA256 of exact bytes of source inventory manifest used by the QA-OFF build',
            'manifestFilename': a.build_source_manifest.name, 'manifestSha256': snapshot_hash,
            'buildIdOrigin': 'New candidate binding ID assigned by this invocation; not a historical build ID',
            'baseCommitMeaning': 'Reported baseline; QA-separated uncommitted derived inputs are identified by the manifest'}
    product.candidate.source_context(context)
    require(context['syntheticPayload'] is False, 'real build context required')
    require(context['baseCommit'] == binding['baseCommit'] and
            context['sourceSnapshotSha256'] == binding['sourceManifestSha256'], 'source context differs from build binding')
    context_evidence['buildBinding'] = binding
    context_evidence['buildProofValidation'] = 'bytes-sha256-only'
    context_evidence['buildProofSemanticValidation'] = 'operator-required: successful build, DLL, baseline and compile-input manifest correspondence'
    rows = product.strict_json(a.data_inputs_json.read_bytes())
    require(type(rows) is list and rows, 'explicit locale/license data input list required')
    for row in rows:
        require(type(row) is dict and set(row) == {'relativePath', 'inputPath', 'sha256'}, 'data input fields')
        require(row['relativePath'].startswith('SonicLatch/data/'), 'data rows cannot replace DLL')
        product.candidate.role(row['relativePath'])
        require(Path(row['inputPath']).is_absolute(), 'absolute input path required')
    locale_evidence = verify_source_locales(product, a.source_zip, rows)
    # Output must be fresh: never overwrite prior proof or generated headers.
    a.output_dir.mkdir(parents=True, exist_ok=False)
    spec = {'sourceContext': context, 'sourceArchivePath': str(a.source_zip.resolve()),
            'sourceArchiveSha256': a.source_sha256,
            'payload': [{'relativePath': 'SonicLatch/bin/64bit/SonicLatch.dll', 'inputPath': str(a.dll.resolve()), 'sha256': a.dll_sha256}] + rows}
    spec_path = a.output_dir / 'release-inputs.json'
    spec_path.write_bytes(product.canonical(spec))
    (a.output_dir / 'source-context.json').write_bytes(product.canonical(context))
    (a.output_dir / 'source-context-evidence.json').write_bytes(product.canonical(context_evidence))
    archive = a.output_dir / 'SonicLatch-0.1.0-product-candidate.zip'
    package_result = builder.prepare(spec_path, archive)
    header = a.output_dir / 'product_release.generated.hpp'
    catalog_result = catalog.generate(archive=archive, manifest_sha256=package_result['manifestSha256'],
        corresponding_source=a.source_zip, output=header, enable_installation=False)
    result = {'package': package_result, 'catalog': catalog_result,
              'sourceContextEvidence': context_evidence, 'localeEvidence': locale_evidence,
              'archiveSha256': digest(archive), 'installationEnabled': False,
              'publicationAuthorized': False, 'runtimeValidation': 'NOT_RUN',
              'helperBuild': 'NOT_RUN', 'nsisBuild': 'NOT_RUN'}
    (a.output_dir / 'connection-result.json').write_bytes(product.canonical(result))
    print(json.dumps(result, indent=2))
if __name__ == '__main__': main()
