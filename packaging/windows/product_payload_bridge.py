"""Frozen candidate-to-plan connection; NOT a product executor or trust authority.

No filesystem mutation, process launch, registry changes, OBS control or CLI.
The existing native fixture cannot consume this schema. Its gates stay intact.
"""
from dataclasses import dataclass, field
import hashlib
import json
import re
from types import MappingProxyType
from collections.abc import Mapping
import candidate_package as candidate
from plan_core import Package, File, Image, build_plan


class ConnectionError(ValueError):
    pass


def demand(value, message):
    if not value:
        raise ConnectionError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def image(data):
    return None if data is None else Image(sha(data), len(data))


def image_record(value):
    return None if value is None else {'sha256': value.sha256, 'size': value.size}


@dataclass(frozen=True)
class FrozenPreparation:
    """Immutable bytes retained for subsequent protected stage/backup creation.

    'blobs' is a digest-addressed byte map, not existing trusted stage paths.
    'record' retains operation even when uninstall has only Unchanged steps.
    No constructor/function grants execution authority.
    """
    record: bytes
    record_sha256: str
    plan: object
    blobs: Mapping
    execution_enabled: bool = field(default=False, init=False)
    publication_authorized: bool = field(default=False, init=False)


def checked_bundle(bundle, pin):
    demand(isinstance(pin, str) and re.fullmatch('[0-9a-f]{64}', pin), 'manifest pin required')
    demand(type(bundle) is candidate.Bundle and type(bundle.manifest) is bytes,
           'verified candidate Bundle required')
    demand(bundle.identity == pin and sha(bundle.manifest) == pin, 'manifest bytes/pin disagree')
    doc = candidate.strict_json(bundle.manifest)
    rows = candidate.validate_document(doc)
    demand(candidate.canonical(doc) == bundle.manifest, 'manifest not canonical')
    payload = {}
    demand(type(bundle.payload) is tuple, 'immutable bundle payload required')
    for pair in bundle.payload:
        demand(type(pair) is tuple and len(pair) == 2, 'payload pair invalid')
        path, data = pair
        demand(type(path) is str and type(data) is bytes and path not in payload,
               'duplicate/mutable payload')
        payload[path] = data
    demand(set(payload) == {r['relativePath'] for r in rows}, 'payload inventory mismatch')
    for row in rows:
        data = payload[row['relativePath']]
        demand(len(data) == row['size'] and sha(data) == row['sha256'], 'payload bytes changed')
    package = Package(pin, tuple(File(r['relativePath'], Image(r['sha256'], r['size'])) for r in rows))
    return package, payload, doc


def prepare(*, operation, transaction_id, snapshots, new_bundle=None, new_pin=None,
            old_bundle=None, old_pin=None, old_receipt=None,
            trusted_new=frozenset(), trusted_old=frozenset(), predecessors=frozenset(),
            version_relation=None, management_complete=False):
    """Connect verified candidate bytes to the existing reviewed pure plan core.

    snapshots: every new/old-owned destination must be explicitly observed.
    bytes means present; None means observed absent; omitted means uninspected.
    Snapshot collection, root identity, manifest/receipt authentication and
    allowed predecessor policy remain the caller's separate responsibility.
    Trusted sets must come from that policy, NEVER derive them from bundle pins.
    """
    demand(isinstance(transaction_id, str) and re.fullmatch('[0-9a-f]{32}', transaction_id),
           'explicit transaction identity required')
    demand(type(snapshots) is dict, 'explicit complete destination snapshots required')
    if old_bundle is None:
        demand(old_pin is None and old_receipt is None, 'unexpected old context')
        old, old_data, old_doc = None, {}, None
    else:
        demand(type(old_receipt) is bytes and 0 < len(old_receipt) <= candidate.LIMITS.max_json_bytes,
               'bounded actual old receipt bytes required')
        old, old_data, old_doc = checked_bundle(old_bundle, old_pin)
    if new_bundle is None:
        demand(new_pin is None, 'unexpected new pin')
        new, new_data, new_doc = None, {}, None
    else:
        new, new_data, new_doc = checked_bundle(new_bundle, new_pin)
    required = set(old_data) | set(new_data)
    demand(required <= set(snapshots), 'destination not inspected; missing is not absent')
    demand(len(snapshots) <= candidate.LIMITS.max_entries, 'snapshot count limit')
    observed = []
    total = 0
    for path, content in snapshots.items():
        candidate.role(path)  # Exact SonicLatch/ prefix once; never profile/scene/other-plugin paths.
        demand(content is None or type(content) is bytes, 'snapshot must be immutable bytes or absent')
        if content is not None:
            total += len(content)
            demand(len(content) <= candidate.LIMITS.max_file_bytes and total <= candidate.LIMITS.max_total_bytes,
                   'snapshot size limit')
            observed.append(File(path, image(content)))
    plan = build_plan(operation=operation, old=old, new=new, observed=tuple(observed),
                      trusted_new=trusted_new, trusted_old=trusted_old, predecessors=predecessors,
                      version_relation=version_relation, management_complete=management_complete)
    blobs = {}
    steps = []
    for step in plan.steps:
        before = snapshots[step.path]
        after = new_data.get(step.path)
        demand(image(before) == step.before and image(after) == step.after, 'plan/byte disagreement')
        refs = {}
        for side, content in (('before', before), ('after', after)):
            if content is None:
                refs[side] = None
            else:
                key = sha(content)
                demand(key not in blobs or blobs[key] == content, 'digest collision')
                blobs[key] = content
                refs[side] = key
        steps.append({'path': step.path, 'action': step.action.value,
                      'before': image_record(step.before), 'after': image_record(step.after),
                      'backupBlob': refs['before'], 'stageBlob': refs['after']})
    # Receipt mutation must remain explicit even if there are zero payload changes.
    receipt_intent = 'remove-owned-receipt' if operation == 'uninstall' else 'publish-new-owned-receipt'
    doc = {'schemaVersion': 1, 'kind': 'frozen-candidate-preparation',
           'executionEnabled': False, 'publicationAuthorized': False, 'releaseAllowed': False,
           'transactionId': transaction_id, 'operation': operation, 'route': plan.route,
           'oldManifestSha256': old_pin, 'newManifestSha256': new_pin,
           'oldReceiptSha256': None if old_receipt is None else sha(old_receipt),
           'oldSourceContext': None if old_doc is None else old_doc['sourceContext'],
           'newSourceContext': None if new_doc is None else new_doc['sourceContext'],
           'moduleId': (new_doc or old_doc)['moduleId'],
           'productVersion': (new_doc or old_doc)['productVersion'],
           'receiptIntent': receipt_intent, 'steps': steps,
           'retainedUnknown': list(plan.retained_unknown),
           'rootIdentity': None, 'protectedStageIdentity': None, 'trustAuthenticated': False,
           'requiredBeforeExecution': ['authenticate manifest/receipt/predecessor policy',
               'pin normal/portable root and parent handles', 'OBS/runtime preflight',
               'create protected same-volume stage and verified backups from retained bytes',
               'persist root-bound plan and journal before any mutation',
               'recover from that frozen plan, not recomputed live input',
               'connect native helper/IPC/registration and receipt terminal handling']}
    record = candidate.canonical(doc)
    demand(len(record) <= candidate.LIMITS.max_json_bytes, 'frozen record size limit')
    return FrozenPreparation(record, sha(record), plan, MappingProxyType(blobs))


def encode_native_plan(preparation, *, root_identity, product_id,
                       predecessor_evidence='', old_receipt=None, new_receipt=None):
    """Serialize the retained plan into product_transaction.hpp's exact format.

    A byte-format bridge only: root identity and receipt bytes still require
    independent protected-host authentication. Returned bytes never authorize
    WindowsStore. Source identity is SHA-256(canonical(sourceContext)).
    """
    demand(type(preparation) is FrozenPreparation and
           sha(preparation.record) == preparation.record_sha256, 'frozen input required')
    doc = candidate.strict_json(preparation.record)
    demand(isinstance(root_identity, str) and re.fullmatch(r'[0-9]+:[0-9]+:[0-9]+', root_identity),
           'pinned Windows volume/file identity required')
    demand(isinstance(product_id, str) and product_id, 'authenticated product id required')
    op = doc['operation']
    demand(op in ('install', 'upgrade', 'repair', 'uninstall'), 'native operation')
    if op == 'install':
        demand(old_receipt is None and predecessor_evidence == '', 'install old receipt')
    else:
        demand(type(old_receipt) is bytes and sha(old_receipt) == doc['oldReceiptSha256'],
               'retained old receipt bytes differ')
        demand(re.fullmatch('[0-9a-f]{64}', predecessor_evidence), 'predecessor evidence required')
    demand((new_receipt is None) == (op == 'uninstall'), 'new receipt presence')
    blobs = dict(preparation.blobs)
    out = bytearray()
    def number(value):
        demand(type(value) is int and 0 <= value < (1 << 64), 'native integer')
        out.extend(value.to_bytes(8, 'little'))
    def text(value):
        demand(type(value) is str, 'native string')
        encoded = value.encode('utf-8')
        demand(len(encoded) <= 4096, 'native string limit')
        number(len(encoded)); out.extend(encoded)
    def ref(value):
        number(0 if value is None else 1)
        if value is not None:
            demand(re.fullmatch('[0-9a-f]{64}', value['sha256']) and
                   0 <= value['size'] <= 32 * 1024 * 1024, 'native reference limit')
            text(value['sha256']); number(value['size'])
    def receipt(data):
        if data is None:
            ref(None); return
        demand(type(data) is bytes and 0 < len(data) <= candidate.LIMITS.max_json_bytes,
               'bounded immutable receipt bytes required')
        key = sha(data)
        demand(key not in blobs or blobs[key] == data, 'receipt digest collision')
        blobs[key] = data
        ref({'sha256': key, 'size': len(data)})
    source = doc['newSourceContext'] or doc['oldSourceContext']
    text('SonicLatch-product-plan-v1')
    for value in (doc['transactionId'], root_identity, doc['moduleId'], product_id,
                  doc['productVersion'], doc['oldManifestSha256'] or '',
                  doc['newManifestSha256'] or '', sha(candidate.canonical(source)),
                  predecessor_evidence):
        text(value)
    number(('install', 'upgrade', 'repair', 'uninstall').index(op))
    receipt(old_receipt); receipt(new_receipt)
    steps = doc['steps']
    demand(0 < len(steps) <= 512, 'native step count')
    number(len(steps)); total = 0
    for step in steps:
        candidate.role(step['path'])
        text(step['path']); ref(step['before']); ref(step['after'])
        for side in ('before', 'after'):
            value = step[side]
            if value is not None:
                data = blobs.get(value['sha256'])
                demand(type(data) is bytes and len(data) == value['size'] and
                       sha(data) == value['sha256'], 'native blob differs')
                total += value['size']
    demand(total <= 128 * 1024 * 1024 and len(out) <= 1024 * 1024, 'native plan bound')
    result = bytes(out)
    return result, sha(result), MappingProxyType(blobs)
