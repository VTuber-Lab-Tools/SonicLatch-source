cmake_minimum_required(VERSION 3.24)
project(SonicLatch_fixture_helper LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_executable(SonicLatch-helper-fixture main.cpp)
target_compile_definitions(SonicLatch-helper-fixture PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(SonicLatch-helper-fixture PRIVATE /W4 /WX /permissive- /utf-8)
target_link_libraries(SonicLatch-helper-fixture PRIVATE bcrypt ole32)
enable_testing()
add_test(NAME native_fixture_transaction COMMAND SonicLatch-helper-fixture --fixture-self-test)
add_executable(SonicLatch-registration-security-tests registration_security_tests.cpp)
target_compile_options(SonicLatch-registration-security-tests PRIVATE /W4 /WX /permissive- /utf-8)
target_compile_definitions(SonicLatch-registration-security-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SonicLatch-registration-security-tests PRIVATE bcrypt advapi32 shell32 ole32)
add_test(NAME registration_security COMMAND SonicLatch-registration-security-tests)
add_executable(SonicLatch-registration-boundary-tests registration_boundary_tests.cpp)
target_compile_options(SonicLatch-registration-boundary-tests PRIVATE /W4 /WX /permissive- /utf-8)
target_compile_definitions(SonicLatch-registration-boundary-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SonicLatch-registration-boundary-tests PRIVATE bcrypt advapi32 shell32 ole32)
add_test(NAME registration_boundaries COMMAND SonicLatch-registration-boundary-tests)

add_executable(SonicLatch-directory-policy-tests directory_policy_tests.cpp)
target_compile_options(SonicLatch-directory-policy-tests PRIVATE /W4 /WX /permissive- /utf-8)
target_compile_definitions(SonicLatch-directory-policy-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SonicLatch-directory-policy-tests PRIVATE bcrypt advapi32 shell32 ole32)
add_test(NAME directory_policy COMMAND SonicLatch-directory-policy-tests)

add_executable(SonicLatch-uninstaller-slot-tests uninstaller_slot_tests.cpp)
target_compile_options(SonicLatch-uninstaller-slot-tests PRIVATE /W4 /WX /permissive- /utf-8)
target_compile_definitions(SonicLatch-uninstaller-slot-tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SonicLatch-uninstaller-slot-tests PRIVATE bcrypt advapi32 shell32 ole32)
add_test(NAME uninstaller_slots COMMAND SonicLatch-uninstaller-slot-tests)

# Product connection sources are staged for integration review only.
# Existing fixture is unchanged; no product CLI is enabled here.
add_library(SonicLatch-product-transaction INTERFACE)
target_include_directories(SonicLatch-product-transaction INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(SonicLatch-product-transaction INTERFACE cxx_std_20)
target_sources(SonicLatch-product-transaction INTERFACE
 "${CMAKE_CURRENT_SOURCE_DIR}/product_transaction.hpp"
 "${CMAKE_CURRENT_SOURCE_DIR}/product_ipc.hpp"
 "${CMAKE_CURRENT_SOURCE_DIR}/product_trust.hpp"
 "${CMAKE_CURRENT_SOURCE_DIR}/product_windows_store.hpp")
target_compile_definitions(SonicLatch-product-transaction INTERFACE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SonicLatch-product-transaction INTERFACE bcrypt advapi32 shell32 ole32)

# Fixed-catalog product CLI. Default catalog is empty and exits 90 before writes.
add_executable(SonicLatch-helper product_main.cpp)
target_link_libraries(SonicLatch-helper PRIVATE SonicLatch-product-transaction)
target_compile_options(SonicLatch-helper PRIVATE /W4 /permissive- /utf-8)
target_link_options(SonicLatch-helper PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
if(SONICLATCH_REVIEWED_CATALOG)
 if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product_release.generated.hpp")
  message(FATAL_ERROR "Fixed approved payload catalog missing")
 endif()
 target_compile_definitions(SonicLatch-helper PRIVATE SONICLATCH_REVIEWED_CATALOG)
endif()
