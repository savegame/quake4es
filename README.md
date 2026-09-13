# Quake 4 ES (Aurora OS port)

A Quake 4 source port for mobile and embedded Linux devices, with a focus on
Aurora OS. The engine renders through OpenGL ES and uses SDL2 for windowing
and input.

## Origins and credits

- This repository is a fork of **glKarin's idTech4A++ (Quake4Doom)** project:
  https://github.com/glKarin/com.n0n3m4.diii4a
- The underlying engine is **id Tech 4**, originally released by id Software
  under the GPL:
  https://github.com/id-Software/DOOM-3

## About this fork

Maintained by **sashikknox**.

News, builds and discussion (in Russian) are published on the Telegram
channel: https://t.me/auroraosgames

Differences from the upstream glKarin source:

- **Touch UI** - on-screen touch controls for playing without a keyboard or
  mouse (virtual joystick, buttons, gesture look).
- **FBO rendering** - the game renders into an intermediate framebuffer
  object and is then drawn with a fullscreen quad. This makes screen
  rotation, scaling and letterboxing possible on mobile devices.
- **ImGui launcher** - an in-process ImGui-based launcher for picking the
  game resources and configuring basic settings before the engine starts.
- **Gamepad support** - game controller support through SDL2 GameController,
  with a bundled mapping database.
- **Aurora OS integration** - build options, packaging and platform glue for
  Aurora OS (Sailfish-based mobile OS).

## Building

The project uses CMake. The main CMake project lives in `doom3/neo`.

Requirements:

- C++ compiler with C++11 support (GCC or Clang)
- CMake 3.x
- SDL2 (or use the bundled/local SDL via `USE_LOCAL_SDL`)
- OpenGL ES 2.0/3.x development libraries
- OpenAL Soft (optional, for sound)

Basic build (desktop Linux, SDL2, OpenGL ES):

```sh
mkdir build && cd build
cmake ../doom3/neo \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_Q4=ON \
    -DQUAKE4=ON \
    -DRAVEN=ON \
    -DBUILD_D3=OFF \
    -DBUILD_PREY=OFF \
    -DBUILD_ETQW=OFF \
    -DBUILD_D3_MOD=OFF \
    -DBUILD_Q4_MOD=OFF
make -j$(nproc)
```

Aurora OS build (touch UI, FBO rendering, ImGui launcher):

```sh
mkdir build && cd build
cmake ../doom3/neo \
    -DCMAKE_BUILD_TYPE=Release \
    -DAURORA=ON \
    -DAURORA_FBO=ON \
    -DAURORA_LAUNCHER=ON \
    -DAURORA_ORG=ru.sashikknox \
    -DAURORA_APP=quake4 \
    -DBUILD_Q4=ON \
    -DQUAKE4=ON \
    -DRAVEN=ON \
    -DBUILD_D3=OFF \
    -DBUILD_PREY=OFF \
    -DBUILD_ETQW=OFF
make -j$(nproc)
```

Useful CMake options:

| Option | Default | Description |
| ------ | ------- | ----------- |
| `AURORA` | OFF | Enable the Aurora OS port |
| `AURORA_FBO` | OFF | Render into an intermediate framebuffer and draw it with a quad (Aurora OS port) |
| `AURORA_LAUNCHER` | OFF | In-process ImGui launcher for picking the game resources (Aurora OS port) |
| `AURORA_ORG` | `ru.sashikknox` | Aurora OS organization name (reverse domain) |
| `AURORA_APP` | `quake4` | Aurora OS application name |
| `BUILD_Q4` | ON | Build Quake 4 support |
| `QUAKE4` | ON | Build the Quake 4 game code |
| `RAVEN` | ON | Build the Raven (Quake 4) core |
| `BUILD_D3` / `BUILD_PREY` / `BUILD_ETQW` | ON | Build other id Tech 4 games; turn OFF for a Quake 4 only build |
| `OPENGLES3` | ON | OpenGL ES 3 support |
| `OPENAL` | ON | OpenAL Soft sound backend |
| `MULTITHREAD` | ON | Multi-threaded renderer support |
| `IMGUI` | ON | ImGui support (required by the launcher) |
| `USE_LOCAL_SDL` | OFF | Use a bundled SDL2 instead of the system one (Linux) |
| `USE_SYSTEM_OGGVORBIS` / `USE_SYSTEM_CURL` / `USE_SYSTEM_FREETYPE` | OFF | Link against system libraries instead of the bundled ones |
| `ONATIVE` | OFF | Optimize for the host CPU |
| `SSE2NEON` | ON (ARM) | Use sse2neon SIMD on ARM |

## Game data

This source release does not contain any game data. You need a legal copy of
Quake 4: copy the `q4base` folder (with the `.pk4` files) from your
installation to the device and point the launcher or the `fs_basepath`
cvar at it. The game data is still covered by the original EULA.

## License

The source code is licensed under the **GNU General Public License v3**
(GPL-3.0). See the `LICENSE` file in this repository.

Note: the original Doom 3 / id Tech 4 GPL release by id Software is also
subject to additional terms from id Software; the Quake 4 game code by Raven
Software was released under the GPL as part of the Quake 4 SDK. Game data
files are not covered by this license and remain proprietary.
