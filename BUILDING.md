# ビルド手順（Windows 用候補）

## 必要なもの

- CMake 3.24 以降、C++20 対応 MSVC と Windows SDK
- Qt 6.11.1（CMake は EXACT 指定）、Core / Widgets
- 対象 OBS の libobs / frontend API ヘッダーと、対応する生成済み obsconfig.h
- 同梱の QCustomPlot 2.1.1 ソース

Qt、OBS、コンパイラー、Windows SDK のバイナリはこのソースに含みません。既存環境の依存ロックと取得元を確認してください。異なる SDK や OBS に置換して動作確認済みとみなさないでください。

## 製品ターゲット

ソースルートから、実際の検証済みパスを指定します。以下の山括弧部分は置換が必要です。

```powershell
cmake -S poc/normal-source-dock -B build/product -A x64 `
  -DCMAKE_PREFIX_PATH="<Qt 6.11.1 の prefix>" `
  -DOBS_HEADERS="<libobs headers>" `
  -DOBS_CONFIG_DIR="<obsconfig.h directory>" `
  -DSONICLATCH_BUILD_QA=OFF `
  -DSONICLATCH_BUILD_WHEEL_DIAGNOSTIC=OFF
cmake --build build/product --config Release --target pitatuner_normal_source_dock
```

出力名は SonicLatch.dll、内部ターゲット名は互換性のため維持します。MSVC runtime は MultiThreadedDLL です。配置時は `data/locale` をプラグインのデータ領域へ同梱します。自動インストール・アンインストール実装は別成果物であり、このソース一式のビルド完了だけでは配布用インストーラーは完成しません。

## 開発用試験

通常製品ビルドでは QA と wheel diagnostic は OFF です。`qa/` と `tests/` は開発用であり、製品 DLL のインストール対象ではありません。QA の分離条件は `qa/development-targets.cmake` を確認してください。旧仕様の試験は対象を選んで実行します。

## 記録済みビルド環境と依存ロック

今回の候補は公式OBS Studio 33.0.0-rc2ソース（tag commit f1c0a116441bdb7a1de7896f810009ea487b8508）のlibobs/frontend APIとRC2生成obsconfig.h、Qt6.11.1、MSVC14.44.35207、Windows SDK10.0.26100.0でビルドしています。QA・wheel diagnosticはOFFです。OBS32以前は対象外です。

DEPENDENCIES.lock.jsonは今回のMSBuild実読込入力を再ハッシュし、絶対パスを論理rootへ置換した一覧です。PRODUCT_SOURCEはコンパイル時のソース、BUILD_GENERATEDは生成物の記録で、SDKやランタイムの再配布指定ではありません。OBS_33_CONFIGのobsconfig.hはOBS_RELEASE_CANDIDATE=2、OBS_BETA=0です。同じ公式ソースの生成設定を使い、空ファイルで置換しないでください。Qt/OBS SDKや実行時DLLはソースZIPに含みません。

最終配布文書と依存記録の更新はDLLのコンパイル後にパッケージ用コピーへ適用しています。実コンパイル入力173ファイルと配布ソースの差は文書・依存記録・installer接続scriptに限定し、製品C++ソースは同一です。

## インストーラーソース

`packaging/windows/` に helper、NSIS、package/catalog生成、入力固定のソースを同梱しています。Python 3.10 以降と NSIS 3.13 は外部依存です。Python モジュールの依存は `integrations/windows-candidates-20261019/installer-next-implementation/` にあります。

製品 DLL と対応ソース ZIP の実際の hash・size、成功 build の入力 inventory と証拠を確定してから catalog を生成します。未確定値や旧成果物の値で置換しません。生成 catalog を helper に接続し、helper → freeze → NSIS の順で構築します。catalog を更新した後は helper も再構築してください。`SONICLATCH_REVIEWED_CATALOG` は生成済み `packaging/windows/installer-helper/product_release.generated.hpp` を必要とします。未生成のまま有効にしません。

`product_package.py` / `build_release_package.py` / `generate_release_catalog.py` が製品 package の生成経路です。`generate_product_catalog.py` は既存 candidate 用経路であり、製品 package と混同しないでください。生成 payload/header、build tree、実行ファイル、個人パスを含む設定、private 履歴はソースに含めません。

## 補正計算の技術補足

有効な推定遅延を D、現在の同期オフセットを S とすると、自動調整は絶対値 −D を設定し、残差表示は D + S です。± は現在の S を基準に操作します。

## 外部取得元・入力schema・生成順の補足

[BUILD-INPUTS.md](BUILD-INPUTS.md) を参照してください。公式OBS/Qt取得元、実DLLのconfigure/build引数、同一hashのobsconfig再生成、inventory/binding生成手順を記載しています。既存ビルド証拠の再検証であり、新規環境の全再ビルドや修正版インストーラー導入成功を意味しません。
