# Explicit opt-in development targets. Never package these modules.
set(SONICLATCH_QA_OBS32_EXE "" CACHE FILEPATH "Exact isolated OBS 32.2.2 executable")
set(SONICLATCH_QA_OBS33_EXE "" CACHE FILEPATH "Exact isolated OBS 33.0.0-rc2 executable")
if(NOT SONICLATCH_QA_OBS32_EXE AND NOT SONICLATCH_QA_OBS33_EXE)
 message(FATAL_ERROR "QA requires at least one explicit isolated OBS executable path")
endif()
foreach(var SONICLATCH_QA_OBS32_EXE SONICLATCH_QA_OBS33_EXE)
 file(TO_CMAKE_PATH "${${var}}" ${var})
 if("${${var}}" MATCHES "[\"\n\r]" OR "${${var}}" MATCHES "[;]")
  message(FATAL_ERROR "QA executable path contains unsupported characters")
 endif()
endforeach()
configure_file("${PRODUCT_DIR}/qa/local-paths.hpp.in" "${CMAKE_CURRENT_BINARY_DIR}/qa-local-paths.hpp" @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
add_library(soniclatch_isolated_legacy MODULE "${CMAKE_CURRENT_SOURCE_DIR}/normal-source-dock-module.cpp" "${PRODUCT_DIR}/src/display_adapter.cpp" "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")
target_include_directories(soniclatch_isolated_legacy PRIVATE "${PRODUCT_DIR}/src" "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
target_link_libraries(soniclatch_isolated_legacy PRIVATE qcp)
target_compile_definitions(soniclatch_isolated_legacy PRIVATE SONICLATCH_ENABLE_EMBEDDED_QA=1)
set_target_properties(soniclatch_isolated_legacy PROPERTIES OUTPUT_NAME SonicLatch RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/isolated-legacy" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/isolated-legacy" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/isolated-legacy")
# Independent renderer/geometry QA: synthetic data, no OBS process or devices.
add_executable(waveform_b_render_test "${PRODUCT_DIR}/tests/waveform_b_render.cpp")
target_include_directories(waveform_b_render_test PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(waveform_b_render_test PRIVATE qcp)

add_executable(compact_dock_render_test "${PRODUCT_DIR}/tests/compact_dock_render.cpp")
target_include_directories(compact_dock_render_test PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(compact_dock_render_test PRIVATE qcp)

# Strictly isolated, independent Qt fixture driver. Never included in payload.
add_library(soniclatch_product_flow_driver MODULE "${PRODUCT_DIR}/qa/product-flow-driver.cpp")
target_include_directories(soniclatch_product_flow_driver PRIVATE "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
target_link_libraries(soniclatch_product_flow_driver PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(soniclatch_product_flow_driver PRIVATE /utf-8)

add_executable(soniclatch_modal_harness_test "${PRODUCT_DIR}/qa/modal-harness-test.cpp")
target_link_libraries(soniclatch_modal_harness_test PRIVATE Qt6::Core Qt6::Widgets)
# This artifact is an isolated fixture binary, never the normal product target.
add_library(soniclatch_isolated_positive_product MODULE "${CMAKE_CURRENT_SOURCE_DIR}/normal-source-dock-module.cpp" "${PRODUCT_DIR}/src/display_adapter.cpp" "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")
target_include_directories(soniclatch_isolated_positive_product PRIVATE "${PRODUCT_DIR}/src" "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
target_link_libraries(soniclatch_isolated_positive_product PRIVATE qcp)
target_compile_definitions(soniclatch_isolated_positive_product PRIVATE SONICLATCH_ISOLATED_POSITIVE_FIXTURE=1 SONICLATCH_ENABLE_EMBEDDED_QA=1)
set_target_properties(soniclatch_isolated_positive_product PROPERTIES OUTPUT_NAME SonicLatch RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/isolated-positive" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/isolated-positive" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/isolated-positive")

add_library(soniclatch_save_restart_driver MODULE "${PRODUCT_DIR}/qa/save-restart-driver.cpp")
target_include_directories(soniclatch_save_restart_driver PRIVATE "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
target_link_libraries(soniclatch_save_restart_driver PRIVATE Qt6::Core Qt6::Widgets)

add_executable(capture_sample_rate_tests "${PRODUCT_DIR}/tests/capture_sample_rate_tests.cpp")
target_include_directories(capture_sample_rate_tests PRIVATE "${PRODUCT_DIR}/src")

add_executable(selection_rejection_qt_tests "${PRODUCT_DIR}/tests/selection_rejection_qt_tests.cpp")
target_include_directories(selection_rejection_qt_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(selection_rejection_qt_tests PRIVATE Qt6::Core Qt6::Widgets)

add_executable(timeline_diagnostic_tests "${PRODUCT_DIR}/tests/timeline_diagnostic_tests.cpp")
target_include_directories(timeline_diagnostic_tests PRIVATE "${PRODUCT_DIR}/src")

add_executable(capture_timeline_policy_tests "${PRODUCT_DIR}/tests/capture_timeline_policy_tests.cpp")
target_include_directories(capture_timeline_policy_tests PRIVATE "${PRODUCT_DIR}/src")

target_sources(capture_timeline_policy_tests PRIVATE "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")

add_executable(simple_settings_ui_tests "${PRODUCT_DIR}/tests/simple_settings_ui_tests.cpp")
target_include_directories(simple_settings_ui_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(simple_settings_ui_tests PRIVATE Qt6::Core Qt6::Widgets)

add_executable(manual_edit_commit_tests "${PRODUCT_DIR}/tests/manual_edit_commit_tests.cpp")
target_include_directories(manual_edit_commit_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(manual_edit_commit_tests PRIVATE Qt6::Core Qt6::Widgets)

add_executable(first_auto_guard_qt_tests "${PRODUCT_DIR}/tests/first_auto_guard_qt_tests.cpp")
target_include_directories(first_auto_guard_qt_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(first_auto_guard_qt_tests PRIVATE Qt6::Core Qt6::Widgets)

add_executable(capture_worker_replay_tests "${PRODUCT_DIR}/tests/capture_worker_replay_tests.cpp" "${PRODUCT_DIR}/src/display_adapter.cpp" "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")
target_include_directories(capture_worker_replay_tests PRIVATE "${PRODUCT_DIR}/src" "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
add_executable(raw_wave_display_adapter_tests "${PRODUCT_DIR}/tests/display_adapter_tests.cpp" "${PRODUCT_DIR}/src/display_adapter.cpp" "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")
target_include_directories(raw_wave_display_adapter_tests PRIVATE "${PRODUCT_DIR}/src")
target_compile_options(raw_wave_display_adapter_tests PRIVATE /UNDEBUG)
add_executable(raw_wave_display_consumer_tests "${PRODUCT_DIR}/tests/display_consumer_tests.cpp" "${PRODUCT_DIR}/src/display_adapter.cpp" "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")
target_include_directories(raw_wave_display_consumer_tests PRIVATE "${PRODUCT_DIR}/src")
target_compile_options(raw_wave_display_consumer_tests PRIVATE /UNDEBUG)

add_library(soniclatch_window_clock_driver MODULE "${PRODUCT_DIR}/qa/window-clock-readonly-driver.cpp")
target_include_directories(soniclatch_window_clock_driver PRIVATE "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
target_link_libraries(soniclatch_window_clock_driver PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(soniclatch_window_clock_driver PRIVATE /utf-8)

add_executable(paired_clock_freshness_tests "${PRODUCT_DIR}/tests/paired_clock_freshness_tests.cpp")
target_include_directories(paired_clock_freshness_tests PRIVATE "${PRODUCT_DIR}/src")

add_executable(live_capture_wave_tests "${PRODUCT_DIR}/tests/live_capture_wave_tests.cpp")
target_include_directories(live_capture_wave_tests PRIVATE "${PRODUCT_DIR}/src")

add_executable(live_wave_render_tests "${PRODUCT_DIR}/tests/live_wave_render_tests.cpp")
target_include_directories(live_wave_render_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(live_wave_render_tests PRIVATE qcp)

add_executable(selected_source_store_tests "${PRODUCT_DIR}/tests/selected_source_store_tests.cpp")
target_include_directories(selected_source_store_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(selected_source_store_tests PRIVATE Qt6::Core Qt6::Widgets)

add_executable(overlap_capture_tests "${PRODUCT_DIR}/tests/overlap_capture_tests.cpp")
target_include_directories(overlap_capture_tests PRIVATE "${PRODUCT_DIR}/src")
add_executable(overlap_qt_tests "${PRODUCT_DIR}/tests/overlap_qt_tests.cpp")
target_include_directories(overlap_qt_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(overlap_qt_tests PRIVATE qcp)

add_executable(capture_idle_race_tests "${PRODUCT_DIR}/tests/capture_idle_race_tests.cpp")
target_include_directories(capture_idle_race_tests PRIVATE "${PRODUCT_DIR}/src")

add_executable(overlay_display_tests "${PRODUCT_DIR}/tests/overlay_display_tests.cpp")
target_include_directories(overlay_display_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(overlay_display_tests PRIVATE qcp)

# Unified source-review batch. Pure tests are platform-independent; Qt target
# exercises the actual new production sample-column overload and label commit.
add_executable(source_batch_pure_tests "${PRODUCT_DIR}/tests/source_batch_pure_tests.cpp")
target_include_directories(source_batch_pure_tests PRIVATE "${PRODUCT_DIR}/src")
add_executable(source_batch_qt_tests "${PRODUCT_DIR}/tests/source_batch_qt_tests.cpp")
target_include_directories(source_batch_qt_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(source_batch_qt_tests PRIVATE qcp)

# Preserve the existing uncommitted configuration recovery regression target.
add_executable(configuration_recovery_tests "${PRODUCT_DIR}/tests/configuration_recovery_tests.cpp")
target_include_directories(configuration_recovery_tests PRIVATE "${PRODUCT_DIR}/src" "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
add_executable(source_batch_control_tests "${PRODUCT_DIR}/tests/source_batch_control_tests.cpp")
target_include_directories(source_batch_control_tests PRIVATE "${PRODUCT_DIR}/src")

# Normal-production async path: fake-API contracts are independent of Qt/OBS.
add_executable(async_model_tests "${PRODUCT_DIR}/tests/async_model_tests.cpp")
target_include_directories(async_model_tests PRIVATE "${PRODUCT_DIR}/src")
add_executable(async_offset_backend_tests "${PRODUCT_DIR}/tests/async_offset_backend_tests.cpp")
target_include_directories(async_offset_backend_tests PRIVATE "${PRODUCT_DIR}/tests/async-stubs" "${PRODUCT_DIR}/src")
foreach(target ui_combined_pure_tests ui_combined_qt_tests waveform_zoom_qt)
 add_executable(${target} "${PRODUCT_DIR}/tests/${target}.cpp")
 target_include_directories(${target} PRIVATE "${PRODUCT_DIR}/src")
endforeach()
target_link_libraries(ui_combined_qt_tests PRIVATE qcp)
target_link_libraries(waveform_zoom_qt PRIVATE qcp)
# Independent strict-path driver uses the SAME normal SonicLatch.dll candidate.
add_library(soniclatch_async_product_flow_driver MODULE "${PRODUCT_DIR}/qa/async-product-flow-driver.cpp")
target_include_directories(soniclatch_async_product_flow_driver PRIVATE "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")
target_link_libraries(soniclatch_async_product_flow_driver PRIVATE Qt6::Core Qt6::Widgets)
if(MSVC)
 foreach(target async_model_tests async_offset_backend_tests ui_combined_pure_tests ui_combined_qt_tests waveform_zoom_qt soniclatch_async_product_flow_driver)
  target_compile_options(${target} PRIVATE /utf-8 /UNDEBUG)
 endforeach()
endif()

# Product write-range regression: no OBS process, pure fake-backend targets.
add_executable(correction_range_tests "${PRODUCT_DIR}/tests/correction_range_tests.cpp" "${PRODUCT_DIR}/src/display_adapter.cpp")
target_include_directories(correction_range_tests PRIVATE "${PRODUCT_DIR}/src")
add_executable(correction_range_qt_tests "${PRODUCT_DIR}/tests/correction_range_qt_tests.cpp")
target_include_directories(correction_range_qt_tests PRIVATE "${PRODUCT_DIR}/src")
target_link_libraries(correction_range_qt_tests PRIVATE Qt6::Core Qt6::Widgets)

# Production packet splitter and clocks; no OBS process, device or UI required.
add_executable(capture_packet_split_tests "${PRODUCT_DIR}/tests/capture_packet_split_tests.cpp")
target_include_directories(capture_packet_split_tests PRIVATE "${PRODUCT_DIR}/src")
add_executable(capture_large_packet_replay_tests "${PRODUCT_DIR}/tests/capture_large_packet_replay_tests.cpp" "${PRODUCT_DIR}/integrations/waveform-model/waveform-model-draft/waveform_model.cpp")
target_include_directories(capture_large_packet_replay_tests PRIVATE "${PRODUCT_DIR}/src" "${OBS_HEADERS}" "${OBS_HEADERS}/../frontend/api" "${OBS_CONFIG_DIR}")

# Display-only history/offset interval regression, no Qt or OBS runtime.
add_executable(display_offset_history_tests "${PRODUCT_DIR}/tests/display_offset_history_tests.cpp")
target_include_directories(display_offset_history_tests PRIVATE "${PRODUCT_DIR}/src")
