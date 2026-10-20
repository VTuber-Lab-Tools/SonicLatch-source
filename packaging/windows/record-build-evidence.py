"""Record MSBuild read inputs and byte bindings after a successful local build.

Does not run a build or certify source/DLL semantics. Keep LOCAL-ONLY output private.
"""
from pathlib import Path
import argparse, hashlib, json, re

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ('source-root', 'source-manifest', 'build-dir', 'build-result', 'dll', 'out'):
        p.add_argument('--' + arg, required=True, type=Path)
    p.add_argument('--base-commit', required=True)
    p.add_argument('--root', action='append', default=[], metavar='NAME=PATH')
    a = p.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', a.base_commit):
        raise ValueError('Actual 40-character build baseline commit required')
    result = json.loads(a.build_result.read_text(encoding='utf-8-sig'))
    if result.get('configureExit') != 0 or result.get('buildExit') != 0:
        raise ValueError('Successful configure and build receipts required')
    source = a.source_root.resolve()
    manifest = json.loads(a.source_manifest.read_text(encoding='utf-8-sig'))
    for row in manifest['files']:
        path = (source / row['path']).resolve()
        if not path.is_relative_to(source) or not path.is_file():
            raise ValueError('Unsafe/missing source manifest path')
        if path.stat().st_size != row['bytes'] or sha(path) != row['sha256']:
            raise ValueError('Source changed after pre-build snapshot: ' + row['path'])
    roots = {'PRODUCT_SOURCE': source, 'BUILD_GENERATED': a.build_dir.resolve()}
    for entry in a.root:
        name, directory = entry.split('=', 1)
        if not re.fullmatch('[A-Z][A-Z0-9_]*', name) or name in roots:
            raise ValueError('Invalid/duplicate logical root name')
        roots[name] = Path(directory).resolve()
    tracked = set()
    for log in a.build_dir.rglob('*.read.*.tlog'):
        if 'CompilerIdCXX' in str(log):
            continue
        raw = log.read_bytes()
        text = raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8')
        for line in text.splitlines():
            if line and not line.startswith('^'):
                file = Path(line)
                if file.is_file():
                    tracked.add(file.resolve())
    if not tracked:
        raise ValueError('No actual MSBuild read inputs found')
    private, logical = [], {}
    for file in sorted(tracked):
        digest = sha(file)
        private.append({'path': str(file), 'bytes': file.stat().st_size, 'sha256': digest})
        for name, root in roots.items():
            if file.is_relative_to(root):
                logical.setdefault(name, []).append({'relativePath': file.relative_to(root).as_posix(), 'sha256': digest})
                break
        else:
            raise ValueError('Unmapped compile input; add a logical --root: ' + str(file))
    a.out.mkdir(parents=True, exist_ok=False)
    write(a.out / 'BUILD-TRACKED-INPUTS-LOCAL-ONLY.json', private)
    write(a.out / 'DEPENDENCIES.lock.json', {'schemaVersion': 1, 'trackedInputCount': len(private), 'externalFilesByRoot': logical, 'rootMapping': {key: key for key in roots}})
    proof = {'baseCommit': a.base_commit, 'dllSha256': sha(a.dll), 'dllSize': a.dll.stat().st_size,
             'sourceManifestSha256': sha(a.source_manifest), 'configureExit': 0, 'buildExit': 0,
             'buildInputCount': len(private), 'operatorValidationRequired': 'Confirm actual DLL, build receipts, baseline, compile-source snapshot and distribution-source differences. This script binds bytes only.'}
    proof_path = a.out / 'BUILD-BINDING-PROOF.json'
    write(proof_path, proof)
    binding = {key: proof[key] for key in ('baseCommit', 'dllSha256', 'dllSize', 'sourceManifestSha256')}
    binding['buildProofSha256'] = sha(proof_path)
    write(a.out / 'build-binding.json', binding)
    print(json.dumps({'trackedInputCount': len(private), 'sourceFilesVerified': len(manifest['files']), 'dllSha256': proof['dllSha256'], 'buildExecuted': False, 'localOnlyListMustRemainPrivate': True}))

if __name__ == '__main__':
    main()
