# Recorded build inputs and package stages (2026-10-20)

This is the current OBS32.2.2 DLL record. Earlier OBS33 experiments remain historical evidence and are not the distribution baseline.

## Frozen DLL identity

- Compile baseline: `19e221286bab7b10a5a541b6ab6832c41318956a`.
- DLL: 890880 bytes; SHA-256 `867b0a34ff371021075d53594759158a81094769e85dbe9d5e34ce94d4b976f8`.
- Compile-source snapshot: 49 files; SHA-256 `bea83f039cc363da065f87f8f3f61fecf872ade4ccbb1359a9eadc4ee922c807`.
- Original build proof SHA-256: `814c59bd4d378caf31e32d174e5c53c9860b5e85c1060f20eed84e786e4a3eed`.
- Configure exit 0; build exit 0; 1022 tracked build inputs.
- QA OFF / wheel diagnostic OFF; no module API version override.

OBS32.2.2 module loading and dock registration were observed. The user later accepted the native e392d78 overwrite/reinstall and running-OBS rejection. Those observations do not validate OBS30/31 or every audio source/filter combination.

## External sources and tools

- Official OBS32.2.2 source: https://github.com/obsproject/obs-studio/releases/download/32.2.2/OBS-Studio-32.2.2-Sources.tar.gz
  - 16660601 bytes; SHA-256 `ec81fb66b03e75ddb3076b576f62679c39262e0e9960cef3e17a40dc5d68e6b4`.
  - Include roots are `libobs` and `frontend/api` in that source tree.
- Qt6.11.1 SDK from the official OBS dependency asset: https://github.com/obsproject/obs-deps/releases/download/2026-07-15/windows-deps-qt6-2026-07-15-x64.zip
  - 260608957 bytes; SHA-256 `7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579`.
  - The existing acquired archive was reused; this documentation correction did not download or install it.
- Bundled QCustomPlot2.1.1: https://www.qcustomplot.com/release/2.1.1/QCustomPlot-source.tar.gz . See `third_party/` and THIRD_PARTY_NOTICES for preserved copyright and license notices.
- Recorded tools: CMake4.3.1, Visual Studio2022 Build Tools / MSVC19.44.35224 (tools14.44.35207), Windows SDK10.0.26100.0; NSIS3.13 for Setup. Existing installations were reused. The source does not contain their installers or assert complete acquisition provenance for every installed tool.

## obsconfig.h

The official `libobs/obsconfig.h.in` SHA-256 is `5b2bb343ddc6c1f8ca0c683a3d87a99029f7d82c7df13da0bda00aca0ec32f44`. The recorded generated header SHA-256 is `1879108423679ba8030c3309dfe17607552a1cb7e4f0bd371cbf373038217b6b`.

The configuration uses `OBS_RELEASE_CANDIDATE=0`, `OBS_BETA=0`; OBS data/plugin paths, GIO/PULSE/XCB/WAYLAND remain undef. A matching CMake script is:

```cmake
set(OBS_RELEASE_CANDIDATE 0)
set(OBS_BETA 0)
configure_file("${OBS32_SOURCE}/libobs/obsconfig.h.in" "${OUT_DIR}/obsconfig.h")
```

Save it as a separate script and run with explicit `OBS32_SOURCE` and `OUT_DIR` values. Do not use the older OBS33 RC2 script or substitute an empty header. Hash the resulting file against the recorded header before building. [BUILDING.md](BUILDING.md) gives the product configure/build commands corresponding to these SDK inputs.

## Inventory and binding

The public `DEPENDENCIES.lock.json` maps the actual MSBuild read inputs to logical roots: PRODUCT_SOURCE: 49; BUILD_GENERATED: 11; OBS_32_2_2: 38; OBS_32_CONFIG: 1; QT_6_11_1: 531; MSVC_14_44_35207: 163; WINDOWS_SDK_10: 192; CMAKE: 35; WINDOWS_SYSTEM: 2 (1022 total).

`SOURCE-SNAPSHOT.json` stores sorted relative paths with file byte counts and SHA-256 values. In this build the snapshot was reconstructed after compilation from the immutable compile commit and actual MSBuild source read inputs; its snapshotTiming field records that timing, and all 49 files matched. For future builds, capture the source snapshot before compilation. The input recorder reads the actual `*.read.*.tlog` files (excluding CompilerIdCXX), resolves and deduplicates existing paths, hashes the files, and fails on unmapped roots. The full absolute-path input inventory is local evidence and must not be published.

`packaging/windows/record-build-evidence.py` records the inventory, proof and binding from an existing build. It does not perform the compilation. Provide the real source snapshot, build directory, build result, DLL and root mappings; the original build proof remains preserved. `build-binding.json` contains exactly:

```json
{"baseCommit":"<compile commit>","dllSha256":"<DLL hash>","dllSize":890880,"sourceManifestSha256":"<snapshot hash>","buildProofSha256":"<proof hash>"}
```

The wrapper checks the proof's byte hash. Human review and the source inventory comparison still establish how the build, SDK and distribution source relate. A new package can add a supplementary proof that records its installer commit and distribution-source inventory while preserving the original DLL build record and all 49 compile inputs.

## Source to package, helper and Setup

Freeze the curated corresponding source ZIP first. Freeze the 42 data files separately with `relativePath`, local `inputPath` and `sha256`; private input paths stay outside the source ZIP. Include 16 locale files, 16 guides, one help index, six usage/support/limitations files, LICENSE, THIRD_PARTY_NOTICES and SOURCE-AVAILABILITY. Together with the DLL these form 43 payload files.

```powershell
python packaging/windows/prepare_product_connection.py --repo <source-root> --dll <DLL> --dll-sha256 <hash> --dll-size <bytes> --build-binding-json <binding.json> --build-proof <proof.json> --source-zip <source.zip> --source-sha256 <hash> --source-size <bytes> --build-source-manifest <SOURCE-SNAPSHOT.json> --data-inputs-json <data-inputs.json> --output-dir <fresh-connection-dir>
```

This produces the archive, manifest, release inputs and disabled default catalog. For an explicitly reviewed installer, invoke `generate_release_catalog.py` with the exact current archive/manifest/source, authenticated predecessor archives, `--predecessor-ledger packaging/windows/reviewed-predecessors.json`, `--output <header>` and `--enable-installation`. Every required predecessor must be present, including installed e392d78 manifest `ef84436ba8abce3aaeb0bd2de6dd119eb4a7cbd7653fb3ebdfed90b5828ca768`. Stage the generated header as `product_release.generated.hpp` beside the helper source.

```powershell
cmake -S <staged-helper> -B <fresh-helper-build> -G "Visual Studio 17 2022" -A x64 -DSONICLATCH_REVIEWED_CATALOG=ON
cmake --build <fresh-helper-build> --config Release --target SonicLatch-helper --parallel 2
python packaging/windows/freeze_nsis_input.py --helper <helper.exe> --sha256 <hash> --output-directory <fresh-nsis-input>
makensis /INPUTCHARSET UTF8 /OUTPUTCHARSET UTF8 /DSONICLATCH_PACKAGE_INPUT=<fresh-nsis-input>/product_package.generated.nsh packaging/windows/SonicLatch.nsi
```

Use fresh output directories and retain old review installers, receipts and evidence. A documentation/data change regenerates the source ZIP, manifest, catalog, helper and Setup; it does not rebuild the unchanged DLL. Check the final source/data inventories, helper embedded bytes, Setup extraction and SHA-256 list together. Installation, source publication and production merge each require the applicable reviewed target and authorization.
