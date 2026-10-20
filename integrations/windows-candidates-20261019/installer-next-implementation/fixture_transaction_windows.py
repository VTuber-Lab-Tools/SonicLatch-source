"""Disposable Windows fixture transaction helper. NO production CLI or executor.
Real file writes/flushes/replacements; trust capability remains fixture-process RAM.
"""
from contextlib import contextmanager,ExitStack
from dataclasses import dataclass
from pathlib import Path
import ctypes as C,hashlib,json,os,tempfile,uuid
import inventory_windows as inv
from plan_core import Plan,Action,Image,index

class Unsafe(ValueError):pass
class Crash(BaseException):pass
@dataclass(frozen=True)
class Result:
 code:int
 state:str
 payload:str
 management:str
 next_action:str
 publication_authorized:bool=False
_TOKEN=object()
class FixtureArea:
 def __init__(self,root,token):
  if token is not _TOKEN:raise Unsafe('fixture factory required')
  self.root=root;self.active=True;self.trusted={}
@contextmanager
def temporary_fixture():
 with tempfile.TemporaryDirectory(prefix='soniclatch-transaction-fixture-') as name:
  a=FixtureArea(Path(name),_TOKEN)
  for n in ['payload','metadata']:(a.root/n).mkdir()
  try:yield a
  finally:a.active=False

def _image(data):return None if data is None else Image(hashlib.sha256(data).hexdigest(),len(data))
def _read(path):
 try:return path.read_bytes()
 except FileNotFoundError:return None

def _atomic(path,data):
 # Same-volume fixed sibling; exclusive create, file flush, write-through promotion.
 tmp=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
 try:
  with tmp.open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())
  move=inv.K.MoveFileExW;move.argtypes=[inv.W.LPCWSTR,inv.W.LPCWSTR,inv.W.DWORD];move.restype=inv.W.BOOL
  if not move(str(tmp),str(path),9):raise inv._error('write-through promote failed')
 finally:
  if tmp.exists():tmp.unlink()

class Helper:
 def __init__(self,area,transaction_id,*,fault=None,safe=lambda:False):
  if type(area) is not FixtureArea or not area.active or transaction_id not in area.trusted:raise Unsafe('active known fixture transaction required')
  self.area=area;self.tx=transaction_id;self.t=area.trusted[transaction_id];self.folder=area.root/'metadata'/transaction_id;self.payload=area.root/'payload';self.fault=fault;self.safe=safe;self.events=[]
 def event(self,name):
  self.events.append(name)
  if self.fault==len(self.events)-1:raise Crash(name)
 def boundary(self,name,fn):
  self.event('before:'+name);fn();self.event('after:'+name)
 def guard(self):
  if not self.area.active or self.safe() is not True:raise Unsafe('fixture process preflight unsafe/unknown')
  with inv.pinned_root(self.payload) as (_,h,_):
   if inv._info(h)[1:3]!=self.t['root_identity']:raise Unsafe('payload root replaced')
  inv.inspect_root(self.payload)
 def current(self,path):
  inv._key(path);return _read(self.payload/path)
 def matches(self,side):return all(_image(self.current(s.path))==getattr(s,side) for s in self.t['plan'].steps)
 def journal(self,state,direction=None,step=None,phase=None):
  if direction:self.direction=direction
  value={'schema':1,'transactionId':self.tx,'planDigest':self.t['digest'],'state':state,'direction':self.direction,'step':step,'phase':phase}
  self.boundary('journal:'+state,lambda:_atomic(self.folder/'journal.json',json.dumps(value,sort_keys=True).encode()))
 def load(self):
  value=json.loads((self.folder/'journal.json').read_text('utf-8'))
  if set(value)!={'schema','transactionId','planDigest','state','direction','step','phase'} or value['schema']!=1 or value['transactionId']!=self.tx or value['planDigest']!=self.t['digest'] or value['state'] not in ['Staged','BackedUp','Applying','Verified','Committed','RollingBack','RolledBack'] or value['direction'] not in ['apply','rollback']:raise Unsafe('unknown journal')
  if value['step'] is not None and value['step'] not in {s.path for s in self.t['plan'].steps}:raise Unsafe('unknown journal step')
  if value['phase'] not in [None,'intent','done']:raise Unsafe('unknown journal phase')
  self.direction=value['direction'];return value
 @contextmanager
 def session(self):
  self.guard();lock=inv.K.CreateFileW(str(self.area.root/'metadata'/'lock'),0xC0000000,0,None,4,0x00200000,None)
  if lock==inv.INVALID:raise Unsafe('fixture busy')
  try:
   with ExitStack() as stack:
    stack.enter_context(inv.pinned_root(self.payload,share=3));stack.enter_context(inv.pinned_root(self.folder,share=3))
    # Parents must exist before prepare; no path creation/deletion API is exposed.
    for s in self.t['plan'].steps:stack.enter_context(inv.pinned_root((self.payload/s.path).parent,share=3))
    yield
  finally:inv.K.CloseHandle(lock)
 def set_payload(self,path,data):
  self.guard();p=self.payload/path
  if data is None:
   if p.exists():p.unlink()
  else:_atomic(p,data)
 def blob(self,i,kind,expected):
  b=_read(self.folder/f'{kind}-{i}.bin')
  if _image(b)!=expected:raise Unsafe('staged/backup bytes changed')
  return b
 def receipt(self):return _read(self.area.root/'metadata'/'receipt.bin')
 def set_receipt(self,data):
  self.guard();p=self.area.root/'metadata'/'receipt.bin'
  if data is None:
   if p.exists():p.unlink()
  else:_atomic(p,data)
 def _recover(self):
  value=self.load();self.guard()
  if value['state']=='RolledBack':
   if not self.matches('before') or self.receipt()!=self.t['old_receipt']:raise Unsafe('terminal rollback drift')
   return Result(30,'RolledBack','restored','fixture-only','prepare fresh transaction')
  committed=value['state']=='Committed' or (self.direction!='rollback' and self.receipt()==self.t['new_receipt'] and self.matches('after'))
  if committed:
   if not self.matches('after') or self.receipt()!=self.t['new_receipt']:raise Unsafe('committed state drift; do not revert')
   self.journal('Committed');return Result(0,'Committed','removed' if self.t['new_receipt'] is None else 'committed','fixture-only','none')
  self.journal('RollingBack','rollback')
  for i,s in reversed(list(enumerate(self.t['plan'].steps))):
   current=_image(self.current(s.path))
   if current==s.before:continue
   if s.action==Action.UNCHANGED or current!=s.after:raise Unsafe('external modification retained')
   b=self.blob(i,'backup',s.before) if s.before is not None else None
   self.journal('RollingBack',step=s.path,phase='intent')
   self.boundary('undo:'+s.path,lambda s=s,b=b:self.set_payload(s.path,b))
   self.journal('RollingBack',step=s.path,phase='done')
  if self.receipt() not in (self.t['old_receipt'],self.t['new_receipt']):raise Unsafe('external receipt retained')
  self.boundary('receipt-restore',lambda:self.set_receipt(self.t['old_receipt']))
  if not self.matches('before') or self.receipt()!=self.t['old_receipt']:raise Unsafe('restore verify failed')
  self.journal('RolledBack');return Result(30,'RolledBack','restored','fixture-only','prepare fresh transaction')
 def apply(self):
  try:
   with self.session():
    if self.load()['state']!='Staged' or self.direction!='apply':raise Unsafe('transaction not fresh')
    if not self.matches('before') or self.receipt()!=self.t['old_receipt']:raise Unsafe('baseline changed')
    for i,s in enumerate(self.t['plan'].steps):
     if s.action!=Action.UNCHANGED:
      if s.before is not None:self.blob(i,'backup',s.before)
      if s.after is not None:self.blob(i,'stage',s.after)
    self.journal('BackedUp')
    for i,s in enumerate(self.t['plan'].steps):
     if s.action==Action.UNCHANGED:continue
     self.journal('Applying',step=s.path,phase='intent');self.guard()
     if _image(self.current(s.path))!=s.before:raise Unsafe('external change before write')
     data=self.blob(i,'stage',s.after) if s.after is not None else None
     self.boundary('apply:'+s.path,lambda s=s,data=data:self.set_payload(s.path,data))
     if _image(self.current(s.path))!=s.after:raise Unsafe('write verify failed')
     self.journal('Applying',step=s.path,phase='done')
    if not self.matches('after'):raise Unsafe('payload verify failed')
    self.journal('Verified')
    self.boundary('receipt-publish',lambda:self.set_receipt(self.t['new_receipt']))
    self.journal('Committed');return Result(0,'Committed','removed' if self.t['new_receipt'] is None else 'committed','fixture-only','none')
  except (Unsafe,inv.InventoryError,OSError,ValueError) as e:
   self.last_error=repr(e);return self.recover()
 def recover(self):
  try:
   with self.session():return self._recover()
  except (Unsafe,inv.InventoryError,OSError,ValueError) as e:
   self.recovery_error=repr(e);return Result(31,'RecoveryRequired','unresolved','fixture-only','preserve journal/backups; inspect')

def prepare(area,plan,payload,*,old_receipt=None,new_receipt=None,safe=lambda:False):
 if type(area) is not FixtureArea or not area.active or type(plan) is not Plan or plan.publication_authorized is not False or safe() is not True:raise Unsafe('active fixture and pure reviewed plan required')
 index(tuple(__import__('plan_core').File(s.path,s.before or s.after or _image(b'')) for s in plan.steps))
 snapshot=inv.inspect_root(area.root/'payload');observed={x.path:x.image for x in snapshot.observed}
 for s in plan.steps:
  valid={Action.CREATE:s.before is None and s.after is not None,Action.REPLACE:s.before is not None and s.after is not None and s.before!=s.after,Action.REMOVE:s.before is not None and s.after is None,Action.UNCHANGED:s.before==s.after}
  if not valid.get(s.action) or observed.get(s.path)!=s.before:raise Unsafe('invalid or modified plan baseline')
  if not (area.root/'payload'/s.path).parent.is_dir():raise Unsafe('fixture destination parent must exist')
  if s.after is not None and s.action!=Action.UNCHANGED and _image(payload.get(s.path))!=s.after:raise Unsafe('payload mismatch')
 if _read(area.root/'metadata'/'receipt.bin')!=old_receipt:raise Unsafe('receipt baseline mismatch')
 tx=uuid.uuid4().hex;folder=area.root/'metadata'/tx;folder.mkdir()
 with inv.pinned_root(area.root/'payload') as (_,h,_):identity=inv._info(h)[1:3]
 digest=hashlib.sha256(repr(plan).encode()).hexdigest();area.trusted[tx]={'plan':plan,'digest':digest,'root_identity':identity,'old_receipt':old_receipt,'new_receipt':new_receipt}
 for i,s in enumerate(plan.steps):
  if s.action!=Action.UNCHANGED:
   if s.before is not None:_atomic(folder/f'backup-{i}.bin',(area.root/'payload'/s.path).read_bytes())
   if s.after is not None:_atomic(folder/f'stage-{i}.bin',payload[s.path])
 helper=Helper(area,tx,safe=safe);helper.direction='apply';helper.journal('Staged');return helper
