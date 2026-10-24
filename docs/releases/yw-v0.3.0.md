# Azahar YW Compatibility Fork v0.3.0

> [!WARNING]
> **非公式の試験版（Prerelease）です。Azahar公式のLevel256対応ではありません。** 起動前に既存のセーブデータ・NAND・ユーザーデータをバックアップしてください。
> Level256のサービスおよび3GXプラグインは本リリースに含まれません。ゲームROM・任天堂システムデータ・LFCS・アカウント鍵も同梱していません。

## v0.3.0 の主な変更

### Level256署名付き3GXプラグイン対応（PR #29）

- 署名付きLevel256 v2.0.1 3GXを検証し、認証済みプラグインに限定して必要なLuma3DS互換機能を有効化。
- プラグインのCTRPF SELECTメニュー、既知のCustomBackdoorコールバック、メモリマッピングなどの互換性処理を改善。
- PLGUDS初期化・サービスリダイレクトを修正。ゲーム本体の `nwm::UDS` を認証済みプラグインの `plg:UDS` に接続し、ScanコマンドのIPC変換とゲスト側要求・応答処理を修正。
- `srv:` がKernel NamedPortで登録されることに合わせてSRV取得経路を修正。
- 別々のメモリ割り当てが偶然隣接してもVMAを誤って統合しないよう、メモリ所有元を確認する修正を追加。
- オプトイン・本人確認済みプラグイン・対象タイトル・所有PID・Capabilityごとのアクセス制限を維持。未検証のSVCと署名付きプラグイン向け `UnmapProcessMemoryEx (0xA1)` は利用不可。

**Linux実ゲームで確認済み（2026-10-24）：** 開発者のCachyOS環境で『妖怪ウォッチバスターズ 赤猫団 月兎組』（日本版、Title ID `000400000016C700`）と署名付きLevel256 v2.0.1 3GXを使用し、**ゲーム内のオンライン募集部屋の表示、および実際の部屋への入室に成功**しました。SELECTメニューの表示も確認済みです。

**未検証：** Windows/macOSでのLevel256実ゲーム動作、他のLevel256対象タイトル、長時間のオンラインプレイや全機能の互換性。ゲーム・プラグイン・サービスの将来の更新で動作が変わる可能性があります。ビルド成功はゲーム動作の保証ではありません。

### その他の改善

- `integration/yw` に蓄積された妖怪ウォッチ関連の互換性・セーブ日時処理・デバッグ改善を含みます。各タイトルでの動作保証ではありません。
- 署名検証、サービス認可、メモリマッピング、UDS IPCなどの回帰テストを追加しました。

## ダウンロード

| OS | 配布ファイル | 備考 |
|---|---|---|
| Windows | `azahar-yw-v0.3.0-windows-msys2-installer.exe` | インストーラー |
| Windows | `azahar-yw-v0.3.0-windows-msys2.zip` | ポータブル版 |
| macOS | `azahar-yw-v0.3.0-macos-universal.zip` | Apple Silicon / Intel両対応 |
| Linux | ソースコード | ローカルビルド |
| Android | 配布なし | APKの署名・配布は見送り |

各ファイルの検証には添付の `SHA256SUMS.txt` を使用してください。WindowsとmacOSの成果物のビルド・整合性検査が完了した場合のみ、GitHub ActionsがこのPrereleaseを公開します。

### 配布に関する注意

- **Windows:** ZIPは展開後に `azahar.exe` を起動します。インストーラー使用時も既存データのバックアップを推奨します。
- **macOS:** Universalアプリはアドホック署名です。Apple Developer ID署名とAppleの公証（notarization）は行っていません。Gatekeeper制限が生じる場合があります。
- **Linux:** ビルド済みバイナリはこのリリースに添付しません。ソースタグからビルドしてください。
- **Level256:** 本フォークでの動作確認であり、Azahar本家による公式サポートではありません。Level256プラグイン、アカウント情報、認証鍵はユーザー側で適法に用意・管理してください。

## Linuxソースビルド

```sh
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch --detach yw-v0.3.0
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ROOM_STANDALONE=OFF -DENABLE_DISCORD_RPC=ON
cmake --build build --parallel 6
./build/bin/Release/azahar
```

通常のAzaharの依存関係、Qt 6開発パッケージ、CMake、Ninjaなどが必要です。

## 不具合報告

OS・ゲーム名と版・実行手順・ビルドコミット・機密情報を削除した `azahar_log.txt` を添えてください。Level256のアカウント情報、鍵、LFCSは公開しないでください。

## English summary

**Azahar YW Compatibility Fork v0.3.0** is an unofficial **prerelease**. It adds verified signed Level256 v2.0.1 3GX compatibility, known Luma3DS callback/memory adapters, and authenticated in-game UDS service routing (PR #29). On Linux/CachyOS, the maintainer confirmed the plugin SELECT menu and successfully **listed and joined an online room** in *Yo-kai Watch Busters: Red Cat Corps — Moon Rabbit Crew* (Japanese version). This is **not official Azahar support** and does **not** establish working gameplay on Windows/macOS or other titles.

Release assets: Windows installer and portable ZIP, macOS universal ZIP (ad-hoc signed, not notarized), and SHA-256 checksums. Linux builds from source. Android APKs, proprietary game/system files, the Level256 plugin, and user credentials are not included.

Maintained by [AlexanderGG](https://github.com/AlexanderGG-0520), based on [Azahar](https://github.com/azahar-emu/azahar) and [utosa123's changes](https://github.com/utosa123/azahar). See `license.txt`.
