!ifndef SONICLATCH_OBS_PREFLIGHT_INCLUDED
!define SONICLATCH_OBS_PREFLIGHT_INCLUDED
Var ObsCheck
; NSIS/System are 32-bit: PROCESSENTRY32W is 556 bytes, szExeFile offset 36.
; Snapshot names only. Never opens, closes or terminates OBS.
!macro DefineObsCheck PREFIX
Function ${PREFIX}CheckObsRunning
 Push $0
 Push $1
 Push $2
 Push $3
 Push $4
 Push $5
 StrCpy $ObsCheck "unknown"
 System::Call 'kernel32::CreateToolhelp32Snapshot(i 2,i 0)p.r0'
 StrCmp $0 -1 obs_done
 System::Alloc 556
 Pop $1
 StrCmp $1 0 obs_close_snapshot
 System::Call '*$1(i 556)'
 System::Call 'kernel32::Process32FirstW(p r0,p r1)i.r2 ?e'
 Pop $5
 obs_scan:
 StrCmp $2 0 obs_scan_end
 IntOp $3 $1 + 36
 System::Call '*$3(&w260 .r4)'
 StrCmp $4 "obs64.exe" obs_found
 StrCmp $4 "obs32.exe" obs_found
 System::Call 'kernel32::Process32NextW(p r0,p r1)i.r2 ?e'
 Pop $5
 Goto obs_scan
 obs_scan_end:
 StrCmp $5 18 0 obs_free
 StrCpy $ObsCheck "clear"
 Goto obs_free
 obs_found:
 StrCpy $ObsCheck "running"
 obs_free:
 System::Free $1
 obs_close_snapshot:
 System::Call 'kernel32::CloseHandle(p r0)'
 obs_done:
 Pop $5
 Pop $4
 Pop $3
 Pop $2
 Pop $1
 Pop $0
FunctionEnd
!macroend
!macro RequireObsClosed PREFIX
 Call ${PREFIX}CheckObsRunning
 StrCmp $ObsCheck "clear" +5
 SetErrorLevel 40
 DetailPrint "OBS preflight: $ObsCheck; helper not invoked."
 MessageBox MB_OK|MB_ICONEXCLAMATION "OBSを閉じてから、Setupを新しく起動してください。OKを押すとSetupを終了します。起動状態を確認できない場合も安全のため停止します。" /SD IDOK
 Quit
!macroend
!endif
