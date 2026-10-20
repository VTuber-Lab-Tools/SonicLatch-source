# 第三者ソフトウェアの表記

## 同梱ソース: QCustomPlot 2.1.1

- `third_party/qcustomplot-2.1.1/qcustomplot.cpp`
- `third_party/qcustomplot-2.1.1/qcustomplot.h`
- Copyright (C) 2011–2022 Emanuel Eichhammer
- GNU General Public License version 3 or, at your option, any later version
- https://www.qcustomplot.com/

上記は同梱ファイルの原著作権・ライセンスヘッダーに基づきます。ヘッダーは変更せず保持しています。GPL v3 本文はルートの `LICENSE` に同梱しています。

## 外部ビルド・実行依存

Qt 6.11.1 と OBS Studio / libobs / frontend API を使用します。この候補ソースにはこれらの SDK や実行バイナリを含めません。実際に配布するバイナリ・ライブラリの構成と各ライセンス・必要な表記・対応ソース提供方法は、最終配布物を確定した段階で確認してください。本書は未確認の依存物について配布条件を満たしたと宣言するものではありません。

Windows SDK / MSVC とインストーラー作成ツールも外部依存です。開発用ツール一式や旧試験証拠はこのプラグイン用公開候補ソースから除外しています。インストーラー／helper の構成と表記は下記に記載します。

## 記録済み依存物の識別（配布範囲を増やさない）

`DEPENDENCIES.lock.json` の論理 root により外部依存を識別できます。

- OBS_32_2_2 / OBS_32_CONFIG: OBS Studio 32.2.2 の libobs / frontend API。SDK は外部に保持し、本ソースZIPには含めません。
- QT_6_11_1: Qt Core / Gui / Widgets 6.11.1 のヘッダー、CMake 設定、import libraries、DLL の記録。本ソースZIPには含めません。
- MSVC_14_44_35207 / WINDOWS_SDK_10 / CMAKE: 開発ツール・システムSDK。記録にあるからといって再配布対象にしません。

これらの各ファイルのライセンス本文はこのクラウド証拠一式には含まれていません。Qt の特定ライセンス経路や SDK の再配布許諾を推測して記載しません。現配布候補が追加同梱する第三者ソースは QCustomPlot の2ファイルのみです。将来外部依存バイナリを同梱する場合は、その実物の表記・許諾条件を確認し、本書を更新してください。

## Windows インストーラーに組み込む NSIS 関連部分

NSIS 3.13 を使用する現行定義 `packaging/windows/SonicLatch.nsi` は、生成 installer／uninstaller の実行部分、Modern UI 2 の画面・日本語リソース、`nsExec`、`System`、および zlib 展開部分を利用します。Modern UI 2 の Welcome 画面は `nsDialogs` を使用します。これらは生成物に組み込まれる部分であり、ビルド用 `makensis.exe` や NSIS ツール一式の再配布とは区別します。NSIS ツール一式は本 sourcepack に含めません。

- NSIS: Copyright (C) 1999–2026 Contributors。個別ファイルの著作権表示が優先されます。
- NSIS 公式文書は、例外が明記された部分を除くコード・プラグイン・ヘッダー・画像、および zlib 圧縮モジュールに zlib/libpng license を適用しています。
- 公式ライセンス本文: https://nsis.sourceforge.io/Docs/AppendixI.html
- nsExec の個別原著作権・ライセンス: https://github.com/NSIS-Dev/nsis/blob/v313/Contrib/nsExec/nsexec.c
- Modern UI 2 の Welcome 実装: https://github.com/NSIS-Dev/nsis/blob/v313/Contrib/Modern%20UI%202/Pages/Welcome.nsh

現行定義の `SetCompressor zlib` に基づきます。bzip2／LZMA はこの定義で選択していないため、使用中の圧縮モジュールとして記載しません。最終ビルド時の設定・マクロ展開で追加されるプラグインやリソースは、実際の入力と出力に照合してください。

## Windows helper のランタイム・OS 依存

`packaging/windows/installer-helper/CMakeLists.txt` は `CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded` を指定しています。MSVC ABI での通常ビルドでは `/MT` に相当し、Microsoft C/C++ ランタイムの静的リンクを指定します。ランタイム DLL の個別同梱がないことと、ランタイムのコードが helper に組み込まれないことは同義ではありません。

- 設定の意味: https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html
- Microsoft の `/MT` 説明: https://learn.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170
- 使用した Visual Studio／Build Tools に適用されるライセンス条件・再頒布条件を確認する際の公式資料: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution

helper が明示リンクする `bcrypt`、`advapi32`、`shell32`、`ole32` は Windows API への依存です。これらの OS DLL、Qt／OBS DLL、VC Redistributable インストーラーを別ファイルとしてコピーする指示は、現行の固定梱包定義にはありません。product payload は `SonicLatch.dll` と検証対象の `SonicLatch/data/` 内ファイルを helper に埋め込み、NSIS はその helper を取り込みます。これは最終 PE の全 import／静的リンク内容の検査に代わるものではありません。

## 最終配布物との照合が必要な事項

1. NSIS の実使用 3.13 配布物に含まれるライセンス文書と、最終コンパイルで取り込んだプラグイン・リソースの一覧。記録済み構築は NSIS 3.13、Modern UI 2、nsDialogs、nsExec、System、zlib を使用しています。最終 Setup の構築ログと展開結果をこの構成に照合します。
2. 最終 helper のコンパイル／リンク記録と PE import 一覧、および使用した MSVC／Build Tools の版・適用ライセンス条件。静的ランタイムの正確な組込み版・構成は現候補だけでは確定していません。
3. 最終 product manifest／埋込み catalog と対応する SonicLatch.dll の依存一覧。既定 catalog は空のまま失敗する構成であり、配布候補は別途生成した固定 catalog を使用します。現行の製品 payload は SonicLatch.dll と 42 data ファイルの計43件で、Qt/OBS SDK やその DLL は含めません。
4. 本書が実配布データに入ること。現行 validator は `.md` を許可しないため、同じ内容を `SonicLatch/data/THIRD_PARTY_NOTICES.txt` として明示的な data 入力に追加し、生成 manifest のサイズ・SHA-256 と実ファイルを照合してください。`LICENSE` 本文も改変せず `.txt` の data 入力として扱ってください。

以上はソース定義から確認できる構成の表記であり、未確認の最終バイナリやライセンス条件について配布適合を宣言するものではありません。
