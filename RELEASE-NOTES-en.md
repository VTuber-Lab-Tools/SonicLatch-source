# SonicLatch 0.1.0

## See and adjust audio timing in OBS

SonicLatch is an OBS plugin that lets you adjust voice delay introduced by audio processing with one button. Inspect the waveforms, use Auto, and fine-tune manually if needed.

## Highlights

- Overlaid PRE/POST waveforms
- Estimated delay, current sync offset, and estimated residual after correction
- Auto, ±, numeric entry, Undo, and Reset
- Status indicators and correction guards based on the audio source and estimate
- Theme-aware display and scrollable dock content

Auto sets the absolute offset to −D for a valid estimated delay D; it does not add −D to the current value. The ± controls adjust the current value. Undo restores the value before the previous successful operation, and Reset sets it to 0 ms. The residual D + S is an estimate.

## Installation and first use

See [Usage](USAGE-en.md). Select an audio source, inspect the measurement and waveforms, then use Auto and fine-tune manually as needed. Measurement and display alone do not change the sync offset.

## Target environment and ranges

Target environment: 64-bit Windows, OBS Studio 33 or later. This candidate was tested on 33.0.0-rc2 only; untested versions are not guaranteed.

Audio configuration: stereo at 44.1 kHz or 48 kHz.

The measurement range for estimated delay D is approximately within ±500 ms, excluding the search endpoints. The write range for sync offset S is −950 to +1000 ms. The measurement and write ranges are different.

Measurement or correction can be unavailable depending on the source and signal. Automatic audio/video sync and end-to-end latency measurement to a streaming destination are outside the measurement scope.

## License, source code, and support

Copyright (C) 2026 VTuber-Lab-Tools

Project-authored portions are licensed under GNU GPL version 3 or any later version (GPL-3.0-or-later). See [LICENSE](LICENSE), [COPYRIGHT](COPYRIGHT), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The publication repository for corresponding source code and build instructions is [SonicLatch-source](https://github.com/VTuber-Lab-Tools/SonicLatch-source). Report technical issues through [GitHub Issues](https://github.com/VTuber-Lab-Tools/SonicLatch-source/issues). The initial source, tag, and distribution artifacts have not yet been published.

Include your OBS and SonicLatch versions, reproduction steps, and observed behavior in bug reports. Check logs for personal information before sharing them.

## Timing changes and readjustment

Audio may not align perfectly depending on your environment and processing load. Estimated values and audio delay can change during a stream or after adjustment. If the timing drifts, check the input and wait for a valid estimate, then press Auto again to readjust using the current estimate. Auto does not continuously track changes or guarantee perfect alignment.
