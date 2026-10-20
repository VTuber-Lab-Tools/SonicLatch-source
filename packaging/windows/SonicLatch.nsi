; Fixed-payload product integration. Source build is not distribution approval.
Unicode true
SetCompressor zlib
Name "SonicLatch"
RequestExecutionLevel admin
!include "MUI2.nsh"
!ifndef SONICLATCH_PACKAGE_INPUT
 !error "DRAFT_BLOCKED: fixed helper/package input has not been supplied"
!endif
!include "${SONICLATCH_PACKAGE_INPUT}"
!ifndef SONICLATCH_HELPER_FILE
 !error "DRAFT_BLOCKED: verified helper missing"
!endif
!ifndef SONICLATCH_INSTALLER_OUTPUT
 !error "DRAFT_BLOCKED: output missing"
!endif
OutFile "${SONICLATCH_INSTALLER_OUTPUT}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Japanese"
Var HelperCode
Var HelperReply
Var Management
!macro ExtractHelper
 InitPluginsDir
 SetOutPath "$PLUGINSDIR"
 File /oname=SonicLatch-helper.exe "${SONICLATCH_HELPER_FILE}"
 SetShellVarContext all
 StrCpy $Management "$APPDATA\SonicLatch\Installer"
!macroend
!macro CallHelper ARG
 nsExec::ExecToStack '"$PLUGINSDIR\SonicLatch-helper.exe" ${ARG}'
 Pop $HelperCode
 Pop $HelperReply
!macroend
!macro RequireReply EXPECTED LABEL
 StrCmp $HelperReply "${EXPECTED}$\r$\n" ${LABEL}
 StrCmp $HelperReply "${EXPECTED}$\n" ${LABEL}
!macroend
; The native helper verifies hashes/ownership and releases all handles before
; these fixed bootstrap deletes. No recursive deletion or arbitrary root.
!macro CleanBootstrap FAIL
 IfFileExists "$Management\helper.exe" 0 +4
  ClearErrors
  Delete "$Management\helper.exe"
  IfErrors ${FAIL}
 IfFileExists "$Management\Uninstall.exe" 0 +4
  ClearErrors
  Delete "$Management\Uninstall.exe"
  IfErrors ${FAIL}
 ; Keep the ownership marker until both executable deletions succeed.
 IfFileExists "$Management\active.bin" 0 +4
  ClearErrors
  Delete "$Management\active.bin"
  IfErrors ${FAIL}
 IfFileExists "$Management\transaction.lock" 0 +4
  ClearErrors
  Delete "$Management\transaction.lock"
  IfErrors ${FAIL}
 IfFileExists "$Management\management.bin" 0 +4
  ClearErrors
  Delete "$Management\management.bin"
  IfErrors ${FAIL}
 ; Empty parents only. Unknown files/directories remain untouched.
 RMDir "$Management\transactions"
 RMDir "$Management"
 RMDir "$APPDATA\SonicLatch"
!macroend
Section "SonicLatch"
 !insertmacro ExtractHelper
 !insertmacro CallHelper "--install"
 StrCmp $HelperCode "0" install_validate
 StrCmp $HelperCode "33" handoff_validate
 StrCmp $HelperCode "34" resume_cleanup_validate
 MessageBox MB_YESNO|MB_ICONEXCLAMATION "導入が完了しませんでした。中断記録から復旧しますか？" /SD IDNO IDNO install_failed
 !insertmacro CallHelper "--recover-active"
 StrCmp $HelperCode "0" install_validate
 StrCmp $HelperCode "33" handoff_validate
 StrCmp $HelperCode "32" resume_uninstall_validate
 StrCmp $HelperCode "30" install_restored
 Goto install_failed
 resume_uninstall_validate:
 !insertmacro RequireReply "SONICLATCH/1 PAYLOAD_REMOVED CLEANUP_REQUIRED" resume_cleanup
 Goto install_failed
 install_restored:
 SetErrorLevel 30
 MessageBox MB_OK "中断前の状態へ戻しました。導入する場合は、このセットアップをもう一度実行してください。" /SD IDOK
 Abort
 handoff_validate:
 !insertmacro RequireReply "SONICLATCH/1 AWAITING_NSIS_UNINSTALLER" write_uninstaller
 Goto install_failed
 write_uninstaller:
 ; Only new own bootstrap file. Never overwrite an existing uninstaller here.
 IfFileExists "$Management\Uninstall.exe" finalize_registration
 ; The final filename is promoted only after WriteUninstaller succeeded.
 ; A partial write remains at the separate pending name and is never registered.
 IfFileExists "$Management\Uninstall.pending.exe" install_failed
 ClearErrors
 WriteUninstaller "$Management\Uninstall.pending.exe"
 IfErrors install_failed
 ClearErrors
 Rename "$Management\Uninstall.pending.exe" "$Management\Uninstall.exe"
 IfErrors install_failed
 finalize_registration:
 !insertmacro CallHelper "--finalize-registration"
 StrCmp $HelperCode "0" install_validate install_failed
 install_validate:
 !insertmacro RequireReply "SONICLATCH/1 PAYLOAD_COMMITTED REGISTERED" install_done
 Goto install_failed
 resume_cleanup_validate:
 !insertmacro RequireReply "SONICLATCH/1 CLEANUP_PENDING" resume_cleanup
 Goto install_failed
 resume_cleanup:
 !insertmacro CallHelper "--cleanup-uninstall"
 StrCmp $HelperCode "0" 0 install_failed
 !insertmacro RequireReply "SONICLATCH/1 CLEANUP_READY" finish_previous_cleanup
 Goto install_failed
 finish_previous_cleanup:
 !insertmacro CleanBootstrap install_failed
 SetErrorLevel 30
 MessageBox MB_OK "前回のアンインストールの後片付けが完了しました。導入する場合は、このセットアップをもう一度実行してください。" /SD IDOK
 Abort
 install_failed:
 SetErrorLevel 31
 DetailPrint "$HelperReply"
 MessageBox MB_OK|MB_ICONSTOP "完了しませんでした。ファイルを消さず、同じセットアップで復旧してください。" /SD IDOK
 Abort
 install_done:
 SetErrorLevel 0
SectionEnd
Section "Uninstall"
 ; NSIS runs its normal temporary copy; embedded helper is also temporary.
 !insertmacro ExtractHelper
 !insertmacro CallHelper "--uninstall"
 StrCmp $HelperCode "32" uninstall_validate
 MessageBox MB_YESNO|MB_ICONEXCLAMATION "削除が中断しています。記録から復旧しますか？" /SD IDNO IDNO uninstall_failed
 !insertmacro CallHelper "--recover-active"
 StrCmp $HelperCode "32" uninstall_validate
 StrCmp $HelperCode "30" uninstall_restored
 Goto uninstall_failed
 uninstall_restored:
 SetErrorLevel 30
 MessageBox MB_OK "中断前の状態へ戻しました。削除する場合は、このアンインストーラーをもう一度実行してください。" /SD IDOK
 Abort
 uninstall_validate:
 !insertmacro RequireReply "SONICLATCH/1 PAYLOAD_REMOVED CLEANUP_REQUIRED" native_cleanup
 Goto uninstall_failed
 native_cleanup:
 !insertmacro CallHelper "--cleanup-uninstall"
 StrCmp $HelperCode "0" 0 uninstall_failed
 !insertmacro RequireReply "SONICLATCH/1 CLEANUP_READY" bootstrap_cleanup
 Goto uninstall_failed
 bootstrap_cleanup:
 !insertmacro CleanBootstrap uninstall_failed
 SetErrorLevel 0
 Goto uninstall_done
 uninstall_failed:
 SetErrorLevel 32
 DetailPrint "$HelperReply"
 MessageBox MB_OK|MB_ICONSTOP "削除が完了しませんでした。既存ファイルは保全しています。同じアンインストーラー、または同じセットアップで後片付けを再開してください。" /SD IDOK
 Abort
 uninstall_done:
SectionEnd
