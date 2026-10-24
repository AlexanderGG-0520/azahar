# Azahar YW Compatibility Fork v0.3.1

> 本リリースはAzahar公式版ではなく、非公式の試験版です。起動前にセーブデータ・NAND・ユーザーデータのバックアップを推奨します。
>
> Level256の3GX・サービス本体・ゲームROM・LFCS・アカウント鍵などは配布物に含みません。

## 1. v0.3.1の概要

Level256 Network v2.0.1との互換性を改善する更新です。プラグインの乱数取得IPCと、FS_USERのメディア種別取得コマンドを実装しました。

## 2. Level256の乱数取得機能

`plg:ldr` の `GetRandomBytes (0x2561)` を実装しました。要求サイズとWritable MappedBufferを検証し、署名検証済みLevel256プラグインと対応プロセスに限定して、暗号学的乱数を返します。乱数生成に失敗した場合は成功を返さず、出力バッファをゼロ化します。既存のLFCS取得 `0x2560` は維持しています。

**注意:** この変更は乱数IPCの実装です。Level256の新規アカウント自動登録が成功することを確認したものではありません。

## 3. メディア種別取得機能

未実装だった `FS_USER::GetMediaType (0x0868)` を追加しました。呼び出し元プロセスの登録情報からメディア種別を返します。

- NAND: `0`
- SDMC: `1`
- GameCard: `2`

プロセス情報がない場合はエラーを返し、未初期化のメディア種別を返さないようにしています。Level256専用の固定値や補正は追加していません。

## 4. 赤猫団のバックアップ機能改善

『妖怪ウォッチバスターズ 赤猫団 月兎組』（Title ID `000400000016C700`）では、修正前にメディア種別の誤りと `UnsupportedOpenFlags` が記録されていました。`0x0868` の実装後、提供されたCachyOS上の確認結果では、SDMCのメディア種別が取得され、以前のエラーが消え、Level256画面にバックアップ作成成功が表示されました。

これはバックアップ作成の確認であり、バックアップからの完全な復元を確認したものではありません。

## 5. 技術的な変更

- Level256 `GetRandomBytes (0x2561)` のIPC ABI、サイズ・権限検証、呼び出し元認可を追加。
- `FS_USER::GetMediaType (0x0868)` を要求元プロセスの登録済み `ProgramInfo` に基づいて実装。
- 上記コマンドのIPC応答、エラー処理、複数プロセス分離を確認するテストを追加。
- 赤猫団・白犬隊向けの詳細なNCCH診断トレースはリリース差分から除外。

## 6. テスト結果

- `FS_USER::GetMediaType`: 26 assertions 成功。
- Level256 `0x2560` / `0x2561` 関連: 10 test cases、74 assertions 成功。
- 全CTest: 179件中174件成功、5件skip、失敗0件。
- CachyOS Linux Releaseビルド、clang-format、`git diff --check` 成功。

## 7. 動作確認済み環境

- **CachyOS Linux:** Linux Releaseビルド成功。
- **赤猫団 月兎組:** Level256によるバックアップ作成成功を確認。`0x0868` によるメディア種別取得の改善もログで確認。
- 関連する自動テスト成功。

## 8. 未検証・既知の制限

- 白犬隊のバックアップ実動作。
- Level256の新規アカウント自動登録。
- Windows/macOS上でのLevel256実ゲーム動作。Windows/macOSのCIビルド成功は実ゲーム動作を保証しません。
- バックアップからの完全復元。
- すべてのLevel256機能および対象タイトルでの互換性。
- 長時間プレイやオンラインサービスを含むすべての動作。

## 9. ダウンロードと注意事項

| OS | ファイル | 備考 |
|---|---|---|
| Windows | `azahar-yw-v0.3.1-windows-msys2-installer.exe` | インストーラー |
| Windows | `azahar-yw-v0.3.1-windows-msys2.zip` | ポータブル版 |
| macOS | `azahar-yw-v0.3.1-macos-universal.zip` | Apple Silicon / Intel Universal版 |
| Linux | ソースコード | リリース添付のバイナリなし。ローカルビルドをご利用ください。 |
| Android | 配布なし | APKのビルド・署名・配布は行いません。 |

各ダウンロードのSHA-256は `SHA256SUMS.txt` で確認してください。macOSアプリの署名・公証の状態は公開後にアセット情報を確認してください。

### Linuxソースビルド

```sh
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch --detach yw-v0.3.1
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ROOM_STANDALONE=OFF -DENABLE_DISCORD_RPC=ON
cmake --build build --parallel 6
./build/bin/Release/azahar
```

通常のAzaharの依存関係、Qt 6開発パッケージ、CMake、Ninjaなどが必要です。Level256のプラグインや認証情報は各自で適切に管理してください。
