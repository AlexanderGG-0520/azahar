# Yo-kai Watch 2 Local Play Fix for Azahar

This fork of Azahar contains a compatibility workaround for local multiplayer in **Yo-kai Watch 2**.

The fix addresses a worker-polling behavior in Azahar's NWM/UDS emulation that can prevent Yo-kai Watch 2 local communication from progressing correctly.

## Latest Release

Yo-kai Watch 2 Local Play Fix v3 Beta 1

https://github.com/utosa123/azahar/releases/latest

## Available Builds

Currently available:

- Windows
- Android

## Compatibility

### Japanese Releases — Confirmed Working

Local multiplayer has been confirmed with:

- Yo-kai Watch 2 Ganso
- Yo-kai Watch 2 Honke
- Yo-kai Watch 2 Shinuchi

Confirmed features include:

- Battles
- Yo-kai trading
- Blasters
- Shin Blasters

Communication between different game versions and different Yo-kai Watch 2 editions has also been confirmed.

However, not every Japanese game version has been tested.

### International Releases — Experimental

Experimental support is included for retail releases from:

- North America
- Europe
- Australia
- Korea

Including:

- Bony Spirits
- Fleshy Souls
- Psychic Specters

International releases have not yet been runtime-tested.

They use the same communication workaround as the confirmed Japanese releases.

---

# Installation

## Windows

1. Download the Windows ZIP from the latest release.

2. Create a simple folder for the modified Azahar build.

Example:

`C:\Azahar-YW2-v3-Beta1`

3. Extract the ZIP into that folder.

Example:

`C:\Azahar-YW2-v3-Beta1\azahar.exe`

4. Launch `azahar.exe`.

### Installation Location

It is recommended to place the modified build in a simple location such as:

`C:\Azahar-YW2-v3-Beta1`

There have been cases where Azahar does not start correctly when placed under locations such as OneDrive.

## Android

1. Download the Android APK from the latest release.

2. Open the APK on your Android device.

3. If Android asks for permission to install apps from that source, allow it.

4. Install and launch Azahar.

### Android Signing Note

The Android APK is currently signed with an Android debug key.

Because the signing key may differ from the official Azahar Android build, Android may not allow this build to be installed directly over an existing Azahar installation.

If you need to uninstall an existing Azahar installation, back up your save data and Azahar data first.

---

# Using Existing Azahar Data

## Windows

If you already use Azahar, your existing environment may already be detected automatically.

If your previous Azahar installation used a portable `user` folder located next to `azahar.exe`, you can copy that folder to the modified build.

Example:

Old installation:

`C:\Azahar-old\user`

Modified build:

`C:\Azahar-YW2-v3-Beta1\user`

It is recommended to **copy** the folder instead of moving it and keep the original as a backup.

If your games and save data already appear normally in the modified build, you do not need to copy the `user` folder manually.

## Android

Before replacing or uninstalling an existing Azahar installation, back up important save data and other Azahar data.

Do not rely on uninstalling and reinstalling without a backup.

---

# Local Multiplayer Setup

For the most reliable results, devices participating in communication should use a compatible build containing this Yo-kai Watch 2 fix whenever possible.

Currently distributed modified builds:

- Windows
- Android

## Azahar Multiplayer Room

1. Launch Azahar on both devices.
2. Open the Multiplayer menu.
3. Join the same Multiplayer Room.
4. Launch Yo-kai Watch 2 on both devices.
5. Start the desired communication feature normally in-game.

Examples:

- Battles
- Yo-kai trading
- Blasters
- Shin Blasters

## Console Information

Make sure the two Azahar instances do not use identical multiplayer identity information.

Check:

- Username
- MAC address
- Console address

If both instances use identical values, change the values on one of the devices before joining the same Multiplayer Room.

---

# Remote Multiplayer

Azahar Multiplayer Rooms can also be used between devices on different networks.

A virtual LAN/VPN tool can be used to make remote devices reachable as if they were on the same local network.

Examples include:

- Radmin VPN for Windows-to-Windows connections

Remote communication may be more sensitive to latency, jitter, or packet loss than communication on the same local network.

For troubleshooting, first confirm that normal local-network communication works.

---

# Troubleshooting

If local communication does not work, check the following first:

- Both devices are connected to the same Azahar Multiplayer Room
- Both devices are using compatible Azahar builds
- Username, MAC address, and console address are not identical
- Cheats are disabled while testing
- Azahar is not installed in a problematic location such as OneDrive
- Firewall or VPN settings are not blocking communication

If you are testing over the Internet, also try the same setup on the same local network.

This helps determine whether the problem is caused by Yo-kai Watch 2 / Azahar or by the network connection.

---

# Technical Details

## Original Problem

Yo-kai Watch 2 creates an NWM worker when starting or joining local communication.

The relevant NWM operations are:

- `0x001D` — `BeginHostingNetwork`
- `0x001E` — `ConnectToNetwork`

After the worker completes its operation, the game checks the worker using:

`WaitSynchronization1(timeout=0)`

On Azahar, the worker thread may already be in the `Dead` state before Yo-kai Watch 2 performs its first `timeout=0` poll.

A `Dead` thread normally indicates that there is no longer any need to wait.

Because of this, Yo-kai Watch 2 can observe a different processing order from the one it expects and follow an incorrect state transition.

This can eventually cause local communication to stop or fail.

## Workaround

Earlier versions of the workaround depended on game-version-specific PC/LR addresses.

v3 no longer depends on those addresses.

Instead, the workaround identifies the NWM worker associated with the Yo-kai Watch 2 Host or Client operation.

It matches:

- Yo-kai Watch 2 title
- NWM Host or Client worker
- the exact worker thread
- `WaitSynchronization1`
- `timeout=0`
- the first matching poll
- completed / `Dead` worker state
- the same process

Only the first matching `timeout=0` poll for that registered worker is routed through Azahar's existing timeout handling.

No artificial communication delay is added.

The workaround does not modify the game's ResultCode and does not rely on game-version-specific PC/LR addresses.

---

# Source / Build Information

Current v3 Beta branch:

`yw2-local-play-v3-beta`

Original v3 Beta 1 source commit:

`2b9131b881a7600089117fd5df55e1de57d18a6b`

Commit subject:

`nwm: work around YW2 worker polling`

The workaround is implemented in Azahar's common core code and is therefore not limited to the Windows frontend.

---

# Development

Investigation, debugging, implementation, testing support, build preparation, and documentation for this project were carried out with extensive assistance from **ChatGPT and OpenAI Codex**.

Runtime testing and verification of game behavior were performed by the project maintainer.

---

# Compatibility Reports

Successful and unsuccessful compatibility reports are both welcome.

Please report results here:

https://github.com/utosa123/azahar/issues/1

---

# 日本語

# Azahar 妖怪ウォッチ2 ローカル通信修正

このforkには、**妖怪ウォッチ2のローカル通信をAzahar上で正常に動作させるための互換性修正**が含まれています。

AzaharのNWM / UDSエミュレーションにおけるworker確認処理の挙動を補正し、妖怪ウォッチ2のローカル通信が正常に進まなくなる問題を回避します。

## 最新版

妖怪ウォッチ2 ローカル通信修正 v3 Beta 1

https://github.com/utosa123/azahar/releases/latest

## 配布版

現在配布している修正版:

- Windows
- Android

## 対応状況

### 日本版 — 動作確認済み

以下でローカル通信の動作を確認しています。

- 妖怪ウォッチ2 元祖
- 妖怪ウォッチ2 本家
- 妖怪ウォッチ2 真打

確認済み機能:

- 通信対戦
- 妖怪交換
- バスターズ
- 真バスターズ

異なるゲームバージョン間や、異なる妖怪ウォッチ2作品間での通信成功も確認しています。

ただし、日本版についてもすべてのゲームバージョンを確認しているわけではありません。

### 海外版 — 試験対応

以下の地域の製品版を試験対応しています。

- 北米
- 欧州
- オーストラリア
- 韓国

対象作品:

- Bony Spirits
- Fleshy Souls
- Psychic Specters

海外版については現時点では動作未検証です。

日本版と同じ通信修正を使用しています。

---

# インストール

## Windows

1. 最新ReleaseからWindows版ZIPをダウンロードします。

2. Cドライブ直下などに修正版Azahar用のフォルダを作成します。

例:

`C:\Azahar-YW2-v3-Beta1`

3. ZIPを展開します。

例:

`C:\Azahar-YW2-v3-Beta1\azahar.exe`

4. `azahar.exe`を起動します。

### 保存場所について

以下のような単純な場所への配置をおすすめします。

`C:\Azahar-YW2-v3-Beta1`

OneDrive配下など、一部の場所ではAzaharが正常に起動しないケースがあります。

## Android

1. 最新ReleaseからAndroid版APKをダウンロードします。

2. Android端末でAPKを開きます。

3. 「この提供元からのアプリのインストール」を求められた場合は許可します。

4. Azaharをインストールして起動します。

### Android版の署名について

現在のAndroid APKはAndroid Debugキーで署名されています。

公式Azahar Android版とは署名が異なる可能性があるため、既存のAzaharの上からインストールできない場合があります。

既存版をアンインストールする場合は、必ず事前にセーブデータやAzaharのデータをバックアップしてください。

---

# 既存のAzahar環境を引き継ぐ場合

## Windows

すでにAzaharを使用している場合、既存の環境がそのまま認識されることがあります。

以前のAzaharで`azahar.exe`と同じ場所に`user`フォルダを置いていた場合は、そのフォルダを修正版へコピーできます。

例:

旧Azahar:

`C:\Azahar-old\user`

↓

今回の修正版:

`C:\Azahar-YW2-v3-Beta1\user`

移動ではなく**コピー**し、元の`user`フォルダはバックアップとして残しておくことをおすすめします。

ゲームやセーブデータがすでに修正版Azahar上で正常に表示されている場合は、手動でコピーする必要はありません。

## Android

既存のAzaharを削除・置き換える前に、重要なセーブデータやAzahar関連データをバックアップしてください。

---

# ローカル通信の使い方

安定した通信のため、可能な限り通信する端末の両方で本修正を含むAzaharを使用してください。

現在配布している修正版:

- Windows
- Android

## Multiplayer Room

1. 両方の端末でAzaharを起動します。
2. Multiplayerを開きます。
3. 同じMultiplayer Roomに参加します。
4. 両方で妖怪ウォッチ2を起動します。
5. ゲーム内から通常どおり通信機能を開始します。

例:

- 通信対戦
- 妖怪交換
- バスターズ
- 真バスターズ

## 通信情報について

2台のAzaharで通信に使用する情報が完全に同じになっていないか確認してください。

確認するもの:

- 名前
- MACアドレス
- コンソールアドレス

同じ場合は、片方のAzaharで変更してから同じMultiplayer Roomへ参加してください。

---

# 遠距離通信

異なるネットワークにいる端末同士でも、Azahar Multiplayer Roomを利用できます。

仮想LAN / VPNツールを使用すると、離れた端末同士を同じLANにいるような状態で接続できます。

例:

- Windows同士: Radmin VPN

遠距離通信は、同じLAN内での通信よりも遅延・ジッター・パケットロスの影響を受けやすくなります。

問題が発生した場合は、まず同じLAN内で正常に通信できるか確認することをおすすめします。

---

# うまく動かない場合

まず以下を確認してください。

- 両方の端末が同じAzahar Multiplayer Roomに参加している
- 対応したAzahar buildを使用している
- 名前・MACアドレス・コンソールアドレスが完全に同じではない
- 動作確認時はチートをOFFにしている
- Windows版をOneDriveなど問題が発生しやすい場所に置いていない
- FirewallやVPNが通信を遮断していない

インターネット越しで問題が発生している場合は、同じLAN内でも同じ症状が発生するか確認してください。

これにより、Azahar / 妖怪ウォッチ2側の問題なのか、ネットワーク側の問題なのかを切り分けやすくなります。

---

# 技術詳細

## 原因

妖怪ウォッチ2では、ローカル通信を開始または参加するときにNWM workerを作成します。

対象となる処理:

- `0x001D` — `BeginHostingNetwork`
- `0x001E` — `ConnectToNetwork`

workerの処理完了後、ゲームは

`WaitSynchronization1(timeout=0)`

を使用してworkerの状態を確認します。

Azaharでは、ゲームが最初の`timeout=0` pollを行う前にworker Threadがすでに`Dead`状態になっている場合があります。

`Dead`になったThreadは通常「もう待つ必要がない」と判定されます。

そのため、妖怪ウォッチ2が本来想定している処理順序とは異なる状態を観測し、誤った状態遷移へ進む場合があります。

これによってローカル通信が停止または失敗していました。

## 今回のworkaround

以前の修正版では、ゲームバージョンごとのPC/LRアドレスを使用していました。

v3ではPC/LRアドレスには依存しません。

代わりに、

- 妖怪ウォッチ2のTitle
- NWM Host / Client worker
- 実際に待機対象となっているworker Thread
- `WaitSynchronization1`
- `timeout=0`
- 最初の一致するpoll
- 完了済み / `Dead`状態
- 同一process

などを条件として対象workerを識別します。

一致したworkerに対する最初の`timeout=0` pollのみ、Azaharに既存のtimeout処理を通します。

人工的な通信遅延は追加していません。

ゲームのResultCodeを固定する処理もありません。

また、ゲームバージョンごとのPC/LRアドレスにも依存しません。

---

# Source / Build情報

v3 Beta branch:

`yw2-local-play-v3-beta`

v3 Beta 1の元ソースcommit:

`2b9131b881a7600089117fd5df55e1de57d18a6b`

Commit subject:

`nwm: work around YW2 worker polling`

今回の修正はAzaharの共通core側に実装されており、Windows専用の修正ではありません。

---

# 開発について

今回の問題調査、デバッグ、実装、テスト支援、ビルド準備、ドキュメント作成には、**ChatGPTおよびOpenAI Codexによる大規模な支援**を使用しています。

実際のゲーム上での動作確認・検証はプロジェクト管理者が行っています。

---

# 動作報告

正常に動作した場合・動作しなかった場合のどちらも歓迎します。

こちらへ報告してください。

https://github.com/utosa123/azahar/issues/1





![Azahar Emulator](https://azahar-emu.org/resources/images/logo/azahar-name-and-logo.svg)

![Current Release](https://img.shields.io/github/v/release/azahar-emu/azahar?label=Current%20Release)
![Current Prerelease](https://img.shields.io/github/v/release/azahar-emu/azahar?include_prereleases&label=Current%20Prerelease)

![GitHub Downloads](https://img.shields.io/github/downloads/azahar-emu/azahar/total?logo=github&label=GitHub%20Downloads)
![Google Play Downloads](https://playbadges.pavi2410.com/badge/downloads?id=io.github.lime3ds.android&pretty&label=Play%20Store%20Downloads)
![Flathub Downloads](https://img.shields.io/flathub/downloads/org.azahar_emu.Azahar?logo=flathub&label=Flathub%20Downloads)
![CI Build Status](https://github.com/azahar-emu/azahar/actions/workflows/build.yml/badge.svg)

<b>Azahar</b> is an open-source 3DS emulator project based on Citra.

It was created from the merging of PabloMK7's Citra fork and the Lime3DS project, both of which emerged shortly after Citra was taken down.

The goal of this project is to be the de-facto platform for future development.

# Installation

### Windows

Azahar is available as both an installer and a zip archive.

Download the latest release in your preferred format from the [Releases](https://github.com/azahar-emu/azahar/releases) page.

If you are unsure of whether you want to use MSVC or MSYS2, use MSYS2.

---

### MacOS

To download a build that will work on all Macs, you can download the `macos-universal` build on the [Releases](https://github.com/azahar-emu/azahar/releases) page.

Alternatively, if you wish to download a build specifically for your Mac, you can choose either:

- `macos-arm64` for Apple Silicon Macs
- `macos-x86_64` for Intel Macs

---

### Android

There are two variants of Azahar available on Android, those being the Vanilla and Google Play builds.

The Vanilla build is technically superior, as it uses an alternative method of file management which is faster, but isn't permitted on the Google Play store.

For most users, we currently recommended downloading Azahar on Android via the Google Play Store for ease of accessibility:

<a href='https://play.google.com/store/apps/details?id=io.github.lime3ds.android'><img width='180' alt='Get it on Google Play' src='https://raw.githubusercontent.com/pioug/google-play-badges/06ccd9252af1501613da2ca28eaffe31307a4e6d/svg/English.svg'/></a>

Alternatively, you can install the app using Obtainium, allowing you to use the Vanilla variant:
1. Download and install Obtainium from [here](https://github.com/ImranR98/Obtainium/releases) (use the file named `app-release.apk`)
2. Open Obtainium and click 'Add App'
3. Type `https://github.com/azahar-emu/azahar` into the 'App Source URL' section
4. Click 'Add'
5. Click 'Install', and select the preferred variant

If you wish, you can also simply install the latest APK from the [Releases](https://github.com/azahar-emu/azahar/releases) page.

Keep in mind that you will not recieve automatic updates when installing via the APK.

---

### Linux

The recommended format for using Azahar on Linux is the Flatpak available on Flathub:

<a href='https://flathub.org/apps/org.azahar_emu.Azahar'><img width='180' alt='Download on Flathub' src='https://dl.flathub.org/assets/badges/flathub-badge-en.png'/></a>

Azahar is also available as an AppImage on the [Releases](https://github.com/azahar-emu/azahar/releases) page.

There are two variants of the AppImage available, those being `azahar.AppImage` and `azahar-wayland.AppImage`.

If you are unsure of which variant to use, we recommend using the default `azahar.AppImage`. This is because of upstream issues in the Wayland ecosystem which may cause problems when running the emulator (e.g. [#1162](https://github.com/azahar-emu/azahar/issues/1162)).

Unless you explicitly require native Wayland support (e.g. you are running a system with no Xwayland), the non-Wayland variant is recommended.

The Flatpak build of Azahar also has native Wayland support disabled by default. If you require native Wayland support, it can be enabled using [Flatseal](https://flathub.org/en/apps/com.github.tchx84.Flatseal).

# Build instructions

Please refer this repository's [wiki](https://github.com/azahar-emu/azahar/wiki/Building-From-Source) for build instructions

# How can I contribute?

### Pull requests

If you want to implement a change and have the technical capability to do so, we would be happy to accept your contributions.

If you are contributing a new feature, it is highly suggested that you first make a Feature Request issue to discuss the addition before writing any code. This is to ensure that your time isn't wasted working on a feature which isn't deemed appropriate for the project.

After creating a pull request, please don't repeatedly merge `master` into your branch. A maintainer will update the branch for you if/ when it is appropriate to do so.

### Language translations

Additionally, we are accepting language translations on [Transifex](https://app.transifex.com/azahar/azahar). If you know a non-english language listed on our Transifex page, please feel free to contribute.

> [!NOTE]
> We are not currently accepting new languages for translation. Please do not request for new languages or language variants to be added.

### Compatibility reports

Even if you don't wish to contribute code or translations, you can help the project by reporting game compatibility data to our compatibility list.

To do so, simply read https://github.com/azahar-emu/compatibility-list/blob/master/CONTRIBUTING.md and follow the instructions.

Contributing compatibility data helps more accurately reflect the current capabilities of the emulator, so it would be highly appreciated if you could go through the reporting process after completing a game.

# Minimum requirements

Below are the minimum requirements to run Azahar:

### Desktop

```
Operating System: Windows 10 (64-bit), MacOS 13.4 (Ventura), or modern 64-bit Linux
CPU: x86-64/ARM64 CPU (Windows for ARM not supported).
     Single core performance higher than 1,800 on Passmark.
     SSE4.2 required on x86_64.
GPU: OpenGL 4.3 or Vulkan 1.1 support
Memory: 2GB of RAM. 4GB is recommended
```
### Android

```
Operating System: Android 10.0+ (64-bit)
CPU: Snapdragon 835 SoC or better
GPU: OpenGL ES 3.2 or Vulkan 1.1 support
Memory: 2GB of RAM. 4GB is recommended
```

# What's next?

We share public roadmaps for upcoming releases in the form of GitHub milestones.

You can find these at https://github.com/azahar-emu/azahar/milestones.

# Join the conversation

We have a community Discord server where you can chat about the project, keep up to date with the latest announcements, or coordinate emulator development.

Join at https://discord.gg/4ZjMpAp3M6
