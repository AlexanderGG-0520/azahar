# Azahar YW Compatibility Fork

Unofficial Azahar fork focused on **Yo-kai Watch 2 / Yo-kai Watch 3 compatibility**.

This build is based on the YW2 local-play work from `utosa123/azahar` and includes additional fixes maintained in this fork.

## Included fixes

- YW2 local-play compatibility fixes from utosa123's `yw2-local-play-v3-beta`
- YW2 local multiplayer / trading / battle related compatibility work
- Qt system-camera lifecycle fixes
- Shared system-camera reference counting
- Linux fix for the in-game camera showing a solid green image
  - Confirmed working with **Yo-kai Watch 2 Fushigi Lens**

> This is an unofficial compatibility-focused fork. It is not an official Azahar release.

## Windows

Two Windows packages are provided in the Release assets.

### Installer — recommended

Download:

`azahar-yw-v0.1.0-windows-msys2-installer.exe`

Run the installer and launch Azahar normally.

### Portable ZIP

Download:

`azahar-yw-v0.1.0-windows-msys2.zip`

Extract the ZIP to a folder and run `azahar.exe`.

The portable ZIP does not require installation.

## Linux

Linux binaries are not distributed in this release. Build the fork from source.

### 1. Clone

```bash
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar
git switch integration/yw
git submodule update --init --recursive
```

If you already cloned the repository:

```bash
git fetch origin
git switch integration/yw
git pull --ff-only
git submodule sync --recursive
git submodule update --init --recursive
```

### 2. Configure

Azahar requires CMake 3.25 or newer, Ninja, a C/C++ compiler, Qt 6 development packages, and the normal Azahar Linux build dependencies.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_ROOM_STANDALONE=OFF \
  -DENABLE_DISCORD_RPC=ON
```

### 3. Build

```bash
cmake --build build -j "$(nproc)"
```

### 4. Run

```bash
./build/bin/Release/azahar
```

If CMake reports missing vendored libraries such as Boost, Catch2, dynarmic, fmt, SDL, or compatibility_list, run:

```bash
git submodule sync --recursive
git submodule update --init --recursive
rm -rf build
```

and configure again.

## Camera configuration on Linux

For a real webcam:

1. Open **Emulation → Configure → System → Camera**.
2. Select **System Camera (qt)**.
3. Select your webcam.
4. Apply the settings.
5. Start the game and open its camera feature.

The Linux green-camera issue that affected the in-game feed while the settings Preview worked is fixed in this fork.

## YW2 local play

The fork preserves utosa123's YW2 local-play v3 workaround. It is intentionally kept separate from unrelated game-speed modifications such as the experimental 120 FPS branch.

## Source

The release is built from the `integration/yw` line of development:

https://github.com/AlexanderGG-0520/azahar/tree/integration/yw

Upstream Azahar:

https://github.com/azahar-emu/azahar

YW2 local-play work:

https://github.com/utosa123/azahar
