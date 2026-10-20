import unittest,os,json
from pathlib import Path
import fixture_transaction_windows as tx
from plan_core import File,Package,build_plan

def package(data,identity):return Package(identity,tuple(File(p,tx._image(b)) for p,b in sorted(data.items())))
def setup(area,kind='upgrade'):
 old={'owned':b'old','obsolete':b'old-delete'};new={'owned':b'new','created':b'new-create'}
 if kind=='install':old={}
 if kind=='uninstall':new={}
 if kind=='repair':new=dict(old)
 for path,data in old.items():(area.root/'payload'/path).write_bytes(data)
 if kind=='repair':(area.root/'payload'/'owned').unlink()
 (area.root/'payload'/'neighbor').write_bytes(b'canary')
 oldreceipt=b'old-receipt' if old else None;newreceipt=b'new-receipt' if new else None
 if oldreceipt:(area.root/'metadata'/'receipt.bin').write_bytes(oldreceipt)
 op=kind;oldpkg=package(old,'a'*64) if old else None;newpkg=oldpkg if kind=='repair' else package(new,'b'*64) if new else None
 observed=tx.inv.inspect_root(area.root/'payload').observed
 plan=build_plan(operation=op,old=oldpkg,new=newpkg,observed=observed,trusted_old={'a'*64},trusted_new={'a'*64,'b'*64},predecessors={'a'*64},version_relation='same' if kind=='repair' else 'newer')
 helper=tx.prepare(area,plan,new,old_receipt=oldreceipt,new_receipt=newreceipt,safe=lambda:True);return helper
@unittest.skipUnless(os.name=='nt','Windows only')
class Transactions(unittest.TestCase):
 def test_apply_all_operations_unknown_preserved(self):
  for kind in ['install','upgrade','repair','uninstall']:
   with self.subTest(kind=kind),tx.temporary_fixture() as a:
    h=setup(a,kind);before=(a.root/'payload'/'neighbor').stat().st_mtime_ns;r=h.apply();self.assertEqual(r.code,0);self.assertTrue(h.matches('after'));self.assertEqual((a.root/'payload'/'neighbor').read_bytes(),b'canary');self.assertEqual((a.root/'payload'/'neighbor').stat().st_mtime_ns,before);self.assertFalse(r.publication_authorized);self.assertEqual(tx.Helper(a,h.tx,safe=lambda:True).recover().code,0)
 def test_every_apply_boundary_reopen_recovery(self):
  count=0
  for kind in ['install','upgrade','repair','uninstall']:
   with tx.temporary_fixture() as a:
    reference=setup(a,kind);reference.events.clear();self.assertEqual(reference.apply().code,0);n=len(reference.events)
   for fault in range(n):
    with self.subTest(kind=kind,fault=fault),tx.temporary_fixture() as a:
     base=setup(a,kind);h=tx.Helper(a,base.tx,fault=fault,safe=lambda:True)
     with self.assertRaises(tx.Crash):h.apply()
     reopened=tx.Helper(a,h.tx,safe=lambda:True);r=reopened.recover();self.assertIn(r.code,[0,30]);self.assertTrue(reopened.matches('after' if r.code==0 else 'before'));self.assertEqual((a.root/'payload'/'neighbor').read_bytes(),b'canary');self.assertEqual(reopened.recover().code,r.code);count+=1
  Path('fixture-apply-boundaries.json').write_text(json.dumps({'nativeFileIO':True,'boundaryCases':count,'scope':'same-process Helper reopen with fixture capability; not power loss or process-death trust reconstruction'}))
 def test_external_payload_preserved(self):
  with tx.temporary_fixture() as a:
   b=setup(a);h=tx.Helper(a,b.tx,fault=5,safe=lambda:True)
   with self.assertRaises(tx.Crash):h.apply()
   (a.root/'payload'/'owned').write_bytes(b'user edit');r=tx.Helper(a,h.tx,safe=lambda:True).recover();self.assertEqual(r.code,31);self.assertEqual((a.root/'payload'/'owned').read_bytes(),b'user edit')
 def test_backup_corruption_refuses(self):
  with tx.temporary_fixture() as a:
   h=setup(a);next(h.folder.glob('backup-*.bin')).write_bytes(b'corrupt');r=h.apply();self.assertIn(r.code,[30,31]);self.assertTrue(h.matches('before'))
 def test_stage_corruption_refuses(self):
  with tx.temporary_fixture() as a:
   h=setup(a);(h.folder/'stage-0.bin').write_bytes(b'corrupt');r=h.apply();self.assertIn(r.code,[30,31]);self.assertTrue(h.matches('before'))
 def test_external_receipt_preserved(self):
  with tx.temporary_fixture() as a:
   h=setup(a);(a.root/'metadata'/'receipt.bin').write_bytes(b'user');self.assertEqual(h.apply().code,31);self.assertEqual((a.root/'metadata'/'receipt.bin').read_bytes(),b'user')
 def test_guard_default_unknown_and_lock(self):
  with tx.temporary_fixture() as a:
   h=setup(a);self.assertEqual(tx.Helper(a,h.tx).apply().code,31)
   with h.session():self.assertEqual(tx.Helper(a,h.tx,safe=lambda:True).apply().code,31)
 def test_directory_ads_and_unknown_directory_remain(self):
  with tx.temporary_fixture() as a:
   h=setup(a);d=a.root/'payload'/'unknown-dir';d.mkdir();Path(str(d)+':note').write_bytes(b'directory ADS');self.assertEqual(h.apply().code,0);self.assertTrue(d.is_dir());self.assertEqual(Path(str(d)+':note').read_bytes(),b'directory ADS')
 def test_journal_tampering_refused(self):
  with tx.temporary_fixture() as a:
   h=setup(a);value=json.loads((h.folder/'journal.json').read_text());value['planDigest']='0'*64;(h.folder/'journal.json').write_text(json.dumps(value));self.assertEqual(h.recover().code,31);self.assertTrue(h.matches('before'))
 def test_committed_drift_not_rolled_back(self):
  with tx.temporary_fixture() as a:
   h=setup(a);self.assertEqual(h.apply().code,0);(a.root/'payload'/'owned').write_bytes(b'user edit');self.assertEqual(h.recover().code,31);self.assertEqual((a.root/'payload'/'owned').read_bytes(),b'user edit')
if __name__=='__main__':unittest.main(verbosity=2)
