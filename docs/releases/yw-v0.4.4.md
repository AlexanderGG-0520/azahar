# Azahar YW Compatibility Fork v0.4.4

## 変更内容

Discord Rich Presenceを有効にしてHOMEメニューを起動すると停止する問題を修正しました。Discordの実装が異なるビルド設定で二重にコンパイルされ、実行中タイトルの情報を取得する際にロック待ちになることが原因でした。Discord実装を正しいビルド設定で一度だけコンパイルします。

v0.4.3のAndroid Level256設定、マウス入力、HOMEメニュー経由のDiscordタイトル更新も含まれています。本ソフトウェアはAzahar公式版ではない独立した非公式フォークです。

## 配布

- Windows MSYS2インストーラーとポータブルZIP
- macOS Universal ZIP
- 固定リリース鍵で署名したAndroid Vanilla APK
- 各配布ファイルのSHA-256チェックサム

AndroidアプリIDは `io.github.alexandergg0520.azaharyw`、versionNameは `yw-v0.4.4-vanilla`、versionCodeは `4004000` です。Linuxの署名付きpacmanリポジトリは別途更新します。

## 検証範囲

LinuxのDiscord有効ビルドと既存テストを実行し、192件成功、音声・DSP関連5件をスキップしました。問題の環境から複製したHOMEデータではDiscordを有効にしたまま停止箇所を通過し、HOMEサービスの初期化が進むことを確認しました。画面上のHOME操作とHOMEからのゲーム起動は未検証です。

Windows・macOSの実機動作、Android端末での表示や動作は未検証です。ゲーム、Level256、オンラインサービスとの互換性をビルド成功だけで保証するものではありません。
