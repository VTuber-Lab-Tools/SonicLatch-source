import hashlib, os, subprocess, unittest
from pathlib import Path
from unittest.mock import patch
import inventory_windows as inv
from plan_core import File,Image,Package,Action,PlanError,build_plan

def image(b):return Image(hashlib.sha256(b).hexdigest(),len(b))
@unittest.skipUnless(os.name=='nt','native Windows only')
class WindowsInventory(unittest.TestCase):
 def setUp(self):
  self.context=inv.temporary_fixture();self.root=self.context.__enter__();self.addCleanup(self.context.__exit__,None,None,None)
 def put(self,name,b=b'fixture'):
  p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b);return p
 def test_present_empty_missing(self):
  self.put('data/en.ini',b'abc');self.put('empty',b'');s=inv.inspect_root(self.root,expected_paths=('data/en.ini','missing'))
  self.assertEqual(s.observed,(File('data/en.ini',image(b'abc')),File('empty',image(b''))));self.assertEqual(s.missing,('missing',));self.assertEqual(s.directories,('data',));self.assertFalse(s.publication_authorized)
 def test_large_and_unicode(self):
  b=bytes(range(256))*700;self.put('日本語/音声.bin',b);self.assertEqual(inv.inspect_root(self.root).observed,(File('日本語/音声.bin',image(b)),))
 def test_empty_all_missing(self):self.assertEqual(inv.inspect_root(self.root,expected_paths=('data/missing',)).missing,('data/missing',))
 def test_unknown_and_repair_plan(self):
  self.put('owned',b'old');self.put('neighbor',b'canary');before={p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in self.root.iterdir()};s=inv.inspect_root(self.root,expected_paths=('owned','missing'));pkg=Package('a'*64,(File('owned',image(b'old')),File('missing',image(b'restore'))));plan=build_plan(operation='repair',old=pkg,new=pkg,observed=s.observed,trusted_old={pkg.identity},trusted_new={pkg.identity},version_relation='same');self.assertEqual(plan.route,'missing-file-repair');self.assertEqual(plan.retained_unknown,('neighbor',));self.assertIsNone(plan.steps[0].before);self.assertEqual(plan.steps[0].action,Action.CREATE);self.assertFalse(plan.publication_authorized);self.assertEqual(before,{p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in self.root.iterdir()})
 def test_uninstall_missing(self):
  pkg=Package('a'*64,(File('missing',image(b'x')),));s=inv.inspect_root(self.root,expected_paths=('missing',));plan=build_plan(operation='uninstall',old=pkg,observed=s.observed,trusted_old={pkg.identity});self.assertIsNone(plan.steps[0].before);self.assertIsNone(plan.steps[0].after)
 def test_unowned_identical_rejected_by_plan(self):
  self.put('new',b'x');pkg=Package('a'*64,(File('new',image(b'x')),));s=inv.inspect_root(self.root)
  with self.assertRaises(PlanError):build_plan(operation='install',new=pkg,observed=s.observed,trusted_new={pkg.identity})
 def test_invalid_paths(self):
  for path in ['../escape','a/../b','C:/a','/abs','a\\b','a:stream','a.','a ','CON','COM¹.txt','LPT²','a//b','', 'x\x00y']:
   with self.subTest(path=path),self.assertRaises(inv.InventoryError):inv.inspect_root(self.root,expected_paths=(path,))
 def test_duplicate_alias_and_collision(self):
  for expected in [('a','A'),('é','e\u0301'),('a','a/b'),('a','a')]:
   with self.subTest(expected=expected),self.assertRaises(inv.InventoryError):inv.inspect_root(self.root,expected_paths=expected)
 def test_expected_alias_of_actual(self):
  self.put('Owned')
  with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root,expected_paths=('owned',))
 def test_bad_expected_container_and_root(self):
  with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root,expected_paths='owned')
  for root in [Path('relative'),str(self.root/'x'/'..'),r'\\server\share',r'\\?\C:\temp']:
   with self.subTest(root=root),self.assertRaises(inv.InventoryError):inv.inspect_root(root)
 def test_hardlink_refused(self):
  p=self.put('owned');os.link(p,self.root/'alias')
  with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root)
 def test_named_stream_refused(self):
  p=self.put('owned');Path(str(p)+':private').write_bytes(b'canary')
  with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root)
  self.assertEqual(Path(str(p)+':private').read_bytes(),b'canary')
 def test_junction_and_junction_root_refused(self):
  target=self.root/'target';target.mkdir();(target/'canary').write_bytes(b'outside');link=self.root/'junction';r=subprocess.run(['cmd','/c','mklink','/J',str(link),str(target)],capture_output=True)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  try:
   with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root)
   with self.assertRaises(inv.InventoryError):inv.inspect_root(link)
   self.assertEqual((target/'canary').read_bytes(),b'outside')
  finally:os.rmdir(link)
 def test_root_pinned_against_rename(self):
  with inv.pinned_root(self.root):
   with self.assertRaises(PermissionError):os.rename(self.root,Path(str(self.root)+'-moved'))
 def test_io_refuses_no_partial_result(self):
  self.put('owned')
  with patch.object(inv,'_streams',side_effect=inv.InventoryError('fixture injected read error')):
   with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root)
  self.assertEqual(inv.inspect_root(self.root).observed[0].image,image(b'fixture'))
 def test_metadata_change_refused(self):
  self.put('owned');original=inv._info;counts={}
  def changing(h):
   value=original(h);counts[h]=counts.get(h,0)+1
   if not value[0]&0x10 and counts[h]>2:return (*value[:-1],value[-1]+1)
   return value
  with patch.object(inv,'_info',side_effect=changing):
   with self.assertRaises(inv.InventoryError):inv.inspect_root(self.root)
 def test_repeat_read_stable(self):
  self.put('nested/owned');a=inv.inspect_root(self.root);b=inv.inspect_root(self.root);self.assertEqual(a,b)
if __name__=='__main__':unittest.main(verbosity=2)
