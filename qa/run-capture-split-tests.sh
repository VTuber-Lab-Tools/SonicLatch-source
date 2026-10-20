#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="${OUT_DIR:-qa/capture-split-results}"
mkdir -p "$out"
cxx="${CXX:-g++}"
common=(-std=c++20 -O2 -pthread -Isrc)
for name in capture_packet_split_tests paired_clock_freshness_tests capture_sample_rate_tests overlap_capture_tests capture_idle_race_tests; do
 "$cxx" "${common[@]}" "tests/$name.cpp" -o "$out/$name"
 "$out/$name"
done
wave=integrations/waveform-model/waveform-model-draft/waveform_model.cpp
"$cxx" "${common[@]}" -Wno-unknown-pragmas -Itests/capture-stubs tests/capture_worker_replay_tests.cpp src/display_adapter.cpp "$wave" -o "$out/capture_worker_replay_tests"
"$out/capture_worker_replay_tests"
"$cxx" "${common[@]}" -Wno-unknown-pragmas -Itests/capture-stubs tests/capture_large_packet_replay_tests.cpp "$wave" -o "$out/capture_large_packet_replay_tests"
"$out/capture_large_packet_replay_tests"
