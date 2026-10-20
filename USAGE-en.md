# Using SonicLatch

## Installation

The installer is a pre-publication candidate. Its final filename and installation steps will be added here when the distribution is available.

After publication, close OBS and follow the instructions supplied with the distribution package. Once installed, start OBS and open the SonicLatch dock.

## Removal

Close OBS before removing the plugin. Specific removal steps will be added here after the final installer and uninstaller have been tested.

## Measure → inspect the waveforms → adjust

1. Select the audio source you want to inspect and provide an audio signal.
   The measurement filter “音声遅延測定（SonicLatch）” is added automatically to capture the audio before processing. No manual addition is needed. Do not remove, disable, or reorder it while SonicLatch is in use.
2. Check the PRE (before filters) and POST (after filters) waveforms, estimated delay, and status.
3. Once the estimate is valid, use Auto to set an offset that compensates for the measured delay.
4. Fine-tune with ± or numeric entry as needed.
5. Check the audio in an actual recording or stream. Use Undo to revert the previous operation, or Reset to set the offset to 0 ms.

Delay D is estimated by comparing PRE and POST. The displayed POST waveform is shifted in time by the current sync offset S; this is not a new measurement of the corrected output.

## What each control does

- Auto: Sets the sync offset to −D for a valid estimated delay D. It does not add −D to the current offset.
- ±: Adjusts the current sync offset.
- Numeric entry: Sets the sync offset to the entered value.
- Undo: Restores the value before the previous successful operation. Changes to the source or external offset can make Undo unavailable.
- Reset: Sets the sync offset to 0 ms.
- Residual: Displays D + S, the estimated delay plus the sync offset. This is not a measurement of the corrected output.

For example, if D is +80 ms, Auto sets the offset to −80 ms, regardless of the current offset.

You can also inspect the current sync offset in OBS Advanced Audio Properties. After changing the value or source outside SonicLatch, check the current state and a new valid estimate before adjusting it.

## Ranges and display

Target environment: 64-bit Windows, OBS Studio 33 or later. This candidate was tested on 33.0.0-rc2 only; untested versions are not guaranteed.

Audio configuration: stereo at 44.1 kHz or 48 kHz.

The measurement range for estimated delay D is approximately within ±500 ms, excluding the search endpoints. The write range for sync offset S is −950 to +1000 ms. The measurement and write ranges are different.

Measurement and display alone do not change the sync offset. If the signal is silent, muted, or the estimate is invalid, check the input and status display.

[Back to README](README-en.md)

## Timing changes and readjustment

Audio may not align perfectly depending on your environment and processing load. Estimated values and audio delay can change during a stream or after adjustment. If the timing drifts, check the input and wait for a valid estimate, then press Auto again to readjust using the current estimate. Auto does not continuously track changes or guarantee perfect alignment.
