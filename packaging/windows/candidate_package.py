"""Bounded nonrelease package preparation. Never installs or authorizes publication.
Reuses the reviewed inventory validator. Input bytes must match caller-pinned hashes.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib,io,json,os,re,stat,sys,uuid,zipfile
CORE=Path(__file__).resolve().parents[2]/'integrations/windows-candidates-20261019/installer-next-implementation'
if str(CORE) not in sys.path:sys.path.insert(0,str(CORE))
from vendor.validator import Inventory,Limits,Invalid,path_key,strict_json
LIMITS=Limits(max_archive_bytes=64*1024*1024,max_entries=512,max_file_bytes=32*1024*1024,max_total_bytes=64*1024*1024)
HASH=re.compile(r'[0-9a-f]{64}\Z')
FIELDS={'schemaVersion','candidateKind','fixtureOnly','publicationAuthorized','moduleId','productVersion','layoutVersion','sourceContext','productManifestDraft','files'}
class PackageError(ValueError):pass
@dataclass(frozen=True)
class Bundle:
 identity:str
 manifest:bytes
 payload:tuple
 def data(self):return {path:data for path,data in self.payload}
def sha(data):return hashlib.sha256(data).hexdigest()
def canonical(value):return (json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'))+'\n').encode('utf-8')
def role(path):
 path_key(path)
 if path=='SonicLatch/SonicLatch.dll':return 'module'
 if not path.startswith('SonicLatch/data/'):raise PackageError('outside declared module/data namespace')
 if path.startswith('SonicLatch/data/locale/') and re.fullmatch(r'[a-z]{2}-[A-Z]{2}\.ini',path.rsplit('/',1)[1]):return 'locale'
 if Path(path).suffix.lower() not in {'.ini','.json','.txt','.png','.svg'}:raise PackageError('unsupported data extension')
 return 'data'
def blocked_draft(draft):
 if not isinstance(draft,dict) or type(draft.get('schemaVersion')) is not int or draft['schemaVersion']!=1 or draft.get('draftOnly') is not True or draft.get('releaseAllowed') is not False or draft.get('moduleId')!='SonicLatch' or draft.get('channel')!='internal-draft':raise PackageError('blocked product draft required')
 if draft.get('productVersion')!='0.1.0' or draft.get('layoutVersion')!='obs33-flat-v1':raise PackageError('unsupported product identity/layout')
def source_context(context):
 if not isinstance(context,dict) or set(context)!={'baseCommit','sourceSnapshotSha256','buildId','syntheticPayload'}:raise PackageError('explicit source context required')
 if not isinstance(context['baseCommit'],str) or not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}',context['baseCommit']):raise PackageError('invalid base commit')
 if not isinstance(context['sourceSnapshotSha256'],str) or not HASH.fullmatch(context['sourceSnapshotSha256']) or not isinstance(context['buildId'],str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,99}',context['buildId']) or type(context['syntheticPayload']) is not bool:raise PackageError('invalid source context')
def validate_document(doc):
 if not isinstance(doc,dict) or set(doc)!=FIELDS or type(doc['schemaVersion']) is not int or doc['schemaVersion']!=1 or doc['candidateKind']!='nonrelease-fixture-bundle' or doc['fixtureOnly'] is not True or doc['publicationAuthorized'] is not False:raise PackageError('candidate schema/gate rejected')
 blocked_draft(doc['productManifestDraft']);source_context(doc['sourceContext'])
 if (doc['moduleId'],doc['productVersion'],doc['layoutVersion'])!=('SonicLatch','0.1.0','obs33-flat-v1'):raise PackageError('candidate identity mismatch')
 rows=doc['files'];keys={};paths=set()
 if not isinstance(rows,list) or not 1<=len(rows)<LIMITS.max_entries:raise PackageError('invalid payload count')
 for row in rows:
  if not isinstance(row,dict) or set(row)!={'relativePath','sha256','size','role','componentLicenseId'}:raise PackageError('invalid file row')
  path=row['relativePath'];key=path_key(path)
  if key in keys or role(path)!=row['role'] or row['componentLicenseId']!='UNRESOLVED':raise PackageError('aliased path or invalid role/license claim')
  if type(row['size']) is not int or not 0<=row['size']<=LIMITS.max_file_bytes or not isinstance(row['sha256'],str) or not HASH.fullmatch(row['sha256']):raise PackageError('invalid size/hash')
  keys[key]=path;paths.add(path)
 for key in keys:
  if any('/'.join(key.split('/')[:i]) in keys for i in range(1,len(key.split('/')))):raise PackageError('ancestor file collision')
 if 'SonicLatch/SonicLatch.dll' not in paths or doc['productManifestDraft'].get('files')!=rows:raise PackageError('module missing or nested manifest disagrees')
 if sum(row['size'] for row in rows)>LIMITS.max_total_bytes:raise PackageError('total payload limit')
 return rows
def verify_stream(stream,expected_identity=None):
 inv=None
 try:
  inv=Inventory(stream,LIMITS);inv.hash_all()
  raw=inv.read_small('candidate-manifest.json',LIMITS.max_json_bytes);doc=strict_json(raw);rows=validate_document(doc)
  if raw!=canonical(doc):raise PackageError('noncanonical candidate manifest')
  identity=sha(raw)
  if expected_identity is not None and identity!=expected_identity:raise PackageError('caller-pinned manifest mismatch')
  if inv.directories or set(inv.entries)!={'candidate-manifest.json'}|{'payload/'+path_key(row['relativePath']) for row in rows}:raise PackageError('missing or unlisted archive entry')
  payload=[]
  for row in rows:
   key='payload/'+path_key(row['relativePath']);info=inv.entries[key]
   if info.orig_filename!='payload/'+row['relativePath'] or info.file_size!=row['size'] or inv.hashes[key]!=row['sha256']:raise PackageError('payload spelling/size/hash mismatch')
   content=inv.read_small(key,LIMITS.max_file_bytes)
   if len(content)!=row['size'] or sha(content)!=row['sha256']:raise PackageError('returned payload bytes changed after inventory verification')
   payload.append((row['relativePath'],content))
  return Bundle(identity,raw,tuple(payload))
 except (Invalid,KeyError,TypeError,ValueError,zipfile.BadZipFile,EOFError) as exc:
  if isinstance(exc,PackageError):raise
  raise PackageError(str(exc)) from exc
 finally:
  if inv is not None:inv.close()
def verify(path,expected_identity=None):
 with Path(path).open('rb') as f:return verify_stream(f,expected_identity)
def read_pinned(path,expected):
 if not isinstance(expected,str) or not HASH.fullmatch(expected):raise PackageError('explicit pinned hash required')
 path=Path(path)
 if path.is_symlink():raise PackageError('source symlink refused')
 with path.open('rb') as f:
  before=os.fstat(f.fileno())
  if not stat.S_ISREG(before.st_mode) or before.st_nlink!=1 or getattr(before,'st_file_attributes',0)&0x400 or before.st_size>LIMITS.max_file_bytes:raise PackageError('source must be bounded regular single-link file')
  data=f.read(LIMITS.max_file_bytes+1);after=os.fstat(f.fileno())
 if (before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns) or len(data)>LIMITS.max_file_bytes or sha(data)!=expected:raise PackageError('source changed or pinned hash mismatch')
 return data
def build(*,draft,version,layout,context,inputs,output):
 blocked_draft(draft);source_context(context)
 if version.strip()!='0.1.0' or layout.get('releaseAllowed') is not False or layout.get('moduleFilename')!='SonicLatch.dll' or layout.get('dllRelativePath')!='SonicLatch/SonicLatch.dll' or layout.get('dataRelativePath')!='SonicLatch/data' or layout.get('layoutVersion')!='obs33-flat-v1':raise PackageError('version/layout disagreement')
 if not isinstance(inputs,dict) or not 1<=len(inputs)<LIMITS.max_entries:raise PackageError('invalid explicit inputs')
 for path,spec in inputs.items():
  if not isinstance(path,str) or not isinstance(spec,(tuple,list)) or len(spec)!=2:raise PackageError('invalid input mapping')
 data={};rows=[];total=0
 for path,(source,expected) in sorted(inputs.items()):
  file_role=role(path);content=read_pinned(source,expected);total+=len(content)
  if total>LIMITS.max_total_bytes:raise PackageError('total payload limit during input read')
  data[path]=content;rows.append({'relativePath':path,'sha256':sha(content),'size':len(content),'role':file_role,'componentLicenseId':'UNRESOLVED'})
 copied=json.loads(json.dumps(draft));copied['files']=rows
 doc={'schemaVersion':1,'candidateKind':'nonrelease-fixture-bundle','fixtureOnly':True,'publicationAuthorized':False,'moduleId':'SonicLatch','productVersion':'0.1.0','layoutVersion':'obs33-flat-v1','sourceContext':context,'productManifestDraft':copied,'files':rows};validate_document(doc)
 archive=io.BytesIO()
 with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_STORED,allowZip64=False) as z:
  for path,content in [('candidate-manifest.json',canonical(doc))]+[('payload/'+name,data[name]) for name in sorted(data)]:
   info=zipfile.ZipInfo(path,(1980,1,1,0,0,0));info.create_system=3;info.external_attr=(stat.S_IFREG|0o644)<<16;z.writestr(info,content)
 raw=archive.getvalue();verified=verify_stream(io.BytesIO(raw));output=Path(output)
 # Prepare, validate, then publish atomically without replacing an existing file.
 # Windows rename is create-only; other platforms use exclusive hard-link publish.
 staged=output.parent/('.candidate-'+uuid.uuid4().hex+'.tmp')
 try:
  with staged.open('xb') as f:f.write(raw);f.flush();os.fsync(f.fileno())
  if os.name=='nt':os.rename(staged,output)
  else:os.link(staged,output)
 finally:
  if staged.exists():staged.unlink()
 return verified
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description='Read-only verification; no installer executor');p.add_argument('archive');p.add_argument('--expected-manifest-sha256');a=p.parse_args()
 try:
  b=verify(a.archive,a.expected_manifest_sha256);print(json.dumps({'valid':True,'manifestSha256':b.identity,'files':len(b.payload),'fixtureOnly':True,'publicationAuthorized':False,'authenticityVerified':False}))
 except (PackageError,OSError) as e:print(json.dumps({'valid':False,'reason':str(e),'publicationAuthorized':False}));sys.exit(31)
