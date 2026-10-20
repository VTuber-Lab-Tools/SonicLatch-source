# SonicLatch

SonicLatch is an OBS plugin that lets you adjust voice delay introduced by audio processing with one button. Inspect the waveforms, use Auto, and fine-tune manually if needed.

[日本語](README.md) · [Installation, removal, and usage](USAGE-en.md) · [0.1.0 release notes](RELEASE-NOTES-en.md)

## Measure → inspect the waveforms → adjust

Select an audio source and provide an audio signal to inspect the PRE/POST waveforms and estimated delay. Once a valid estimate is available, use Auto to compensate for it, then fine-tune with ± or numeric entry as needed. Measurement and display alone do not change the sync offset.

Delay D is estimated by comparing PRE and POST. The displayed POST waveform is shifted in time by the current sync offset S; this is not a new measurement of the corrected output.

- Auto: Sets the sync offset to −D for a valid estimated delay D. It does not add −D to the current offset.
- ±: Adjusts the current sync offset.
- Numeric entry: Sets the sync offset to the entered value.
- Undo: Restores the value before the previous successful operation. Changes to the source or external offset can make Undo unavailable.
- Reset: Sets the sync offset to 0 ms.
- Residual: Displays D + S, the estimated delay plus the sync offset. This is not a measurement of the corrected output.

## Target environment and ranges

Target environment: 64-bit Windows and OBS 30+ (OBS 32.2.2 recommended). Loading and dock display have been confirmed in OBS 32.2.2 on a real system. OBS 30/31 remain untested on a real system. The support target and the verified scope are distinct.

Audio configuration: stereo at 44.1 kHz or 48 kHz.

The measurement range for estimated delay D is approximately within ±500 ms, excluding the search endpoints. The write range for sync offset S is −950 to +1000 ms. The measurement and write ranges are different.

Silence, mute, or an invalid estimate can make measurement or correction unavailable. Measurement covers the audio source’s filter chain, not automatic audio/video sync or end-to-end latency to a streaming destination.

## Installation and removal

For first-time setup, see [installation and removal instructions](USAGE-en.md). Close OBS before running setup. The DLL is installed in obs-studio\plugins\SonicLatch\bin\64bit under the Windows common application-data folder.

## Building

See [BUILDING.md](BUILDING.md).

## License, source code, and support

Operated by VtuberLab(VLab)

Copyright (C) 2026 VTuber-Lab-Tools

Project-authored portions are licensed under GNU GPL version 3 or any later version (GPL-3.0-or-later). See [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The publication repository for corresponding source code and build instructions is [SonicLatch-source](https://github.com/VTuber-Lab-Tools/SonicLatch-source). Report technical issues through [GitHub Issues](https://github.com/VTuber-Lab-Tools/SonicLatch-source/issues). The initial source, tag, and distribution artifacts have not yet been published.

Include your OBS and SonicLatch versions, reproduction steps, and observed behavior in bug reports. Check logs for personal information before sharing them.

## Timing changes and readjustment

Audio may not align perfectly depending on your environment and processing load. Estimated values and audio delay can change during a stream or after adjustment. If the timing drifts, check the input and wait for a valid estimate, then press Auto again to readjust using the current estimate. Auto does not continuously track changes or guarantee perfect alignment.
