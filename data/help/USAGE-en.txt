# Using SonicLatch

These instructions are being prepared for release. Real-machine checks reported on 20 October 2026 found no problem with the latest setup stopping while OBS was running or with reinstalling. See the accompanying KNOWN-LIMITS-en.txt for the verified scope.

## Installation

1. Download the EXE identified as the Windows setup on the distribution page. The source-code ZIP is not the normal installer. Check the release and supported environment listed on that page.
2. Stop streaming and recording, then close every running instance of OBS.
3. Open the downloaded setup. If Windows requests administrator approval, check that the prompt belongs to the file you opened and approve it yourself. Cancel if you cannot authorize it. If Windows blocks execution, do not bypass protection settings; contact support below.
4. Follow the setup screens. The current setup UI is in Japanese. There is no destination-selection page: files go into obs-studio\plugins\SonicLatch inside the Windows common application-data folder (normally C:\ProgramData\obs-studio\plugins\SonicLatch). The DLL is in bin\64bit\SonicLatch.dll under that directory; languages and help are in data. You do not need to select the OBS program folder or manually copy the DLL into the plugin root.
5. After setup reports completion, start OBS and open “Audio Delay” from the Docks menu. If it is missing, check your OBS version and the requirements below, then contact support instead of manually copying files or repeatedly installing.

If setup does not finish, see “Interruptions and errors” below. Portable configurations and custom plugin locations are outside the verified scope of these instructions.

## Updating

Close every instance of OBS, then run a setup that the distribution page explicitly identifies as supporting an update from your installed version. If support is not stated for an older version, or you installed files manually, contact support rather than overwriting or removing them first. Do not mix files from old and new versions.

## Removal

1. If you no longer want the sync offsets set with SonicLatch, inspect each affected source in OBS before removal and use “Reset offset” or manual adjustment as needed. “Reset offset” sets 0 ms; it does not restore the value from before installation. Do not assume uninstalling the plugin restores sync offsets automatically.
2. Stop streaming and recording, then close every running instance of OBS.
3. Select “SonicLatch” in the Windows apps list and start uninstalling it. Review the administrator prompt and follow the removal screens.
4. After removal completes, start OBS and check the sync offsets and actual audio for the sources you use.

If SonicLatch is absent from the apps list or removal is interrupted, do not delete related folders manually. See the section below.

## Interruptions and errors

The current setup and removal dialogs are in Japanese:

- “OBSを閉じてから、Setupを新しく起動してください” asks you to close OBS and start setup again. Pressing OK exits setup. Stop streaming and recording, close every instance of OBS, and launch the same setup again. Waiting on the stopped screen does not resume installation.
- “中断記録から復旧しますか？” asks whether to recover an interrupted installation. “記録から復旧しますか？” asks about recovery during removal. Keep OBS closed and use the recovery option shown in the dialog.
- “中断前の状態へ戻しました” means the previous state was restored, not that installation or removal completed. Follow the dialog and run the same setup or the same uninstaller again.
- “前回のアンインストールの後片付けが完了しました” means cleanup of the previous uninstall completed, not that a new installation completed. Run the same setup again as instructed only if you want to install.
- If an error stops the operation, retain the files and use the recovery offered by the same setup, or the same uninstaller used for removal. If recovery cannot proceed or the app is missing from the apps list, stop retrying and contact support. Do not delete management records or backups or change folder permissions.
- Report problems to [SonicLatch GitHub Issues](https://github.com/VTuber-Lab-Tools/SonicLatch-source/issues). Include your OBS and SonicLatch versions, the setup filename, steps taken, and the error shown. Review logs and screenshots for personal or secret information, including names and user-folder paths, and share only the relevant parts.

## Measure → inspect the waveforms → adjust

1. Select the audio source you want to inspect and provide an audio signal.
   The measurement filter “Audio Delay Measurement (SonicLatch)” is added automatically to capture the audio before processing. No manual addition is needed. Do not remove, disable, or reorder it while SonicLatch is in use.
2. Check the PRE (before filters) and POST (after filters) waveforms, estimated delay, and status.
3. Once the estimate is valid, use “Auto adjust” (Auto) to set an offset that compensates for the measured delay.
4. Fine-tune with ± or numeric entry as needed. Typing or editing a value does not apply it; use “Apply entered offset” to apply the value.
5. Check the audio in an actual recording or stream. Use “Undo” to revert the previous operation, or “Reset offset” to set the offset to 0 ms.

Delay D is estimated by comparing PRE and POST. The displayed POST waveform is shifted in time by the current sync offset S; this is not a new measurement of the corrected output.

## What each control does

- Auto adjust (Auto): Sets the sync offset to −D for a valid estimated delay D. It does not add −D to the current offset.
- ±: Adjusts the current sync offset.
- Numeric entry: Sets the sync offset to the entered value.
- Undo: Restores the value before the previous successful operation. Changes to the source or external offset can make Undo unavailable.
- Reset offset: Sets the sync offset to 0 ms.
- Residual: Displays D + S, the estimated delay plus the sync offset. This is not a measurement of the corrected output.

For example, if D is +80 ms, Auto sets the offset to −80 ms, regardless of the current offset.

You can also inspect the current sync offset in OBS Advanced Audio Properties. After changing the value or source outside SonicLatch, check the current state and a new valid estimate before adjusting it.

## Ranges and display

Target environment: 64-bit Windows and OBS 30+ (OBS 32.2.2 recommended). Loading and dock display have been confirmed in OBS 32.2.2 on a real system. OBS 30/31 remain untested on a real system. The support target and the verified scope are distinct.

Audio configuration: stereo at 44.1 kHz or 48 kHz.

The measurement range for estimated delay D is approximately within ±500 ms, excluding the search endpoints. The write range for sync offset S is −950 to +1000 ms. The measurement and write ranges are different.

Measurement and display alone do not change the sync offset. If the signal is silent, muted, or the estimate is invalid, check the input and status display.

[Back to README](README-en.md)

## Timing changes and readjustment

Audio may not align perfectly depending on your environment and processing load. Estimated values and audio delay can change during a stream or after adjustment. If the timing drifts, check the input and wait for a valid estimate, then press “Auto adjust” again to readjust using the current estimate. Auto does not continuously track changes or guarantee perfect alignment.

## Help files

The 16 language guides are in data\help\guide-<locale>.txt under the SonicLatch directory described above. The same help folder contains USAGE-ja.txt and USAGE-en.txt with installation and removal instructions. Restart OBS after changing its display language.

## Support and tested scope

See SUPPORT-en.txt and KNOWN-LIMITS-en.txt in the accompanying help folder. Contact the BOOTH shop offering SonicLatch using its shop message feature. Support is available in Japanese and English.
