"""Pure change-plan core; no filesystem I/O and no production trust authority.

The caller supplies authenticated manifests and a pinned-root inventory. This
module validates their consistency, not their authenticity. Fixture allowlists
are explicitly injected; defaults authorize no identities. Version ordering is
supplied by the caller rather than inventing a production version schema.
"""
from dataclasses import dataclass
from enum import Enum
import re
from vendor.validator import path_key, Invalid


class PlanError(ValueError):
    pass


class Action(str, Enum):
    CREATE = 'Create'
    REPLACE = 'Replace'
    REMOVE = 'RemoveObsolete'
    UNCHANGED = 'Unchanged'


@dataclass(frozen=True)
class Image:
    sha256: str
    size: int

    def __post_init__(self):
        if (not isinstance(self.sha256, str) or
                re.fullmatch('[0-9a-f]{64}', self.sha256) is None or
                type(self.size) is not int or self.size < 0):
            raise PlanError('invalid hash or size')


@dataclass(frozen=True)
class File:
    path: str
    image: Image


@dataclass(frozen=True)
class Package:
    identity: str  # externally authenticated manifest SHA-256, not a receipt claim
    files: tuple[File, ...]


@dataclass(frozen=True)
class Step:
    path: str
    action: Action
    before: Image | None
    after: Image | None


@dataclass(frozen=True)
class Plan:
    route: str  # payload, missing-file-repair, management-only, verified-no-op
    steps: tuple[Step, ...]
    retained_unknown: tuple[str, ...]
    publication_authorized: bool = False


def index(rows):
    result = {}
    for row in rows:
        if not isinstance(row, File) or not isinstance(row.image, Image):
            raise PlanError('invalid inventory row')
        try:
            key = path_key(row.path)
        except (Invalid, UnicodeError) as exc:
            raise PlanError('unsafe path') from exc
        if key in result:
            raise PlanError('duplicate or aliased path')
        result[key] = row
    keys = set(result)
    for key in keys:
        parts = key.split('/')
        if any('/'.join(parts[:i]) in keys for i in range(1, len(parts))):
            raise PlanError('file/ancestor collision')
    return result


def package_index(package, allowed):
    if (not isinstance(package, Package) or
            not isinstance(package.identity, str) or
            re.fullmatch('[0-9a-f]{64}', package.identity) is None or
            package.identity not in allowed or
            not isinstance(package.files, tuple) or not package.files):
        raise PlanError('package identity untrusted or empty manifest')
    return index(package.files)


def build_plan(*, operation, old=None, new=None, observed=(),
               trusted_new=frozenset(), trusted_old=frozenset(),
               predecessors=frozenset(), version_relation=None,
               management_complete=False):
    """Generate a complete immutable plan from caller-owned snapshots.

    observed contains PRESENT files only; missing is represented by absence.
    version_relation is 'same', 'newer', or 'older', established outside this
    pure adapter. Actual Windows root/process/provenance checks remain required.
    No execution API, environment probe, release-gate bypass, or installer CLI.
    """
    if operation not in ('install', 'upgrade', 'repair', 'uninstall'):
        raise PlanError('unsupported operation')
    if type(management_complete) is not bool:
        raise PlanError('invalid management state')
    if operation == 'install':
        if old is not None or new is None:
            raise PlanError('install requires new package and no old package')
    elif operation == 'uninstall':
        if old is None or new is not None:
            raise PlanError('uninstall requires old package only')
    elif old is None or new is None:
        raise PlanError('upgrade/repair requires both manifests')
    before = package_index(old, trusted_old) if old is not None else {}
    after = package_index(new, trusted_new) if new is not None else {}
    actual = index(observed)
    same = old is not None and new is not None and old.identity == new.identity
    if old is not None and new is not None:
        if version_relation not in ('same', 'newer', 'older'):
            raise PlanError('version comparison missing')
        if version_relation == 'older':
            raise PlanError('downgrade refused')
        if same != (version_relation == 'same'):
            raise PlanError('same version/different manifest or inconsistent identity')
        if same and before != after:
            raise PlanError('same identity with inconsistent file list')
        if operation == 'repair' and not same:
            raise PlanError('repair must target the same manifest')
        if not same and old.identity not in predecessors:
            raise PlanError('predecessor not allowlisted')
    # Reject spelling aliases and file/ancestor conflicts across all snapshots.
    union = {}
    for table in (before, after, actual):
        for key, row in table.items():
            if key in union and union[key].path != row.path:
                raise PlanError('cross-snapshot path alias')
            union[key] = row
    index(tuple(union.values()))
    steps = []
    missing = False
    for key in sorted(before.keys() | after.keys()):
        prior, target, current = before.get(key), after.get(key), actual.get(key)
        if prior is None and current is not None:
            # Identical bytes at an unowned path still do not establish ownership.
            raise PlanError('unowned destination collision')
        if prior is not None and current is not None and current.image != prior.image:
            raise PlanError('owned file modified; preserve it')
        b = current.image if current else None
        a = target.image if target else None
        missing |= prior is not None and current is None and target is not None
        action = (Action.UNCHANGED if a == b else Action.CREATE if b is None
                  else Action.REMOVE if a is None else Action.REPLACE)
        steps.append(Step((target or prior).path, action, b, a))
    changed = any(s.action != Action.UNCHANGED for s in steps)
    if changed:
        route = 'missing-file-repair' if same and missing else 'payload'
    else:
        # Different package identity still requires receipt publication, even if
        # every file is byte-identical. It cannot be a verification no-op.
        route = ('payload' if new is not None and not same else
                 'verified-no-op' if management_complete else 'management-only')
    return Plan(route, tuple(steps), tuple(sorted(
        row.path for key, row in actual.items() if key not in before and key not in after)))
