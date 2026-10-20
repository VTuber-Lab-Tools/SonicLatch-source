# Building SonicLatch for Windows

The 0.1.0 Windows DLL was built with the genuine OBS Studio **32.2.2** SDK, Qt **6.11.1**, MSVC 19.44.35224 (tools 14.44.35207), Windows SDK 10.0.26100.0 and CMake 4.3.1. Both product QA and wheel diagnostics were OFF. Its SHA-256 is `867b0a34ff371021075d53594759158a81094769e85dbe9d5e34ce94d4b976f8` (890880 bytes).

OBS 32.2.2 is the tested and recommended host. The product targets OBS 30 or newer; OBS 30/31 have not been tested. An OBS33-only build existed during development and is not the DLL described here. Do not override the module API version to simulate compatibility.

## Dependencies and configuration

Use a C++20 MSVC compiler, CMake 3.24 or newer, Windows SDK, the exact Qt 6.11.1 Core/Gui/Widgets SDK, OBS32.2.2 libobs/frontend headers, and the bundled QCustomPlot 2.1.1 source. OBS, Qt, Microsoft tools, Python and NSIS binaries are external dependencies and are not included in this source archive.

`DEPENDENCIES.lock.json` records 1022 actual build inputs under logical roots, without workstation paths. `OBS_32_2_2` and `OBS_32_CONFIG` identify the SDK and its generated configuration. `PRODUCT_SOURCE` contains 49 compile inputs; `BUILD_GENERATED` identifies generated build files. The lock is an input inventory, not a dependency downloader or permission to redistribute the listed SDKs.

Generate `obsconfig.h` from the official 32.2.2 `libobs/obsconfig.h.in` with `OBS_RELEASE_CANDIDATE=0` and `OBS_BETA=0`; preserve the template's normal undef handling. [BUILD-INPUTS.md](BUILD-INPUTS.md) provides the exact template/config hashes and configuration example. The historical `generate-obsconfig.cmake` script is explicitly for OBS33 RC2 and must not be used for this SDK32 build.

## Product DLL

Run from this source root, replacing the SDK paths with the verified locations:

```powershell
cmake -S poc/normal-source-dock -B build/product -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/sdk/qt6.11.1" `
  -DOBS_HEADERS="C:/sdk/obs-studio-32.2.2-sources/libobs" `
  -DOBS_CONFIG_DIR="C:/build/obs32-config" `
  -DSONICLATCH_BUILD_QA=OFF `
  -DSONICLATCH_BUILD_WHEEL_DIAGNOSTIC=OFF
cmake --build build/product --config Release --target pitatuner_normal_source_dock --parallel 2
```

The output is `SonicLatch.dll`; the internal target name is retained. The DLL uses `MultiThreadedDLL` (/MD). The installed layout is `SonicLatch/bin/64bit/SonicLatch.dll` and `SonicLatch/data/`. The payload contains the DLL plus 42 data files: 16 locales, 16 language guides, one help index, six usage/support/limitations files, LICENSE, THIRD_PARTY_NOTICES and SOURCE-AVAILABILITY.

`qa/` and `tests/` contain development targets; they are not installed. Only run tests relevant to a change. Normal product builds keep QA and wheel diagnostics OFF; see `qa/development-targets.cmake` for optional targets.

## Corresponding source and build evidence

The DLL compile baseline is `19e221286bab7b10a5a541b6ab6832c41318956a`. Its 49 compile inputs and 1022 tracked build inputs were hashed and recorded. The saved compile-source snapshot was reconstructed after the build from the immutable compile commit and actual MSBuild source read inputs; all 49 files matched byte for byte. Later installer, fixture and documentation changes do not imply a DLL rebuild: the package preparation verifies the 49 DLL compile inputs against the frozen snapshot and retains the DLL bytes. The distribution source commit therefore differs from the DLL compile baseline.

The build binding connects the DLL hash/size, compile-source manifest and build proof. The release's separate source inventory, product manifest and SHA-256 list identify the exact corresponding source and package. A byte binding does not by itself prove ABI compatibility or reproducible binaries. See [BUILD-INPUTS.md](BUILD-INPUTS.md) for the recorded build and packaging stages.

The earlier installer candidate e392d78 was accepted after a normal native overwrite/reinstall and OBS-running rejection reported by the user, with independent receipt/payload/registration readback. A later documentation repack preserves that evidence but is a distinct installer artifact; its own validation status is recorded alongside its hashes. Native uninstall and interrupted-recovery acceptance have not been recorded.

## Installer

`packaging/windows/` supplies helper, NSIS and package/catalog generation sources. Python 3.10 or newer and NSIS 3.13 are additional build dependencies. The package tooling's Python modules are included under `integrations/windows-candidates-20261019/installer-next-implementation/`.

The product path is `product_package.py` / `build_release_package.py` / `generate_release_catalog.py`. The older `generate_product_catalog.py` is a separate candidate path. First freeze the corresponding source, DLL/build proof and every data input; generate the product archive/manifest and fixed catalog; build the helper; freeze its bytes; then compile NSIS. Changing source or data requires regenerating the catalog, helper and Setup. The reviewed predecessor ledger must include every required authenticated predecessor, including the actually installed e392d78 release.

The default helper catalog is empty and fails closed before product writes. `SONICLATCH_REVIEWED_CATALOG=ON` requires the generated `product_release.generated.hpp`. The helper uses `MultiThreaded` (/MT) and requests administrator execution. Source compilation alone does not authorize installation or publication. Generated payload headers, build trees, executables, credentials, private receipts and workstation configuration are excluded from corresponding source.

## Offset calculation

For an accepted delay estimate D and current sync offset S, recommended offset is the absolute value -D; displayed estimated residual is D + S. The increment buttons operate relative to the current S. The capture waveform is not the actual output after sync-offset processing; see the usage and limitations documents.
