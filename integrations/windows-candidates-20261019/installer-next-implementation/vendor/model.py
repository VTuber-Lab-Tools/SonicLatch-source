"""In-memory transaction specification, NOT a Windows installer or security boundary."""
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class Action(str, Enum):
    CREATE = 'Create'
    REPLACE = 'Replace'
    REMOVE = 'RemoveObsolete'
    UNCHANGED = 'Unchanged'


class Crash(Exception):
    pass


class Unsafe(Exception):
    pass


@dataclass(frozen=True)
class Step:
    path: str
    action: Action
    before: Optional[bytes]
    after: Optional[bytes]


@dataclass
class FakeFS:
    files: dict[str, bytes]
    receipt: Optional[bytes]
    registration: Optional[bytes]
    root_identity: str = 'fixture-root'
    registration_owned: bool = True


@dataclass
class Durable:
    steps: tuple[Step, ...]
    old_receipt: Optional[bytes]
    new_receipt: Optional[bytes]
    old_registration: Optional[bytes]
    target_registration: Optional[bytes]
    root_identity: str
    backups: dict[str, bytes] = field(default_factory=dict)
    journal: list[str] = field(default_factory=list)
    state: str = 'Preflight'
    direction: str = 'apply'
    started: bool = False
    terminal: Optional[str] = None


@dataclass(frozen=True)
class Result:
    code: int
    payload: str
    management: str
    next_action: str


class Engine:
    """All 'durable' objects are RAM; atomicity and trust are explicit test axioms.

    fault_index is a zero-based BEFORE/AFTER event index. Reopening an Engine
    over the same FakeFS/Durable emulates process restart, not power-loss I/O.
    """
    def __init__(self, fs, durable, fault_index=None):
        self.fs, self.d = fs, durable
        self.fault_index = fault_index
        self.events = []

    @classmethod
    def prepare(cls, fs, steps, new_receipt, target_registration,
                trusted_paths, fault_index=None):
        # trusted_paths is supplied by the test fixture, NOT derived from receipt.
        steps = tuple(steps)
        seen = set()
        for s in steps:
            if (s.path in seen or s.path not in trusted_paths or
                not s.path.startswith('payload/') or '\\' in s.path or
                any(p in ('', '.', '..') for p in s.path.split('/'))):
                raise Unsafe('untrusted or duplicate path')
            seen.add(s.path)
            valid = {
                Action.CREATE: s.before is None and s.after is not None,
                Action.REPLACE: s.before is not None and s.after is not None and s.before != s.after,
                Action.REMOVE: s.before is not None and s.after is None,
                Action.UNCHANGED: s.before == s.after,
            }
            if not valid.get(s.action, False) or fs.files.get(s.path) != s.before:
                raise Unsafe('invalid plan or modified baseline')
        if not fs.registration_owned:
            raise Unsafe('registration ownership unproven')
        d = Durable(steps, fs.receipt, new_receipt, fs.registration,
                    target_registration, fs.root_identity)
        return cls(fs, d, fault_index)

    def event(self, name):
        self.events.append(name)
        if len(self.events) - 1 == self.fault_index:
            raise Crash(name)

    def boundary(self, name, fn):
        self.event('before:' + name)
        fn()
        self.event('after:' + name)

    def log(self, entry):
        def append():
            self.d.journal.append(entry)
            if entry in ('committed', 'rolled-back'):
                self.d.terminal = entry
        self.boundary('journal:' + entry, append)

    def guard(self):
        if self.fs.root_identity != self.d.root_identity:
            raise Unsafe('root identity changed')

    def matches(self, side):
        return all(self.fs.files.get(s.path) == getattr(s, side) for s in self.d.steps)

    def set_file(self, path, content):
        self.guard()
        if content is None:
            self.fs.files.pop(path, None)
        else:
            self.fs.files[path] = content

    def apply(self):
        if self.d.started or self.d.terminal or self.d.journal or self.d.direction != 'apply':
            raise Unsafe('transaction already used; prepare a fresh transaction')
        self.d.started = True
        self.guard()
        self.log('staging-intent')
        self.d.state = 'Staged'
        self.log('staged')
        for s in self.d.steps:
            if s.action != Action.UNCHANGED and s.before is not None:
                self.log('backup-intent:' + s.path)
                def backup(s=s):
                    if self.fs.files.get(s.path) != s.before:
                        raise Unsafe('baseline changed before backup')
                    self.d.backups[s.path] = s.before
                self.boundary('backup:' + s.path, backup)
                self.log('backup-done:' + s.path)
        self.d.state = 'BackedUp'
        self.log('backed-up')
        for s in self.d.steps:
            if s.action == Action.UNCHANGED:
                continue
            self.log('apply-intent:' + s.path)
            self.d.state = 'Applying'
            def mutate(s=s):
                if self.fs.files.get(s.path) != s.before:
                    raise Unsafe('baseline changed before mutation')
                self.set_file(s.path, s.after)
            self.boundary('apply:' + s.path, mutate)
            self.boundary('verify:' + s.path,
                          lambda s=s: self.require(self.fs.files.get(s.path) == s.after))
            self.log('apply-done:' + s.path)
        self.boundary('verify-payload', lambda: self.require(self.matches('after')))
        self.d.state = 'Verified'
        self.log('receipt-intent')
        def publish():
            self.guard()
            self.require(self.fs.receipt == self.d.old_receipt and self.matches('after'))
            self.fs.receipt = self.d.new_receipt
        self.boundary('receipt-publish', publish)
        self.log('receipt-done')
        self.log('committed')
        self.d.state = 'Committed'
        return self.repair_management()

    @staticmethod
    def require(condition):
        if not condition:
            raise Unsafe('verification mismatch')

    def recovery_required(self):
        self.d.state = 'RecoveryRequired'
        return Result(31, 'mixed', 'recovery-required', 'preserve evidence; inspect conflict')

    def repair_management(self, fail_registration=False):
        try:
            self.guard()
            self.require(self.d.terminal != 'rolled-back' and self.d.direction != 'rollback')
            self.require(self.fs.receipt == self.d.new_receipt and self.matches('after'))
            self.require(self.fs.registration_owned)
            if self.fs.registration not in (self.d.old_registration, self.d.target_registration):
                raise Unsafe('registration changed by another writer')
            # Seal commit-equivalent recovery before management work.
            self.d.terminal = 'committed'
            # Fresh attempt, even if payload was already committed; never recopies payload.
            self.log('management-attempt')
            self.log('registration-intent')
            def register():
                self.guard()
                if fail_registration:
                    raise OSError('injected registration failure')
                self.fs.registration = self.d.target_registration
            self.boundary('registration-write', register)
            self.log('registration-done')
            self.boundary('verify-management', lambda: self.require(
                self.fs.registration == self.d.target_registration))
            self.log('management-complete')
            self.d.state = 'Committed'
            return Result(0, 'removed' if self.d.new_receipt is None else 'committed', 'complete', 'none')
        except OSError:
            return Result(32 if self.d.new_receipt is None else 33,
                          'removed' if self.d.new_receipt is None else 'committed',
                          'registration-incomplete', 'retry management only')
        except Unsafe:
            return self.recovery_required()

    def recover(self):
        try:
            self.guard()
            terminal = self.terminal_result()
            if terminal is not None:
                return terminal
            # A rollback that already started must not be mistaken for a commit.
            if self.d.direction != 'rollback' and self.fs.receipt == self.d.new_receipt and self.matches('after'):
                self.d.state = 'Committed'
                return self.repair_management()
            return self.rollback()
        except Unsafe:
            return self.recovery_required()

    def terminal_result(self):
        if self.d.terminal == 'committed':
            # Later drift is a new operation, never permission to revert this release.
            return self.repair_management()
        if self.d.terminal == 'rolled-back':
            self.require(self.matches('before') and self.fs.receipt == self.d.old_receipt
                         and self.fs.registration == self.d.old_registration)
            return Result(30, 'restored', 'complete', 'prepare fresh transaction to retry')
        return None

    def rollback(self):
        try:
            self.guard()
            terminal = self.terminal_result()
            if terminal is not None:
                return terminal
            # Incomplete-transaction recovery only; no committed-version revert API.
            if self.d.direction != 'rollback' and self.fs.receipt == self.d.new_receipt and self.matches('after'):
                return self.repair_management()
            self.log('rollback-intent')
            self.d.direction = 'rollback'
            self.d.state = 'RollingBack'
            for s in reversed(self.d.steps):
                self.log('undo-intent:' + s.path)
                def undo(s=s):
                    self.guard()
                    current = self.fs.files.get(s.path)
                    if current == s.before:
                        return
                    if s.action == Action.UNCHANGED or current != s.after:
                        raise Unsafe('external change retained')
                    if s.before is not None:
                        self.require(self.d.backups.get(s.path) == s.before)
                    self.set_file(s.path, s.before)
                self.boundary('undo:' + s.path, undo)
                self.log('undo-done:' + s.path)
            self.log('receipt-restore-intent')
            def restore_receipt():
                self.guard()
                self.require(self.fs.receipt in (self.d.old_receipt, self.d.new_receipt))
                self.fs.receipt = self.d.old_receipt
            self.boundary('receipt-restore', restore_receipt)
            self.log('receipt-restored')
            self.boundary('verify-baseline', lambda: self.require(
                self.matches('before') and self.fs.receipt == self.d.old_receipt
                and self.fs.registration == self.d.old_registration))
            self.log('rolled-back')
            self.d.state = 'RolledBack'
            return Result(30, 'restored', 'complete', 'retry original operation')
        except Unsafe:
            return self.recovery_required()
