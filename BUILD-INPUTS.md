# 再構築担当への確認済み証跡（2026-10-20）

対象DLLは890880 bytes、SHA256 `0b59d3fd3ff026826d80a7c51166bd2a78107abe70986e5041dc3f008aa5d5d8`。以下はこのDLLの実ビルドに関する証跡であり、今回のinstaller修正の再導入成功を意味しません。OBS33.0.0-rc2以外は未保証です。

## OBS SDKとQt

- OBS tag: `33.0.0-rc2`、commit: `f1c0a116441bdb7a1de7896f810009ea487b8508`。
- SDK公式ソース: https://github.com/obsproject/obs-studio/releases/download/33.0.0-rc2/OBS-Studio-33.0.0-rc2-Sources.tar.gz
  - 16721301 bytes、SHA256 `eb0e7a3e7de80c8287e8f79f17e9668b1b442166ec83e98be3189de0658e734e`。
  - 実際のinclude rootは展開先の `libobs` と `frontend/api`。
- 実使用Qt: **6.11.1**。公式OBS依存パッケージ:
  https://github.com/obsproject/obs-deps/releases/download/2026-07-15/windows-deps-qt6-2026-07-15-x64.zip
  - 260608957 bytes、SHA256 `7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579`。
  - 取得済みファイルのハッシュと、2026-10-20に読み取った公式GitHub assetのsize/digestが一致。追加取得はしていません。
  - 実prefixの末尾は歴史的名称 `dependencies/qt6.11.1-obs32` ですが、OBS32 SDKを使用したという意味ではありません。
- QCustomPlot: 2.1.1、公式取得元は既存の `setup_qcp_probe.py` に記録された https://www.qcustomplot.com/release/2.1.1/QCustomPlot-source.tar.gz 。実配布ソースと変更・ライセンスはソースZIP内のthird_partyを参照。
- 実ツール: CMake 4.3.1、VS2022 Build Tools / MSVC 19.44.35224（tools 14.44.35207）、Windows SDK 10.0.26100.0、NSIS 3.13。これらのインストーラー取得URL・取得時ハッシュは今回抽出できていません。既存ツールを利用した事実と、完全な取得経路証明を区別してください。

## obsconfig.hの生成

元の生成設定は `qa/obs33-rc2/CMakeLists.txt` にあり、以下の3行です。`configure_file` に **@ONLYはありません**。

```cmake
set(OBS_RELEASE_CANDIDATE 2)
set(OBS_BETA 0)
configure_file("${OBS33_SOURCE}/libobs/obsconfig.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/obsconfig.h")
```

入力 `libobs/obsconfig.h.in` SHA256:
`5b2bb343ddc6c1f8ca0c683a3d87a99029f7d82c7df13da0bda00aca0ec32f44`。
実生成 `obsconfig.h` SHA256:
`be0ff8239632948b6bf711c002a61e04c06e890eb5139106e01c16aef9200ea7`。
OBS_DATA_PATH/OBS_PLUGIN_PATH/OBS_PLUGIN_DESTINATIONおよびGIO/PULSE/XCB/WAYLANDはundef、RC=2/BETA=0です。

同梱の `packaging/windows/generate-obsconfig.cmake` による、余分なfixtureをビルドしない再生成コマンド:

```powershell
cmake -DOBS33_SOURCE=C:/sdk/obs-studio-33.0.0-rc2-sources -DOUT_DIR=C:/build/obs33-config -P packaging/windows/generate-obsconfig.cmake
Get-FileHash C:/build/obs33-config/obsconfig.h -Algorithm SHA256
```

これは元のconfigure_file設定から抽出した再現用コマンドです。元のfixture configureの全CLIを記録したログは未発見です。今回この再生成手順を別ディレクトリで実行し、実生成物と同じハッシュになることを検証済みです。

## 実DLLのconfigure/build引数

`$TASK`は専用workspace、`$CANDIDATE=$TASK/catalog-final-candidate`、`$OBS33=$TASK/isolated-obs33-rc2-compare/acquisition/source/obs-studio-33.0.0-rc2-sources`、`$CFG=$TASK/isolated-obs33-rc2-compare/build-fixture/generated`です。実記録の引数をこのルート表記へ置換しています。

```powershell
cmake -S "$CANDIDATE/source/SonicLatch/poc/normal-source-dock" -B "$CANDIDATE/build-obs33-final-off" -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$TASK/dependencies/qt6.11.1-obs32" "-DOBS_HEADERS=$OBS33/libobs" "-DOBS_CONFIG_DIR=$CFG" -DSONICLATCH_BUILD_QA=OFF -DSONICLATCH_BUILD_WHEEL_DIAGNOSTIC=OFF
cmake --build "$CANDIDATE/build-obs33-final-off" --config Release --target pitatuner_normal_source_dock --parallel 2
```

`build-request-obs33.json` / `build-result-obs33.json` / configure・buildログで両exit=0を確認済みです。製品音声・UIコードは今回変更していません。installer helperのみの修正で製品DLLの全回帰をやり直す必要はありません。

## compile inventoryとbuild binding

元の処理は `finalize_package_inputs.py` に記録されています。ただし元スクリプトは文書更新・ZIP作成も含む当時専用処理なので、そのまま再実行しないでください。

1. コンパイル前にソース全ファイルを相対pathでソートし、各 `path` / `bytes` / `sha256` を `{"files":[...]}` として `SOURCE-SNAPSHOT.json` に保存。実DLLの時点では173ファイル。最終配布ソース176ファイルとの差分は記録済みで、今回のhelper修正はさらに別差分です。
2. 実ビルド後、build配下の `*.read.*.tlog` を読みます。`CompilerIdCXX` 配下を除き、BOMがFF FEならUTF-16、それ以外はUTF-8で復号。空行・`^`始まりを除いた実在ファイルを重複排除して、各ファイルのbytes/SHA256を採取。実記録は1087ファイル。
3. 絶対path付き一覧は `BUILD-TRACKED-INPUTS-LOCAL-ONLY.json` としてローカルに保持し、公開しません。公開用 `DEPENDENCIES.lock.json` にはPRODUCT_SOURCE/BUILD_GENERATED/OBS_33_RC2/OBS_33_CONFIG/QT_6_11_1/MSVC_14_44_35207/WINDOWS_SDK_10/CMAKE/WINDOWS_SYSTEMのルート名と相対path/SHA256へ変換した一覧を保存。未マッピングがあれば停止。
4. `BUILD-BINDING-PROOF.json` には実ビルド成功、DLL hash/size、実際のsource manifest hash、SDK、QA/diagnostic設定などの証拠を記録。配布ソースの差分がDLLのcompile inputを変更していないかは人が確認する必要があります。
5. `build-binding.json` は次の**5フィールドだけ**。JSONの最終bytesをハッシュして結びます。

```json
{"baseCommit":"<actual build baseline>","dllSha256":"<actual DLL SHA256>","dllSize":890880,"sourceManifestSha256":"<SOURCE-SNAPSHOT.json SHA256>","buildProofSha256":"<BUILD-BINDING-PROOF.json SHA256>"}
```

実DLLに対応するsource manifest SHA256は `5dc2e6fbd443d34a3060704048ea92de6a5cf90ee58c7adca594a98a9b488a19`、proof SHA256は `27272266ecc1614ed5ad1a30f30e5c820d20bb1ba7250f54aaf1c94a55ab018c`。baseCommitは `850b10e58bcfa994fb7835031e2bb68b1f866c78`。これらは既存ビルドの照合値です。新しいビルドでは自分の入力・出力から再計算してください。

wrapperはproofの**bytes hashのみ**を検証します。ビルド成功・DLL・baseline・compile inventoryの対応まで自動証明するものではありません。

抽出した処理を単独実行できる `packaging/windows/record-build-evidence.py` を同梱しました。コンパイル前manifestの全ファイルを読み戻し、成功記録とtlogからinventory・proof・bindingを作成します。今回、既存ビルドへ適用して173ソースファイルと1087入力を検証し、DLL hashも一致しました。ビルドは再実行していません。例:

```powershell
python packaging/windows/record-build-evidence.py --source-root <compile-source-root> --source-manifest <pre-build-SOURCE-SNAPSHOT.json> --build-dir <actual-build-dir> --build-result <actual-build-result.json> --dll <actual-DLL> --out <new-evidence-dir> --base-commit <actual-40-char-commit> --root OBS_33_RC2=<SDK-root> --root OBS_33_CONFIG=<generated-config-root> --root QT_6_11_1=<Qt-prefix> --root MSVC_14_44_35207=<MSVC-tools-root> --root WINDOWS_SDK_10=<Windows-Kits-10-root> --root CMAKE=<CMake-install-root> --root WINDOWS_SYSTEM=C:/Windows
```

SOURCE-SNAPSHOTの保存形式はUTF-8の `{"files":[{"path":"relative/path","bytes":123,"sha256":"..."}]}`。対象ファイルを相対pathでソートし、コンパイル**前**に固定してください。生成後のmanifest bytesを手編集するとbindingも再作成が必要です。

注意: 古い `FINAL-DLL-IDENTITY.json` のsourceManifestSha256には `cca56bc2...` が残っています。最終wrapperが実際に受け入れた `SOURCE-SNAPSHOT.json` のbytes hashとbuild bindingは上記 `5dc2e6fb...` です。旧記録は書き換えず保存し、今回の173ファイル全件一致と最終bindingで照合しました。

## ソースから製品パッケージ・helper・Setupへの連鎖

ソースZIPを先に確定し、`data-inputs.json` に製品dataのrelativePath/inputPath/sha256を作成します。元候補はlocale16+LICENSE+NOTICE=18 dataファイルです。SOURCE-AVAILABILITY追加などで変わる場合は一覧と個数も再生成してください。

```powershell
python packaging/windows/prepare_product_connection.py --repo <source-root> --dll <DLL> --dll-sha256 <hash> --dll-size <bytes> --build-binding-json <build-binding.json> --build-proof <BUILD-BINDING-PROOF.json> --source-zip <source.zip> --source-sha256 <hash> --source-size <bytes> --build-source-manifest <SOURCE-SNAPSHOT.json> --data-inputs-json <data-inputs.json> --output-dir <fresh-connection-dir>
```

これが固定製品archive/manifest/release-inputs/catalogを生成します。製品接続wrapperの既定はinstallation無効です。実機用の有効化は既存承認と対応するレビューを経た別候補に限ります。

```powershell
cmake -S <staged-installer-helper> -B <fresh-helper-build> -G "Visual Studio 17 2022" -A x64 -DSONICLATCH_REVIEWED_CATALOG=ON
cmake --build <fresh-helper-build> --config Release --target SonicLatch-helper --parallel 2
python packaging/windows/freeze_nsis_input.py --helper <helper.exe> --sha256 <actual-helper-sha256> --output-directory <fresh-nsis-input>
makensis /INPUTCHARSET UTF8 /OUTPUTCHARSET UTF8 /DSONICLATCH_PACKAGE_INPUT=<fresh-nsis-input>/product_package.generated.nsh packaging/windows/SonicLatch.nsi
```

staged helperには確定した `product_release.generated.hpp` を置きます。実helperは/MT、requireAdministratorでビルド。全て新しい出力先を使い、過去のSetup・原本ログを上書きしないでください。source ZIP/manifest/catalog/helper/Setupはソース・文書変更ごとに再固定が必要です。


## 自分のビルド前ソース一覧を記録する

source rootだけを列挙し、buildや出力directoryはsource rootの外に置いてください。以下の入力pathは利用者の実pathに置換します。生成したmanifest自身はsource rootへ置かず、ビルド前のbytesを保全します。

```python
from pathlib import Path
import hashlib, json
source = Path("<source-root>").resolve()
output = Path("<outside-source>/SOURCE-SNAPSHOT.json")
rows = []
for path in sorted(source.rglob("*"), key=lambda p: p.relative_to(source).as_posix()):
    if path.is_file() and ".git" not in path.relative_to(source).parts:
        data = path.read_bytes()
        rows.append({"path": path.relative_to(source).as_posix(),
                     "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
output.write_text(json.dumps({"files": rows}, indent=2) + "\n", encoding="utf-8")
```

このmanifestの行数が旧ビルドの173である必要はありません。追加された文書・helper試験を含む現在のソースは別snapshotです。成功した実configure/buildのexitをbuild-result JSONへ記録し、record-build-evidence.pyに渡します。成功値を未実行のまま作成しないでください。
