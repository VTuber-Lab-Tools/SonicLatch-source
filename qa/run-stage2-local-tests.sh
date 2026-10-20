#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${OUT_DIR:-qa/stage2-local-results}"
mkdir -p "$out"
python3 tests/collection_event_contract_tests.py
SONICLATCH_TEST_UBSAN=1 python3 tests/collection_event_contract_tests.py
for test in display_offset_history_tests ui_combined_pure_tests source_batch_pure_tests live_capture_wave_tests waveform_viewport_tests capture_packet_split_tests; do
 echo "BUILD/RUN $test (local synthetic unit test, not OBS runtime)"
 g++ -std=c++20 -O2 -pthread -Isrc "tests/$test.cpp" -o "$out/$test"
 "$out/$test"
done
g++ -std=c++20 -O2 -pthread -Isrc tests/correction_range_tests.cpp src/display_adapter.cpp -o "$out/correction_range_tests"
"$out/correction_range_tests"
g++ -std=c++20 -O2 -pthread -Wno-unknown-pragmas -Isrc -Itests/capture-stubs tests/capture_large_packet_replay_tests.cpp integrations/waveform-model/waveform-model-draft/waveform_model.cpp -o "$out/capture_large_packet_replay_tests"
"$out/capture_large_packet_replay_tests"
