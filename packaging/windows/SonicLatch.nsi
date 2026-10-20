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
Var SlotTx
Var SlotId
Var RecoveryReapplyPending
Var InstallRetried
!include "nsis-obs-preflight.nsh"
!insertmacro DefineObsCheck ""
!insertmacro DefineObsCheck "un."
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
 StrCmp $HelperCode "31" 0 +2
 DetailPrint "$HelperReply"
 StrCmp $HelperCode "40" 0 +5
 DetailPrint "$HelperReply"
 MessageBox MB_OK|MB_ICONEXCLAMATION "導入前の確認で停止しました。この補助実行では変更処理を開始していません。OBSを閉じてから、Setupを新しく起動してください。OKを押すとSetupを終了します。管理者権限等の理由は詳細表示を確認してください。" /SD IDOK
 SetErrorLevel 40
 Quit
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
 !insertmacro RequireObsClosed ""
 !insertmacro ExtractHelper
 StrCpy $RecoveryReapplyPending 0
 StrCpy $InstallRetried 0
 install_target:
 !insertmacro CallHelper "--install"
 StrCmp $HelperCode "0" install_validate
 StrCmp $HelperCode "33" handoff_validate
 StrCmp $HelperCode "34" resume_cleanup_validate
 StrCmp $InstallRetried 1 install_failed
 MessageBox MB_YESNO|MB_ICONEXCLAMATION "導入が完了しませんでした。中断記録から復旧しますか？" /SD IDNO IDNO install_failed
 StrCpy $RecoveryReapplyPending 1
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
 ; Each attempt gets a new protected slot bound to tx/plan/helper. Unsealed
 ; leftovers and the legacy fixed pending filename are preserved, not adopted.
 !insertmacro CallHelper "--prepare-uninstaller-slot"
 StrCmp $HelperCode "36" resumed_slot_validate
 StrCmp $HelperCode "35" 0 install_failed
 StrCpy $SlotTx $HelperReply 32 30
 StrCpy $SlotId $HelperReply 32 63
 !insertmacro RequireReply "SONICLATCH/1 UNINSTALLER_SLOT $SlotTx $SlotId" verify_slot
 Goto install_failed
 resumed_slot_validate:
 !insertmacro RequireReply "SONICLATCH/1 UNINSTALLER_PROMOTED" finalize_registration
 Goto install_failed
 verify_slot:
 ; The helper validates bounded hex IDs and exact active ownership before any
 ; path derived from the reply is used for a write.
 !insertmacro CallHelper "--verify-uninstaller-slot $SlotTx $SlotId"
 StrCmp $HelperCode "0" 0 install_failed
 !insertmacro RequireReply "SONICLATCH/1 UNINSTALLER_SLOT_VERIFIED" write_slot
 Goto install_failed
 write_slot:
 ClearErrors
 WriteUninstaller "$Management\transactions\$SlotTx\uninstaller-slots\$SlotId\Uninstall.pending.exe"
 IfErrors install_failed
 ; Seal is invoked only after this WriteUninstaller reported success.
 !insertmacro CallHelper "--seal-uninstaller-slot $SlotTx $SlotId"
 StrCmp $HelperCode "0" 0 install_failed
 !insertmacro RequireReply "SONICLATCH/1 UNINSTALLER_SEALED" promote_slot
 Goto install_failed
 promote_slot:
 !insertmacro CallHelper "--promote-uninstaller-slot $SlotTx $SlotId"
 StrCmp $HelperCode "0" 0 install_failed
 !insertmacro RequireReply "SONICLATCH/1 UNINSTALLER_PROMOTED" finalize_registration
 Goto install_failed
 finalize_registration:
 !insertmacro CallHelper "--finalize-registration"
 StrCmp $HelperCode "0" install_validate install_failed
 install_validate:
 !insertmacro RequireReply "SONICLATCH/1 PAYLOAD_COMMITTED REGISTERED" install_registered
 Goto install_failed
 install_registered:
 ; Recovery may have registered an older plan. Require one current-catalog
 ; install after it, including any old uninstaller handoff. Never recover twice.
 StrCmp $RecoveryReapplyPending 1 reapply_current_target install_done
 reapply_current_target:
 StrCpy $RecoveryReapplyPending 0
 StrCpy $InstallRetried 1
 Goto install_target
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
 !insertmacro RequireObsClosed "un."
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
