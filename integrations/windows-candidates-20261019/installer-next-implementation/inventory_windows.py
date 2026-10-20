"""Read-only Win32 inventory adapter. No apply, ownership or trust authority.
Only disposable fixtures are exercised by its tests. A snapshot is not atomic.
"""
from contextlib import contextmanager, ExitStack
from dataclasses import dataclass
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import hashlib, os, tempfile
from plan_core import File, Image
from vendor.validator import path_key, Invalid

class InventoryError(ValueError): pass
@dataclass(frozen=True)
class Snapshot:
    observed: tuple[File, ...]
    missing: tuple[str, ...]
    directories: tuple[str, ...]
    publication_authorized: bool = False

class INFO(C.Structure):
    _fields_=[('attributes',W.DWORD),('creation',W.FILETIME),('access',W.FILETIME),('write',W.FILETIME),('volume',W.DWORD),('sizeHigh',W.DWORD),('sizeLow',W.DWORD),('links',W.DWORD),('indexHigh',W.DWORD),('indexLow',W.DWORD)]
class STREAM(C.Structure):
    _fields_=[('size',C.c_longlong),('name',W.WCHAR*296)]
if os.name=='nt':
    K=C.WinDLL('kernel32',use_last_error=True)
    K.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,W.HANDLE];K.CreateFileW.restype=W.HANDLE
    K.CloseHandle.argtypes=[W.HANDLE];K.CloseHandle.restype=W.BOOL
    K.GetFileInformationByHandle.argtypes=[W.HANDLE,C.POINTER(INFO)];K.GetFileInformationByHandle.restype=W.BOOL
    K.GetFinalPathNameByHandleW.argtypes=[W.HANDLE,W.LPWSTR,W.DWORD,W.DWORD];K.GetFinalPathNameByHandleW.restype=W.DWORD
    K.ReadFile.argtypes=[W.HANDLE,C.c_void_p,W.DWORD,C.POINTER(W.DWORD),C.c_void_p];K.ReadFile.restype=W.BOOL
    K.GetFileType.argtypes=[W.HANDLE];K.GetFileType.restype=W.DWORD
    K.FindFirstStreamW.argtypes=[W.LPCWSTR,C.c_int,C.POINTER(STREAM),W.DWORD];K.FindFirstStreamW.restype=W.HANDLE
    K.FindNextStreamW.argtypes=[W.HANDLE,C.POINTER(STREAM)];K.FindNextStreamW.restype=W.BOOL
    K.FindClose.argtypes=[W.HANDLE];K.FindClose.restype=W.BOOL
INVALID=C.c_void_p(-1).value

def _error(label):
    return InventoryError(label+' (Win32 '+str(C.get_last_error())+')')
def _key(path):
    try:return path_key(path)
    except (Invalid,UnicodeError) as e:raise InventoryError('unsafe or aliased path') from e

def _info(handle):
    i=INFO()
    if not K.GetFileInformationByHandle(handle,C.byref(i)):raise _error('metadata read failed')
    # Access time may change as a consequence of reading; do not treat it as content.
    return (i.attributes,i.volume,(i.indexHigh<<32)|i.indexLow,i.links,(i.sizeHigh<<32)|i.sizeLow,(i.write.dwHighDateTime<<32)|i.write.dwLowDateTime,(i.creation.dwHighDateTime<<32)|i.creation.dwLowDateTime)

def _final(handle):
    b=C.create_unicode_buffer(32768);n=K.GetFinalPathNameByHandleW(handle,b,len(b),0)
    if not n or n>=len(b):raise _error('final path unavailable')
    value=b.value
    if not value.startswith('\\?\\') and not value.startswith('\\\\?\\'):raise InventoryError('nonlocal final path')
    return value[4:]

@contextmanager
def _open(path,directory,*,share=1):
    # OPEN_EXISTING, no follow at the final component. Ancestors are pinned below.
    handle=K.CreateFileW(str(path),0x80000000,share,None,3,0x02200000,None)
    if handle==INVALID:raise _error('read-only open failed')
    try:
        info=_info(handle)
        if K.GetFileType(handle)!=1 or info[0]&0x400 or bool(info[0]&0x10)!=directory:raise InventoryError('reparse point or nonregular entry')
        if os.path.normcase(_final(handle))!=os.path.normcase(str(path)):raise InventoryError('final path alias or redirected root')
        yield handle
    finally:K.CloseHandle(handle)

def _streams(path):
    data=STREAM();h=K.FindFirstStreamW(str(path),0,C.byref(data),0)
    if h==INVALID:
        if C.get_last_error()==38:return
        raise _error('stream inventory unavailable')
    try:
        while True:
            if data.name!='::$DATA':raise InventoryError('alternate stream refused')
            if not K.FindNextStreamW(h,C.byref(data)):
                if C.get_last_error()!=38:raise _error('stream enumeration failed')
                break
    finally:K.FindClose(h)

def _canonical_root(root):
    p=Path(root)
    if not p.is_absolute() or not p.drive or p.drive.startswith('\\') or str(p).startswith('\\') or '..' in p.parts or p!=Path(os.path.normpath(str(p))):raise InventoryError('local absolute canonical root required')
    for part in p.parts[1:]:_key(part)
    return p

@contextmanager
def pinned_root(root,*,share=1):
    """Native helper seam: pin validated existing local root without writing it.
    Caller must authenticate package/root selection separately. No install authority.
    """
    if os.name!='nt':raise InventoryError('Windows required')
    root=_canonical_root(root)
    with ExitStack() as stack:
        current=Path(root.anchor)
        # Pin ancestors without delete sharing before deriving any descendant path.
        stack.enter_context(_open(current,True,share=share))
        for part in root.parts[1:]:
            current=current/part;stack.enter_context(_open(current,True,share=share))
        handle=stack.enter_context(_open(root,True,share=share))
        yield root,handle,stack

def inspect_root(root,*,expected_paths=()):
    """Present rows feed build_plan(observed=snapshot.observed); missing stays absent.
    Holds opened descendants through scan; refuses detectable changes, aliases,
    streams, links and I/O errors. Not an atomic snapshot or transaction executor.
    """
    if isinstance(expected_paths,(str,bytes)):raise InventoryError('path sequence required')
    expected=tuple(expected_paths);names={};kinds={};seen=set()
    def register(path,kind):
        key=_key(path)
        if key in names and names[key]!=path:raise InventoryError('case or Unicode alias')
        if key in kinds and kinds[key]!=kind:raise InventoryError('file/directory collision')
        names[key]=path;kinds[key]=kind;return key
    for path in expected:
        key=_key(path)
        if key in seen:raise InventoryError('duplicate expected path')
        seen.add(key);parts=path.split('/')
        for n in range(1,len(parts)):register('/'.join(parts[:n]),'directory')
        register(path,'file')
    rows=[];directories=[];identities=set();held=[]
    try:
        with pinned_root(root) as (root,anchor,stack):
            volume=_info(anchor)[1]
            def walk(path,handle,prefix):
                before=_info(handle);entries=sorted(os.listdir(path));held.append((handle,before))
                for name in entries:
                    relative=f'{prefix}/{name}' if prefix else name;_key(relative);child=path/name
                    attrs=os.lstat(child).st_file_attributes
                    if attrs&0x400:raise InventoryError('reparse entry refused')
                    directory=bool(attrs&0x10);register(relative,'directory' if directory else 'file')
                    h=stack.enter_context(_open(child,directory));info=_info(h)
                    if info[1]!=volume:raise InventoryError('cross-volume entry')
                    if directory:
                        directories.append(relative);walk(child,h,relative)
                    else:
                        if info[3]!=1 or info[1:3] in identities:raise InventoryError('hardlink or identity alias')
                        identities.add(info[1:3]);_streams(child);digest=hashlib.sha256();size=0;buffer=C.create_string_buffer(65536);count=W.DWORD()
                        while True:
                            if not K.ReadFile(h,buffer,len(buffer),C.byref(count),None):raise _error('file read failed')
                            if not count.value:break
                            digest.update(buffer.raw[:count.value]);size+=count.value
                        if size!=info[4] or _info(h)!=info:raise InventoryError('file changed during scan')
                        held.append((h,info));rows.append(File(relative,Image(digest.hexdigest(),size)))
                if entries!=sorted(os.listdir(path)) or _info(handle)!=before:raise InventoryError('directory changed during scan')
            walk(root,anchor,'')
            if any(_info(h)!=info for h,info in held):raise InventoryError('entry changed before snapshot completion')
    except OSError as e:raise InventoryError('filesystem inventory failed') from e
    present={_key(x.path) for x in rows}
    return Snapshot(tuple(sorted(rows,key=lambda x:x.path)),tuple(sorted(x for x in expected if _key(x) not in present)),tuple(sorted(directories)))

@contextmanager
def temporary_fixture():
    with tempfile.TemporaryDirectory(prefix='soniclatch-win-inventory-') as name:yield Path(name)
