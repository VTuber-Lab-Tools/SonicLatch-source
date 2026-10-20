# SonicLatch contact and bug report (pre-publication draft)

Support languages are Japanese and English.

Contact: Use the shop message feature on the BOOTH shop offering SonicLatch. Support is available in Japanese and English.

Prepare your report locally, then send only the necessary information to the shop. You do not need to post logs or personal information in public comments or public issue trackers. This document does not automatically send any information.

## Bug report template

Fill in what you know. “Unknown” is fine for any unavailable detail.

- Subject: a short description of the problem
- SonicLatch version / build identifier: copy the label from the supplied package, or enter “Unknown”
- OS and version:
- OBS version:
- Interface language:
- Relevant audio source type / device model (only if needed; no personal names or device serial numbers):
- Action and steps to reproduce:
  1.
  2.
  3.
- Expected behavior:
- Actual behavior:
- Status or error text shown on screen (with confidential information removed):
- Frequency: every time / sometimes / once / unknown. If known, include a count such as “2 out of 3 attempts”
- Time and time zone (optional, only to match the event to a log):
- Recent related setting or source changes (optional; relevant details only):
- Attachments (optional): reviewed and redacted log excerpt / cropped screenshot / none

## Before attaching logs or screenshots

Attachments are optional. A written description is enough to start a report. Do not send audio data (including PCM), streams or recordings, stream keys, passwords, or tokens. This template does not request scene collections or crash dumps.

1. Keep the original log locally and make a separate copy for sharing. Limit the excerpt to the period around the problem and omit unrelated content.
2. Open the copy as text and review all of it. Look for source names, scene names, UUIDs, file paths containing usernames, email addresses, and credentials in URLs. Check exception messages too.
3. Replace identifying details with labels such as “[Source A]”, “[UUID-A]”, and “[Private path]”. Use the same label for the same item and different labels for different items. Retain error names, states, values, and event order where they contain no confidential information.
4. Crop screenshots to the relevant area. Cover stream previews, chat, account names, source names, paths, and other private content with solid opaque blocks. Reopen the exported image to confirm the hidden information is no longer visible and the relevant status remains readable.
5. Check filenames for personal information and review the final files again. If you are unsure you can safely remove private content, omit the attachment and describe the situation in words.

Do not assume automatic redaction has been applied. Review manually. A product feature for support-log export, preview, or automatic redaction has not been established at this stage. Some states may not be logged; if you cannot find a matching log entry, still describe the screen state and reproduction steps.

## Reporting an operation or state

- Identify whether the problem concerns source selection, PRE / POST waveforms, estimated delay D, current sync offset S, or estimated residual D+S. Do not guess missing values. “—” means unavailable, not 0 ms.
- “Auto adjust” sets S=−D using a valid delay estimate. It does not add a correction to the current offset or continuously follow changes. The ± controls, “Apply entered offset”, “Undo”, and “Reset offset” are also product operations. Reset sets the offset to 0 ms; it does not restore the pre-installation value.
- Undo restores the value before the last successful change. Changes to the source or settings can make it unavailable. Reading the current offset again does not restore an Undo invalidated by an external offset change. Do not repeatedly click a disabled control to try to enable it.
- The displayed POST position and estimated residual are not measurements of the corrected output. If the problem occurs in a recording, describe that separately from an on-screen problem. You do not need to send the recording.
- For installation, update, removal, or recovery problems, include the executable filename, operation sequence, whether it completed or was interrupted, and the displayed error. Do not disable protections, change access permissions, or delete management records or backups.

## About this pre-release candidate

Auto, manual adjustment, Undo, and Reset have been confirmed in existing limited product tests. The target is 64-bit Windows and OBS 30+ (OBS 32.2.2 recommended). The latest candidate uses the OBS 32.2.2 SDK/API 32, and loading and dock display have been confirmed on a real system. Real-machine checks reported on 20 October 2026 found no problem with the latest Setup stopping while OBS was running or with reinstalling. OBS 30/31 remain untested on a real system. See the accompanying “Tested environment and current limitations”. Completion of this document does not mean that a release has been published or that every installation, removal, and recovery path has been verified.
