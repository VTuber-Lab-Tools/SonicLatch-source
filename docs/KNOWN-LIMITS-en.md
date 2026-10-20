# SonicLatch: Tested environment and current limitations (pre-release candidate)

Target environment: 64-bit Windows and OBS 30+ (OBS 32.2.2 recommended). Loading and dock display have been confirmed in OBS 32.2.2 on a real system. OBS 30/31 remain untested on a real system. The support target and the verified scope are distinct.

Based on saved evidence and subsequent real-machine reports from 20 October 2026. Product targets are distinct from the candidates and conditions actually tested. No problem was reported with the latest Setup stopping while OBS was running or with reinstalling. This does not establish that every installation, removal, or interrupted-recovery path, or OBS 30/31, has been verified.

## Environment and verification scope

|Item|Target and evidence|Limits|
|---|---|---|
|OS / CPU|Intended for Windows x64; limited checks exist on the Windows test environment|Minimum Windows version coverage, macOS, Linux, and ARM support are unverified|
|OBS 33 (historical candidates)|Evidence is for 33.0.0-rc2. Existing fc4c-series product correction/display and 0b59-series I01 placement/loading have limited evidence|Do not generalize to every final OBS 33 version, future versions, or successful placement/loading by the latest Setup|
|OBS 32.2.2|The latest candidate uses the genuine OBS 32.2.2 SDK/API 32. It passed the isolated loader check, and normal OBS logs confirm module loading and dock addition|Loading and dock display do not establish a pass for every audio and correction condition in the latest DLL|
|OBS 30 / 31|Included in the product target, but untested on a real system|Do not count OBS 32.2.2 results as real-system passes on 30 / 31|
|Audio input|Existing 96fb-series evidence confirms waveforms and valid measurements from an ordinary microphone. Specified normalization and invalid-input cases also have evidence|Not a guarantee for every microphone, external filter, or audio content. Synthetic-input tests are not ordinary-microphone tests|
|Correction|Existing product candidates have limited evidence of actual Auto/manual/Undo/Reset writes, safe rejection, and external-change protection|Not a claim that the final DLL passed all tests or that all conditions synchronize perfectly|
|Languages|UI and help target 16 languages. Support is Japanese and English. Setup screens are Japanese|Help drafting/review and inclusion in the final package are separate. Support is not offered in 16 languages|
|Latest DLL|The API 32 build has a SHA-256 beginning 867b0a34. The DLL in the latest Setup e392d78 is byte-identical to the preceding candidate. Display names and help cover 16 languages|Do not substitute results from old API 33 builds or dedicated test modules for a full runtime test of this DLL|
|Setup|e392d78 was built and statically verified. The user reported no problem with stopping while OBS was running or with reinstalling on their real system. The DLL location is SonicLatch\bin\64bit\SonicLatch.dll|Removal, interrupted recovery, and other paths outside that report remain unconfirmed. Keep auxiliary-script passes distinct from real-machine reports|

## Measurement and adjustment limits

- Silence, mute, invalid input, audio content, or filter processing may prevent measurement. “—” means unavailable, not 0 ms. Wait for a fresh valid estimate and check the state. Reselecting a source or reading its offset is not a universal recovery method.
- Estimated delay D comes from comparing PRE and POST. The displayed POST position after applying current sync offset S, and residual D+S, are model estimates, not measurements of corrected output. Reading back OBS's stored offset does not prove recorded synchronization.
- Auto explicitly sets S=−D from a valid estimate when clicked. It does not continuously track changes; timing can change after adjustment or during a stream. You can apply it again after obtaining a fresh valid estimate, but perfect alignment is not guaranteed.
- The ± controls and Apply entered offset change the sync offset. Undo restores the value before the last successful operation; Reset sets it to 0 ms. External changes can invalidate Undo. Reading the offset again does not restore it.
- Existing recording checks use independent markers/reference tracks and document failures and limitations as well as successful cases. Recording is neither wholly untested nor guaranteed to synchronize perfectly. Results can vary with environment and processing conditions.
- Measuring the entire video path including cameras, automatic lip-sync, eliminating monitoring latency, and removing filter processing time are outside the scope. Changes to a source's sync offset also affect other scenes using that same source.

## Historical findings retained

- The old API 33 DLL (0b59 series) was rejected on OBS 32.2.2 with result −4 because it required newer libobs 33.0. That historical record is retained and is distinct from loading the current API 32 DLL.
- Historical shutdown records show an OBS memory diagnostic counter of 1, including matched runs both without and with the target DLL. Allocation ownership is unknown. This proves neither a product-specific leak nor leak-free operation. No verified workaround is available.
- The cause of an earlier performance-criterion failure remains unknown. Later fixed-condition normal/load 600-second passes exist, but do not prove that historical cause was fixed or guarantee a maximum duration for every callback. Retaining this record does not request additional long-duration retesting for this release.

Use the accompanying Japanese/English report template for support. Attachments are optional; manually review and redact only the necessary excerpts. Sending audio, recordings, or secrets is not requested, and personal information is not automatically transmitted.
