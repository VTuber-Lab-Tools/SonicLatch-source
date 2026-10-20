import hashlib
import unittest
from dataclasses import replace
from plan_core import Action, File, Image, Package, PlanError, build_plan, index
from vendor.model import Action as ModelAction, Engine, FakeFS, Step as ModelStep, Crash

A, B = 'a' * 64, 'b' * 64

def image(data):
    return Image(hashlib.sha256(data).hexdigest(), len(data))

def file(path, data=b'old'):
    return File(path, image(data))

OLD = Package(A, (file('payload/module.dll'), file('payload/obsolete.txt')))
NEW = Package(B, (file('payload/module.dll', b'new'), file('payload/data/en.ini', b'locale')))


def plan(op='upgrade', old=OLD, new=NEW, observed=None, **kw):
    args = dict(operation=op, old=old, new=new,
                observed=old.files if observed is None and old else (observed or ()),
                trusted_new=frozenset((A, B)), trusted_old=frozenset((A, B)),
                predecessors=frozenset((A,)), version_relation='newer')
    args.update(kw)
    return build_plan(**args)


class PlanTests(unittest.TestCase):
    def test_upgrade_complete_actions(self):
        p = plan()
        self.assertEqual([s.action for s in p.steps], [Action.CREATE, Action.REPLACE, Action.REMOVE])
        self.assertEqual(p.route, 'payload')
        self.assertFalse(p.publication_authorized)

    def test_install(self):
        p = plan('install', old=None)
        self.assertTrue(all(s.action == Action.CREATE and s.before is None for s in p.steps))

    def test_install_identical_unknown_blocks(self):
        with self.assertRaisesRegex(PlanError, 'unowned'):
            plan('install', old=None, observed=NEW.files)

    def test_same_manifest_noop_requires_management(self):
        for complete, route in ((True, 'verified-no-op'), (False, 'management-only')):
            p = plan(new=OLD, version_relation='same', management_complete=complete)
            self.assertEqual(p.route, route)
            self.assertEqual(len(p.steps), 2)
            self.assertTrue(all(s.action == Action.UNCHANGED for s in p.steps))

    def test_missing_file_repair_actual_absence(self):
        p = plan('repair', new=OLD, observed=OLD.files[:1], version_relation='same')
        self.assertEqual(p.route, 'missing-file-repair')
        self.assertEqual(p.steps[1].action, Action.CREATE)
        self.assertIsNone(p.steps[1].before)

    def test_upgrade_same_manifest_missing_routes_repair(self):
        self.assertEqual(plan(new=OLD, observed=(), version_relation='same',
                              management_complete=True).route, 'missing-file-repair')

    def test_modified_file_refused_for_every_operation(self):
        for op in ('upgrade', 'repair', 'uninstall'):
            with self.subTest(op=op), self.assertRaisesRegex(PlanError, 'modified'):
                plan(op, new=None if op == 'uninstall' else OLD,
                     observed=(file('payload/module.dll', b'changed'),), version_relation='same')

    def test_unknown_neighbor_is_retained(self):
        unknown = file('payload/another-plugin.dll', b'neighbor')
        p = plan(observed=OLD.files + (unknown,))
        self.assertEqual(p.retained_unknown, (unknown.path,))
        self.assertNotIn(unknown.path, [s.path for s in p.steps])

    def test_uninstall_missing_files_not_recreated(self):
        p = plan('uninstall', new=None, observed=OLD.files[:1])
        self.assertEqual([s.action for s in p.steps], [Action.REMOVE, Action.UNCHANGED])
        self.assertIsNone(p.steps[1].before)
        self.assertIsNone(p.steps[1].after)

    def test_uninstall_already_removed_management_retry(self):
        for complete, route in ((False, 'management-only'), (True, 'verified-no-op')):
            self.assertEqual(plan('uninstall', new=None, observed=(),
                                  management_complete=complete).route, route)

    def test_new_identity_same_bytes_still_payload_receipt_update(self):
        p = plan(new=Package(B, OLD.files), management_complete=True)
        self.assertEqual(p.route, 'payload')
        self.assertTrue(all(s.action == Action.UNCHANGED for s in p.steps))

    def test_untrusted_and_empty_predecessor_refused(self):
        for setting in ('trusted_old', 'trusted_new', 'predecessors'):
            with self.subTest(setting=setting), self.assertRaises(PlanError):
                plan(**{setting: frozenset()})
        with self.assertRaises(PlanError):
            build_plan(operation='install', new=NEW)

    def test_invalid_version_relationship(self):
        for rel in ('same', 'older', None, 'unknown'):
            with self.subTest(rel=rel), self.assertRaises(PlanError):
                plan(version_relation=rel)
        with self.assertRaises(PlanError):
            plan(new=OLD, version_relation='newer')

    def test_repair_cannot_upgrade(self):
        with self.assertRaises(PlanError):
            plan('repair')

    def test_manifest_identity_cannot_hide_file_list_change(self):
        with self.assertRaises(PlanError):
            plan(new=Package(A, NEW.files), version_relation='same')

    def test_falsey_non_package_values_cannot_bypass_validation(self):
        for invalid in ([], {}, (), '', 0, False):
            for operation, field in (('install', 'new'), ('uninstall', 'old')):
                with self.subTest(operation=operation, invalid=invalid), self.assertRaises(PlanError):
                    build_plan(operation=operation, **{field: invalid})

    def test_invalid_operation_shapes(self):
        for op, old, new in [('install', OLD, NEW), ('install', None, None),
                             ('uninstall', OLD, NEW), ('uninstall', None, None),
                             ('repair', None, NEW), ('recover', OLD, NEW)]:
            with self.subTest(op=op), self.assertRaises(PlanError):
                plan(op, old=old, new=new)

    def test_invalid_hash_size(self):
        for digest, size in [('bad', 0), ('A' * 64, 0), (A, -1), (A, True), (A, 1.5)]:
            with self.subTest(digest=digest, size=size), self.assertRaises(PlanError):
                Image(digest, size)
        self.assertEqual(Image(A, 0).size, 0)

    def test_unsafe_paths(self):
        for path in ('../x', '/x', 'C:/x', 'a\\b', 'a:b', 'a//b', 'a/./b',
                     'a/NUL.txt', 'a/x.', 'a/x ', 'a/\x00', 'a/\ud800'):
            with self.subTest(path=path), self.assertRaises(PlanError):
                index((file(path),))

    def test_duplicate_alias_and_ancestor_paths(self):
        for paths in [('a', 'A'), ('a', 'a'), ('a', 'a/b'), ('é', 'e\u0301')]:
            with self.subTest(paths=paths), self.assertRaises(PlanError):
                index(tuple(file(p) for p in paths))

    def test_cross_snapshot_alias_and_ancestor_refused(self):
        for rows in [(file('PAYLOAD/module.dll'),), (file('payload'),)]:
            with self.subTest(rows=rows), self.assertRaises(PlanError):
                plan(observed=rows)
        with self.assertRaises(PlanError):
            plan(new=Package(B, (file('PAYLOAD/module.dll'),)))

    def test_complete_plan_keeps_unchanged_paths(self):
        p = plan(new=Package(B, OLD.files + (file('payload/new.txt'),)))
        self.assertEqual(len(p.steps), 3)
        self.assertEqual(sum(s.action == Action.UNCHANGED for s in p.steps), 2)

    def test_inputs_not_mutated_and_plan_frozen(self):
        observed = list(OLD.files)
        p = plan(observed=observed)
        self.assertEqual(observed, list(OLD.files))
        with self.assertRaises(AttributeError):
            p.route = 'verified-no-op'
        with self.assertRaises(AttributeError):
            p.steps[0].before = image(b'wrong')


class ModelConnectionTests(unittest.TestCase):
    def prepare(self, *, repair=False, fault=None):
        old_bytes = {'payload/module.dll': b'old', 'payload/obsolete.txt': b'old'}
        new_bytes = {'payload/module.dll': b'new', 'payload/data/en.ini': b'locale'}
        if repair:
            old_bytes.pop('payload/obsolete.txt')
            new_bytes = {'payload/module.dll': b'old', 'payload/obsolete.txt': b'old'}
        observed = tuple(file(k, v) for k, v in old_bytes.items())
        p = plan('repair' if repair else 'upgrade', new=OLD if repair else NEW,
                 observed=observed, version_relation='same' if repair else 'newer')
        # Fixture-only bridge uses known bytes after checking generated hashes.
        steps = []
        for s in p.steps:
            b, a = old_bytes.get(s.path), new_bytes.get(s.path)
            self.assertEqual(s.before, image(b) if b is not None else None)
            self.assertEqual(s.after, image(a) if a is not None else None)
            steps.append(ModelStep(s.path, ModelAction(s.action.value), b, a))
        canaries = {'scenes/main.json': b'untouched', 'payload/other.txt': b'unknown'}
        fs = FakeFS(old_bytes | canaries, b'old-receipt', b'old-registration')
        eng = Engine.prepare(fs, steps, b'new-receipt', b'new-registration',
                             frozenset(s.path for s in steps), fault_index=fault)
        return eng, old_bytes, new_bytes, canaries

    def test_generated_plans_apply_and_all_crash_boundaries(self):
        count = 0
        for repair in (False, True):
            clean, _, new, canaries = self.prepare(repair=repair)
            self.assertEqual(clean.apply().code, 0)
            self.assertEqual(clean.fs.files, new | canaries)
            for boundary in range(len(clean.events)):
                with self.subTest(repair=repair, boundary=boundary):
                    eng, old, new, canaries = self.prepare(repair=repair, fault=boundary)
                    with self.assertRaises(Crash):
                        eng.apply()
                    resumed = Engine(eng.fs, eng.d)
                    result = resumed.recover()
                    self.assertIn(result.code, (0, 30))
                    self.assertIn(eng.fs.files, (old | canaries, new | canaries))
                    once = dict(eng.fs.files)
                    Engine(eng.fs, eng.d).recover()
                    self.assertEqual(eng.fs.files, once)
                    count += 1
        self.assertGreater(count, 0)
        print(f'Generated-plan crash boundaries verified: {count}')

if __name__ == '__main__':
    unittest.main()
