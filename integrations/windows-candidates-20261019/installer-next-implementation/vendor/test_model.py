import copy
import unittest
from model import Action as A, Crash, Engine, FakeFS, Step, Unsafe

CANARIES = {'user/scenes.json': b'scenes', 'user/profiles/main.ini': b'profile',
            'user/global.ini': b'global', 'user/offsets.json': b'offsets',
            'adjacent/other.dll': b'other plugin', 'payload/unknown.txt': b'unknown'}


def fixture(kind='upgrade'):
    if kind == 'install':
        steps = [Step('payload/new.dll', A.CREATE, None, b'new')]
        old, new = None, b'receipt-v2'
    elif kind == 'uninstall':
        steps = [Step('payload/old.dll', A.REMOVE, b'old', None)]
        old, new = b'receipt-v1', None
    elif kind == 'repair':
        # Original receipt claims file exists; actual baseline has it missing.
        steps = [Step('payload/missing.dll', A.CREATE, None, b'repaired'),
                 Step('payload/missing.dat', A.CREATE, None, b'repaired-data')]
        old = new = b'receipt-v1'
    else:
        steps = [Step('payload/new.dll', A.CREATE, None, b'new'),
                 Step('payload/main.dll', A.REPLACE, b'v1', b'v2'),
                 Step('payload/obsolete.dat', A.REMOVE, b'old data', None),
                 Step('payload/same.dat', A.UNCHANGED, b'same', b'same')]
        old, new = b'receipt-v1', b'receipt-v2'
    files = dict(CANARIES)
    files.update({s.path: s.before for s in steps if s.before is not None})
    fs = FakeFS(files, old, b'reg-v1' if old else None)
    e = Engine.prepare(fs, steps, new, b'reg-v2' if new else None, {s.path for s in steps})
    return e


class TransactionTests(unittest.TestCase):
    crash_counts = {}

    def canaries(self, fs):
        self.assertEqual({k: fs.files.get(k) for k in CANARIES}, CANARIES)

    def test_fault_every_forward_boundary_and_restart(self):
        for kind in ('install', 'upgrade', 'repair', 'uninstall'):
            trace = fixture(kind)
            self.assertEqual(trace.apply().code, 0)
            self.crash_counts['forward:' + kind] = len(trace.events)
            for n, label in enumerate(trace.events):
                with self.subTest(kind=kind, index=n, event=label):
                    e = fixture(kind)
                    baseline = copy.deepcopy(e.fs)
                    e.fault_index = n
                    with self.assertRaises(Crash):
                        e.apply()
                    self.canaries(e.fs)
                    restarted = Engine(e.fs, e.d)
                    result = restarted.recover()
                    self.assertIn(result.code, (0, 30))
                    self.canaries(e.fs)
                    if result.code == 30:
                        self.assertEqual(e.fs, baseline)
                    else:
                        self.assertTrue(restarted.matches('after'))
                        self.assertEqual(e.fs.receipt, e.d.new_receipt)
                        self.assertEqual(e.fs.registration, e.d.target_registration)
                    final = copy.deepcopy(e.fs)
                    self.assertIn(Engine(e.fs, e.d).recover().code, (0, 30))
                    self.assertEqual(e.fs, final)

    def before_receipt(self, kind='upgrade'):
        trace = fixture(kind)
        trace.apply()
        cut = trace.events.index('before:receipt-publish')
        e = fixture(kind)
        e.fault_index = cut
        with self.assertRaises(Crash):
            e.apply()
        return e

    def test_fault_every_rollback_boundary_and_restart(self):
        # Repair's unchanged receipt + fully repaired payload is intentionally a
        # commit-equivalent state, so exercise its rollback with a partial plan.
        for kind in ('install', 'upgrade', 'repair', 'uninstall'):
            if kind == 'repair':
                trace = fixture(kind)
                trace.apply()
                original = fixture(kind)
                original.fault_index = trace.events.index('after:apply:payload/missing.dll')
                with self.assertRaises(Crash): original.apply()
            else:
                original = self.before_receipt(kind)
            trace = Engine(copy.deepcopy(original.fs), copy.deepcopy(original.d))
            self.assertEqual(trace.recover().code, 30)
            self.crash_counts['rollback:' + kind] = len(trace.events)
            for n, label in enumerate(trace.events):
                with self.subTest(kind=kind, index=n, event=label):
                    fs, d = copy.deepcopy(original.fs), copy.deepcopy(original.d)
                    e = Engine(fs, d, n)
                    with self.assertRaises(Crash):
                        e.recover()
                    result = Engine(fs, d).recover()
                    self.assertEqual(result.code, 30)
                    self.assertEqual(fs, fixture(kind).fs)
                    self.canaries(fs)

    def test_partial_missing_file_repair_restores_absence(self):
        fs = FakeFS(dict(CANARIES), b'receipt', b'reg')
        steps = [Step('payload/a', A.CREATE, None, b'a'), Step('payload/b', A.CREATE, None, b'b')]
        e = Engine.prepare(fs, steps, b'receipt', b'reg', {s.path for s in steps})
        trace = copy.deepcopy(e)
        trace.apply()
        e.fault_index = trace.events.index('after:apply:payload/a')
        with self.assertRaises(Crash): e.apply()
        self.assertEqual(Engine(fs, e.d).recover().code, 30)
        self.assertEqual(fs.files, CANARIES)

    def test_third_party_files_preserved_each_undo_action(self):
        for path in ('payload/new.dll', 'payload/main.dll', 'payload/obsolete.dat', 'payload/same.dat'):
            e = self.before_receipt()
            e.fs.files[path] = b'external data'
            self.assertEqual(Engine(e.fs, e.d).recover().code, 31)
            self.assertEqual(e.fs.files[path], b'external data')
            self.canaries(e.fs)

    def test_replace_missing_is_not_safe_undo(self):
        e = self.before_receipt()
        del e.fs.files['payload/main.dll']
        self.assertEqual(Engine(e.fs, e.d).recover().code, 31)
        self.assertNotIn('payload/main.dll', e.fs.files)

    def test_corrupt_backup_and_receipt_preserved(self):
        e = self.before_receipt()
        e.d.backups['payload/obsolete.dat'] = b'corrupt'
        self.assertEqual(Engine(e.fs, e.d).recover().code, 31)
        self.assertNotIn('payload/obsolete.dat', e.fs.files)
        e = self.before_receipt()
        e.fs.receipt = b'foreign receipt'
        self.assertEqual(Engine(e.fs, e.d).recover().code, 31)
        self.assertEqual(e.fs.receipt, b'foreign receipt')

    def test_receipt_alone_or_journal_alone_not_success(self):
        for journal in ([], ['committed']):
            e = fixture()
            e.d.journal = journal
            e.fs.receipt = e.d.new_receipt
            e.fs.files['payload/main.dll'] = b'foreign'
            self.assertEqual(e.recover().code, 31)
            self.assertEqual(e.fs.files['payload/main.dll'], b'foreign')

    def test_root_change_blocks_all_recovery_mutation(self):
        e = self.before_receipt()
        e.fs.root_identity = 'another-root'
        baseline = copy.deepcopy(e.fs)
        self.assertEqual(Engine(e.fs, e.d).recover().code, 31)
        self.assertEqual(e.fs, baseline)

    def test_registration_failure_retry_and_every_retry_boundary(self):
        for kind in ('install', 'uninstall'):
            trace = fixture(kind)
            trace.apply()
            cut = trace.events.index('before:registration-write')
            e = fixture(kind)
            e.fault_index = cut
            with self.assertRaises(Crash): e.apply()
            retry = Engine(e.fs, e.d)
            before = copy.deepcopy(e.fs.files)
            result = retry.repair_management(fail_registration=True)
            self.assertEqual(result.code, 33 if kind == 'install' else 32)
            self.assertEqual(e.fs.files, before)
            original_fs, original_d = copy.deepcopy(e.fs), copy.deepcopy(e.d)
            good = Engine(copy.deepcopy(e.fs), copy.deepcopy(e.d))
            self.assertEqual(good.repair_management().code, 0)
            self.crash_counts['registration-retry:' + kind] = len(good.events)
            for n in range(len(good.events)):
                fs, d = copy.deepcopy(original_fs), copy.deepcopy(original_d)
                with self.assertRaises(Crash): Engine(fs, d, n).repair_management()
                self.assertEqual(Engine(fs, d).recover().code, 0)
                self.assertEqual(fs.files, before)
                self.canaries(fs)

    def test_modified_committed_payload_cannot_management_noop(self):
        e = fixture()
        e.apply()
        e.fs.files['payload/main.dll'] = b'external'
        self.assertEqual(e.repair_management().code, 31)
        del e.fs.files['payload/main.dll']
        self.assertEqual(e.repair_management().code, 31)

    def test_untrusted_path_collision_and_registration_rejected(self):
        for path in ('payload/../user/global.ini', '/payload/x', 'payload/x', 'payload//x'):
            fs = FakeFS(dict(CANARIES), None, None)
            with self.assertRaises(Unsafe):
                Engine.prepare(fs, [Step(path, A.CREATE, None, b'x')], b'r', b'g', set())
            self.canaries(fs)
        fs = FakeFS(dict(CANARIES), None, None)
        with self.assertRaises(Unsafe):
            Engine.prepare(fs, [Step('payload/unknown.txt', A.CREATE, None, b'x')], b'r', b'g', {'payload/unknown.txt'})
        self.canaries(fs)
        e = fixture()
        e.apply()
        e.fs.registration = b'foreign registration'
        self.assertEqual(e.repair_management().code, 31)
        self.assertEqual(e.fs.registration, b'foreign registration')

    def test_committed_repair_external_deletion_never_reverts_other_file(self):
        for entrypoint in ('recover', 'rollback'):
            e = fixture('repair')
            e.d.target_registration = e.d.old_registration
            self.assertEqual(e.apply().code, 0)
            del e.fs.files['payload/missing.dll']
            saved = copy.deepcopy(e.fs)
            for _ in range(3):
                self.assertEqual(getattr(Engine(e.fs, e.d), entrypoint)().code, 31)
                self.assertEqual(e.fs, saved)

    def test_terminal_transaction_reuse_requires_new_durable(self):
        for completed in ('committed', 'rolled-back'):
            e = fixture() if completed == 'committed' else self.before_receipt()
            if completed == 'committed':
                self.assertEqual(e.apply().code, 0)
            else:
                self.assertEqual(Engine(e.fs, e.d).recover().code, 30)
            saved = copy.deepcopy(e.fs)
            with self.assertRaises(Unsafe): Engine(e.fs, e.d).apply()
            self.assertEqual(e.fs, saved)
            self.assertEqual(Engine(e.fs, e.d).recover().code, 0 if completed == 'committed' else 30)
            self.assertEqual(e.fs, saved)
            if completed == 'rolled-back':
                fresh = Engine.prepare(e.fs, e.d.steps, e.d.new_receipt,
                                       e.d.target_registration, {s.path for s in e.d.steps})
                self.assertIsNot(fresh.d, e.d)
                self.assertEqual(fresh.apply().code, 0)
                installed = copy.deepcopy(e.fs)
                self.assertEqual(Engine(e.fs, fresh.d).recover().code, 0)
                self.assertEqual(e.fs, installed)

    @classmethod
    def tearDownClass(cls):
        print('\nFault injection boundaries:', cls.crash_counts)
        print('Total injected crashes:', sum(cls.crash_counts.values()))


if __name__ == '__main__':
    unittest.main(verbosity=2)
