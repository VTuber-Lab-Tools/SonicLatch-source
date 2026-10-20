"""Persistent, finally-based isolated QA restoration. No force-kill guarantee."""
from pathlib import Path
import hashlib,json
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(path,value):
 path=Path(path);temp=path.with_suffix(path.suffix+'.tmp');temp.write_text(json.dumps(value,indent=2),encoding='utf-8');temp.replace(path)
class Guard:
 def __init__(self,root,backup,live,config_relative='config/obs-studio',extra=()):
  self.root=Path(root).resolve();self.backup=Path(backup);self.live=live;self.config=self.root/config_relative;self.extra=list(extra);self.manifest=[]
 def __enter__(self):
  if self.live():raise RuntimeError('OBS already present; no changes')
  self.backup.mkdir(parents=True,exist_ok=False)
  paths=[p for p in self.config.rglob('*') if p.is_file() and p.relative_to(self.config).parts[0] not in {'logs','profiler_data'}]+[self.root/p for p in self.extra]
  for i,p in enumerate(paths):
   if not p.resolve().is_relative_to(self.root):raise RuntimeError('snapshot target outside isolated root')
   data=p.read_bytes();name=str(i)+'.bin';(self.backup/name).write_bytes(data);self.manifest.append({'path':str(p.relative_to(self.root)),'backup':name,'sha256':hashlib.sha256(data).hexdigest()})
  save(self.backup/'manifest.json',self.manifest);return self
 def __exit__(self,kind,error,traceback):
  proof={'restored':False,'files':len(self.manifest),'originalException':kind.__name__ if kind else None,'status':'pending','forceKillGuaranteed':False};restore_error=None
  try:
   if self.live():raise RuntimeError('OBS remains; restore pending; no file restoration while host lives')
   known={item['path'] for item in self.manifest}
   for p in list(self.config.rglob('*')):
    if p.is_file() and p.relative_to(self.config).parts[0] not in {'logs','profiler_data'} and str(p.relative_to(self.root)) not in known:
     if not p.resolve().is_relative_to(self.root):raise RuntimeError('cleanup target outside isolated root')
     p.unlink()
   for item in self.manifest:
    p=self.root/item['path'];data=(self.backup/item['backup']).read_bytes()
    if hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError('backup integrity mismatch')
    if not p.resolve().is_relative_to(self.root):raise RuntimeError('restore target outside isolated root')
    p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
   after=[dict(item,afterSHA256=digest(self.root/item['path'])) for item in self.manifest]
   if any(item['afterSHA256']!=item['sha256'] for item in after):raise RuntimeError('restore readback mismatch')
   proof.update(restored=True,status='complete',after=after)
  except BaseException as e:restore_error=e;proof.update(status='restore_pending',restoreError={'type':type(e).__name__,'detail':str(e)})
  finally:save(self.backup/'restore-proof.json',proof)
  if restore_error:raise restore_error
  return False
