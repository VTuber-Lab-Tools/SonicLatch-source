"""Product-manifest/byte binding using the existing bounded archive inventory.

The fixture validator is unchanged. Product bytes and corresponding-source
identity are explicit; package preparation does not authorize installation or
publication. No Windows I/O, ACL, registry, installer execution or signing.
"""
from pathlib import Path
import io
import json
import os
import stat
import uuid
import zipfile
import candidate_package as candidate

PackageError = candidate.PackageError
Bundle = candidate.Bundle
LIMITS = candidate.LIMITS
sha = candidate.sha
canonical = candidate.canonical
strict_json = candidate.strict_json
FIELDS = {'schemaVersion', 'artifactKind', 'fixtureOnly', 'publicationAuthorized',
          'productId', 'moduleId', 'productVersion', 'layoutVersion', 'architecture',
          'copyrightHolder', 'licenseExpression', 'sourceContext', 'sourceArchive', 'files'}


def require(ok, reason):
    if not ok:
        raise PackageError(reason)


def validate_document(doc):
    require(type(doc) is dict and set(doc) == FIELDS, 'product manifest fields')
    require(type(doc['schemaVersion']) is int and doc['schemaVersion'] == 1 and
            doc['artifactKind'] == 'product-package' and doc['fixtureOnly'] is False and
            doc['publicationAuthorized'] is False, 'product schema/publication gate')
    require((doc['productId'], doc['moduleId'], doc['productVersion'], doc['layoutVersion'],
             doc['architecture']) == ('SonicLatch', 'SonicLatch', '0.1.0', 'obs33-flat-v1', 'amd64'),
            'product identity/layout mismatch')
    require(doc['copyrightHolder'] == 'VTuber-Lab-Tools' and
            doc['licenseExpression'] == 'GPL-3.0-or-later', 'approved copyright/license required')
    candidate.source_context(doc['sourceContext'])
    require(doc['sourceContext']['syntheticPayload'] is False, 'product cannot use synthetic payload')
    source = doc['sourceArchive']
    require(type(source) is dict and set(source) == {'filename', 'sha256', 'size'},
            'corresponding-source archive fields')
    require(type(source['filename']) is str and source['filename'].endswith('.zip') and
            candidate.path_key(source['filename']) == source['filename'].lower() and
            '/' not in source['filename'] and '\\' not in source['filename'], 'source archive basename')
    require(type(source['sha256']) is str and candidate.HASH.fullmatch(source['sha256']) and
            type(source['size']) is int and 0 < source['size'] <= 256 * 1024 * 1024,
            'bounded corresponding-source hash/size required')
    rows = doc['files']
    require(type(rows) is list and 0 < len(rows) < LIMITS.max_entries, 'product inventory count')
    keys = set()
    for row in rows:
        require(type(row) is dict and set(row) == {'relativePath', 'sha256', 'size', 'role', 'componentLicenseId'},
                'product file fields')
        path = row['relativePath']; key = candidate.path_key(path)
        require(key not in keys and candidate.role(path) == row['role'] and
                row['componentLicenseId'] == 'GPL-3.0-or-later', 'product path/role/license')
        require(type(row['size']) is int and 0 <= row['size'] <= LIMITS.max_file_bytes and
                type(row['sha256']) is str and candidate.HASH.fullmatch(row['sha256']), 'product file hash/size')
        keys.add(key)
    for key in keys:
        require(not any('/'.join(key.split('/')[:n]) in keys for n in range(1, len(key.split('/')))),
                'product ancestor collision')
    require('soniclatch/soniclatch.dll' in keys and
            sum(row['size'] for row in rows) <= LIMITS.max_total_bytes, 'product DLL/total size')
    return rows


def verify_stream(stream, expected_identity=None):
    inventory = None
    try:
        inventory = candidate.Inventory(stream, LIMITS)
        inventory.hash_all()
        manifest = inventory.read_small('product-manifest.json', LIMITS.max_json_bytes)
        doc = strict_json(manifest); rows = validate_document(doc)
        require(manifest == canonical(doc), 'product manifest not canonical')
        identity = sha(manifest)
        require(expected_identity is None or identity == expected_identity, 'caller-pinned product manifest differs')
        require(not inventory.directories and set(inventory.entries) == {'product-manifest.json'} |
                {'payload/' + candidate.path_key(row['relativePath']) for row in rows},
                'extra/missing product archive entry')
        payload = []
        for row in rows:
            path = row['relativePath']; key = 'payload/' + candidate.path_key(path)
            entry = inventory.entries[key]
            data = inventory.read_small(key, LIMITS.max_file_bytes)
            require(entry.orig_filename == 'payload/' + path and entry.file_size == row['size'] and
                    len(data) == row['size'] and sha(data) == row['sha256'] and
                    inventory.hashes[key] == row['sha256'], 'actual product bytes differ')
            payload.append((path, data))
        return Bundle(identity, manifest, tuple(payload))
    except (candidate.Invalid, KeyError, TypeError, ValueError, zipfile.BadZipFile, EOFError) as exc:
        if isinstance(exc, PackageError):
            raise
        raise PackageError(str(exc)) from exc
    finally:
        if inventory is not None:
            inventory.close()


def verify(path, expected_identity=None):
    with Path(path).open('rb') as stream:
        return verify_stream(stream, expected_identity)


def source_identity(path, expected_hash):
    """Read a caller-pinned source zip without extracting, executing or sharing it."""
    require(type(expected_hash) is str and candidate.HASH.fullmatch(expected_hash), 'source pin required')
    path = Path(path); require(not path.is_symlink(), 'source archive symlink refused')
    with path.open('rb') as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and before.st_nlink == 1 and
                not getattr(before, 'st_file_attributes', 0) & 0x400 and
                0 < before.st_size <= 256 * 1024 * 1024, 'source archive type/size')
        import hashlib
        digest = hashlib.sha256()
        size = 0
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk: break
            digest.update(chunk); size += len(chunk)
            require(size <= 256 * 1024 * 1024, 'source archive growth')
        after = os.fstat(stream.fileno())
    require((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) ==
            (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) and
            size == before.st_size and digest.hexdigest() == expected_hash, 'source archive differs from pin')
    return {'filename': path.name, 'sha256': expected_hash, 'size': size}


def build(*, context, inputs, source_archive, source_sha256, output):
    """Use explicit file/hash pairs; never infer a payload by recursive glob."""
    candidate.source_context(context)
    require(type(inputs) is dict and 0 < len(inputs) < LIMITS.max_entries, 'explicit product inputs required')
    payload = {}; rows = []; total = 0
    for path, spec in sorted(inputs.items()):
        require(type(spec) in (tuple, list) and len(spec) == 2, 'input source/hash pair')
        role = candidate.role(path); data = candidate.read_pinned(spec[0], spec[1]); total += len(data)
        require(total <= LIMITS.max_total_bytes, 'product input total limit')
        payload[path] = data
        rows.append({'relativePath': path, 'sha256': sha(data), 'size': len(data), 'role': role,
                     'componentLicenseId': 'GPL-3.0-or-later'})
    doc = {'schemaVersion': 1, 'artifactKind': 'product-package', 'fixtureOnly': False,
           'publicationAuthorized': False, 'productId': 'SonicLatch', 'moduleId': 'SonicLatch',
           'productVersion': '0.1.0', 'layoutVersion': 'obs33-flat-v1', 'architecture': 'amd64',
           'copyrightHolder': 'VTuber-Lab-Tools', 'licenseExpression': 'GPL-3.0-or-later',
           'sourceContext': context, 'sourceArchive': source_identity(source_archive, source_sha256),
           'files': rows}
    validate_document(doc)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_STORED, allowZip64=False) as archive:
        for name, data in [('product-manifest.json', canonical(doc))] + [('payload/' + p, payload[p]) for p in sorted(payload)]:
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0)); entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | 0o644) << 16; archive.writestr(entry, data)
    raw = buffer.getvalue(); verified = verify_stream(io.BytesIO(raw)); target = Path(output)
    stage = target.parent / ('.product-' + uuid.uuid4().hex + '.tmp')
    try:
        with stage.open('xb') as stream:
            stream.write(raw); stream.flush(); os.fsync(stream.fileno())
        if os.name == 'nt': os.rename(stage, target)
        else: os.link(stage, target)
    finally:
        if stage.exists(): stage.unlink()
    return verified


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive'); parser.add_argument('--expected-manifest-sha256', required=True)
    args = parser.parse_args()
    bundle = verify(args.archive, args.expected_manifest_sha256)
    print(json.dumps({'valid': True, 'manifestSha256': bundle.identity, 'files': len(bundle.payload),
                      'fixtureOnly': False, 'publicationAuthorized': False}))
