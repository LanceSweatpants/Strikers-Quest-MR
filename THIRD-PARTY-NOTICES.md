# Third-Party Notices

This app (the Meta Quest APK built from `smstrikers-port/`) contains software and assets made by
third parties. Most of it is compiled and statically linked into the native library
`lib/arm64-v8a/libmain.so`; some of it ships as separate shared libraries, Java code or asset files
inside the APK. This file lists every third-party component that ends up in the APK, with its
license, copyright lines and upstream location, followed by the full text of each license that
requires its text or notice to accompany binary redistribution.

Notes:

- **Game and porting code.** The decompiled game code and the porting code are not covered by this
  file. They have their own license files in `smstrikers-port/`: `LICENSE-CC0.txt`,
  `LICENSE-BSD.TXT`, `LICENSE-LGPL-2.1.txt` and `LICENSE-GPL-2.0.txt` (see also the License section
  of `README.md`).
- **RVZ/WIA disc reader.** The RVZ and WIA disc-image reader in `smstrikers-port/src/platform/disc.c`
  follows the code of the Dolphin emulator (https://github.com/dolphin-emu/dolphin) and is therefore
  under GPL-2.0-or-later. The license text is `smstrikers-port/LICENSE-GPL-2.0.txt`.
- **Button prompts.** The button prompt images in the APK (`assets/input-prompts/kenney/`) are
  Kenney's "Input Prompts" pack, released under CC0 1.0 (public domain dedication). Its license
  file ships next to them in the APK as `assets/input-prompts/LICENSE-Kenney.txt`.
- **License choices.** Where a component offers a choice of licenses, this app uses: ODE under its
  BSD-style license (not the LGPL), zstd under its BSD license (not the GPLv2), FreeType under the
  FreeType License (FTL), the OpenXR loader under Apache-2.0 (not MIT), HIDAPI under its original
  license, jsoncpp under the MIT License, and stb / miniz under their public-domain options.

---

## Contents of the APK

| APK path | What it is | Third-party content |
| --- | --- | --- |
| `lib/arm64-v8a/libmain.so` | The game, the port and its engine | Aurora (incl. magic_enum, libogc-derived code), MusyX, zstd, bzip2, LZMA SDK, ODE, SDL 3, Dawn (incl. Tint, Abseil, SPIRV-Tools, SPIR-V grammar, webgpu.h), Abseil, {fmt}, xxHash, libpng, FreeType, Dear ImGui (incl. ProggyClean font, stb), SQLite, Luckiest Guy font, Opus, rcheevos, LLVM runtime pieces |
| `lib/arm64-v8a/libc++_shared.so` | C++ standard library | LLVM libc++ / libc++abi (from Android NDK r29) |
| `lib/arm64-v8a/libopenxr_loader.so` | OpenXR loader | Khronos OpenXR loader 1.1.63 (incl. jsoncpp, jnipp, android-jni-wrappers, LLVM libc++) |
| `lib/arm64-v8a/libmain_hook.so`, `libfile_redirect_hook.so`, `libgsl_alloc_hook.so`, `libhook_impl.so` | GPU driver loading hooks | libadrenotools (incl. liblinkernsbypass) |
| `classes*.dex` | Java code | SDL 3 Java glue (`org.libsdl.app`), Aurora Java glue (`dev.encounter.aurora`) |
| `assets/input-prompts/` | Button prompt images | Kenney Input Prompts (CC0) |

## Component summary

| Component | Version | License | Upstream |
| --- | --- | --- | --- |
| Aurora | vendored in `smstrikers-port/extern/aurora` (with local changes) | MIT | https://github.com/encounter/aurora |
| magic_enum (bundled in Aurora) | 0.7.2 | MIT | https://github.com/Neargye/magic_enum |
| libogc-derived code (bundled in Aurora) | - | zlib | https://github.com/devkitPro/libogc |
| MusyX | vendored in `smstrikers-port/extern/musyx` | MIT | https://github.com/AxioDL/musyx |
| Zstandard (zstd), decompression only | 1.5.8 | BSD-3-Clause (chosen from BSD / GPLv2) | https://github.com/facebook/zstd |
| bzip2 / libbzip2 | 1.0.8 | bzip2 BSD-style license | https://sourceware.org/bzip2/ |
| LZMA SDK (LzmaDec, Lzma2Dec) | 19.00 (decoder sources, as vendored by mGBA) | Public domain | https://www.7-zip.org/sdk.html |
| Open Dynamics Engine (ODE) | 0.5, with reconstructed changes | ODE BSD-style license (chosen from BSD / LGPL-2.1) | https://www.ode.org |
| SDL (native library and Java glue) | 3.4.10 | zlib | https://github.com/libsdl-org/SDL |
| yuv2rgb (bundled in SDL) | - | BSD-3-Clause | https://github.com/descampsa/yuv2rgb |
| HIDAPI (bundled in SDL) | - | original HIDAPI license (chosen from GPLv3 / BSD / original) | https://github.com/libusb/hidapi |
| Dawn and Tint (prebuilt static library) | package v20260807.225922 (source ref 1155e0ed531126f33a1279afa029349651ca1c93) | BSD-3-Clause | https://dawn.googlesource.com/dawn (package: https://github.com/encounter/dawn) |
| webgpu.h (bundled in Dawn) | - | BSD-3-Clause | https://github.com/webgpu-native/webgpu-headers |
| SPIRV-Tools (bundled in Dawn) | as pinned by Dawn | Apache-2.0 | https://github.com/KhronosGroup/SPIRV-Tools |
| SPIR-V Headers / grammar (bundled in Dawn) | as pinned by Dawn | Khronos MIT-style license | https://github.com/KhronosGroup/SPIRV-Headers |
| Abseil (standalone copy, and Dawn's internal copy) | 20240722.0 (standalone) | Apache-2.0 | https://github.com/abseil/abseil-cpp |
| {fmt} | 12.1.0 | MIT (with optional object-code exception) | https://github.com/fmtlib/fmt |
| xxHash | 0.8.3 | BSD-2-Clause | https://github.com/Cyan4973/xxHash |
| libpng | 1.6.58 | PNG Reference Library License version 2 | https://github.com/pnggroup/libpng |
| FreeType | 2.14.3 | FreeType License (FTL) (chosen from FTL / GPLv2); BDF and PCF drivers under MIT-style licenses | https://freetype.org |
| Dear ImGui (docking branch) | 1.91.9b | MIT | https://github.com/ocornut/imgui |
| ProggyClean font (embedded in Dear ImGui) | - | MIT | https://www.proggyfonts.net |
| SQLite | 3.51.3 | Public domain | https://sqlite.org |
| libadrenotools | commit 8fae8ce254dfc1344527e05301e43f37dea2df80 | BSD-2-Clause | https://github.com/bylaws/libadrenotools |
| liblinkernsbypass (bundled in libadrenotools) | - | BSD-2-Clause | https://github.com/bylaws/liblinkernsbypass |
| Khronos OpenXR loader for Android | 1.1.63 | Apache-2.0 (chosen from Apache-2.0 / MIT) | https://github.com/KhronosGroup/OpenXR-SDK |
| jsoncpp (bundled in the OpenXR loader) | - | MIT (chosen from public domain / MIT) | https://github.com/open-source-parsers/jsoncpp |
| jnipp (bundled in the OpenXR loader) | - | MIT | https://github.com/mitchdowd/jnipp |
| android-jni-wrappers (bundled in the OpenXR loader) | - | BSL-1.0 | https://github.com/KhronosGroup/OpenXR-SDK (`src/external/android-jni-wrappers`) |
| LLVM libc++, libc++abi, libunwind, compiler-rt builtins | Android NDK r29 (29.0.14206865) | Apache-2.0 WITH LLVM-exception (legacy parts: NCSA / MIT) | https://github.com/llvm/llvm-project |
| Kenney Input Prompts | 1.5A | CC0 1.0 | https://www.kenney.nl |
| Luckiest Guy font | Regular (Google Fonts) | Apache-2.0 | https://fonts.google.com/specimen/Luckiest+Guy |
| Opus audio codec (libopus) | 1.5.2 | BSD-3-Clause (Opus variant) | https://opus-codec.org |
| rcheevos | 12.5.0 | MIT | https://github.com/RetroAchievements/rcheevos |
| LAPACK routines (Java port, guest converter) | 3.2.2, as NumPy's lapack_lite carries them | BSD-3-Clause (LAPACK modified BSD) | https://www.netlib.org/lapack/ |
| NumPy lapack_lite (the C those routines were ported from) | - | BSD-3-Clause | https://github.com/numpy/numpy |

---

## Component details

### Aurora

- License: MIT (text: [MIT License](#mit-license)).
- Copyright (c) 2022 Luke Street
- Upstream: https://github.com/encounter/aurora. The copy in `smstrikers-port/extern/aurora` carries
  local modifications (see `smstrikers-port/tools/aurora-local-changes.patch`). Aurora's Java glue
  (`dev.encounter.aurora.AuroraSurface`) is in the APK's dex files under the same license.
- Aurora bundles:
  - **magic_enum** 0.7.2 (`include/magic_enum.hpp`), MIT. Copyright (c) 2019 - 2021 Daniil Goncharov.
    Upstream: https://github.com/Neargye/magic_enum
  - **libogc-derived code** in `lib/card/SRAM.hpp` (marked "Modified code taken from libogc"), zlib
    license (text: [zlib License](#zlib-license)). Copyright (C) 2004 Michael Wiedenbauer (shagkur),
    Dave Murphy (WinterMute).

### MusyX

- License: MIT (text: [MIT License](#mit-license)).
- Copyright (c) 2023 Axiomatic Data Laboratories
- Upstream: https://github.com/AxioDL/musyx (vendored in `smstrikers-port/extern/musyx`).

### Zstandard (zstd)

- License: BSD-3-Clause. zstd is dual-licensed (BSD or GPLv2); this app uses it under the BSD
  license (text: [zstd BSD License](#zstd-bsd-license)).
- Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.
- Copyright (c) Yann Collet - Meta Platforms, Inc
- Copyright (c) 2016 Tino Reichardt
- Upstream: https://github.com/facebook/zstd. Version 1.5.8, decompression sources only, vendored in
  `smstrikers-port/extern/zstd`.

### bzip2 / libbzip2

- License: bzip2's BSD-style license (text: [bzip2 License](#bzip2-license)).
- Copyright (C) 1996-2019 Julian R Seward.
- Upstream: https://sourceware.org/bzip2/. Version 1.0.8, the library sources only, vendored
  unmodified in `smstrikers-port/extern/bzip2` (from Dolphin's Externals). Used to read .rvz/.wia
  disc images made with bzip2 compression.

### Open Dynamics Engine (ODE)

- License: ODE is offered under either the GNU LGPL 2.1 or a BSD-style license. This app uses the
  **BSD-style license** (text: [ODE BSD-style License](#ode-bsd-style-license); also in
  `smstrikers-port/LICENSE-BSD.TXT` and `smstrikers-port/src/ode/LICENSE`).
- Copyright (c) 2001-2004, Russell L. Smith.
- Upstream: https://www.ode.org (version 0.5; the copy in `smstrikers-port/src/ode` and
  `smstrikers-port/include/ode` includes reconstructed changes and game-specific additions, as
  described in `smstrikers-port/src/ode/LICENSE`).

### SDL 3 (native library and Java glue)

- License: zlib (text: [zlib License](#zlib-license)).
- Version 3.4.10, statically linked into `libmain.so`; its Java glue (`org.libsdl.app.*`) is in the
  APK's dex files under the same license.
- Copyright lines (all under the zlib license):
  - Copyright (C) 1997-2026 Sam Lantinga
  - Copyright (C) Valve Corporation (including Copyright (C) 2022 Valve Corporation)
  - Copyright 2023 Collabora Ltd.
  - Copyright (C) 2023 Max Maisel
  - Copyright (C) 2025 Simon Wood
  - Copyright (C) 2025 Michal Malý
  - Copyright (C) 2025 Katharine Chui
  - Copyright (C) 2025 Bernat Arlandis
  - Copyright (C) 2025 Mitchell Cairns
  - Copyright (C) 2025 Zuiki Inc.
  - Copyright (C) 2001-2011 Andreas Schiffler (`SDL_rotate.c`)
  - Copyright (c) 1998-2021 Gareth McCaughan (`SDL_qsort.c`, relicensed under zlib for SDL)
- Upstream: https://github.com/libsdl-org/SDL
- SDL bundles, with their own licenses:
  - **yuv2rgb** (`src/video/yuv2rgb`), BSD-3-Clause. Copyright (c) 2016, Adrien Descamps.
    Text: [yuv2rgb BSD License](#yuv2rgb-bsd-license).
  - **HIDAPI** (`src/hidapi`; on Android the device backend is SDL's own zlib-licensed code), used
    under the original HIDAPI license. Copyright 2009, Alan Ott, Signal 11 Software.
    Text: [HIDAPI original license](#hidapi-original-license).
  - **stb_image** (public domain option) and **miniz** (public domain / Unlicense), plus small
    public-domain helpers (CRC, murmur3 hash, PDCLib-derived code); see
    [Public domain and CC0 components](#public-domain-and-cc0-components).

### Dawn and Tint (WebGPU implementation)

- License: BSD-3-Clause (text: [BSD 3-Clause License](#bsd-3-clause-license)).
- Copyright 2017-2025 The Dawn & Tint Authors (years vary per file)
- Upstream: https://dawn.googlesource.com/dawn. The APK uses the prebuilt static library
  `libwebgpu_dawn.a` from https://github.com/encounter/dawn (release v20260807.225922, source ref
  1155e0ed531126f33a1279afa029349651ca1c93), with the Null, OpenGL and Vulkan backends.
- The prebuilt package ships no license or notices file. The following third-party code was found
  inside `libwebgpu_dawn.a` (object list) and in `libmain.so` (symbol table):
  - **webgpu.h** header, BSD-3-Clause. Copyright (c) 2019, "WebGPU native" developers.
    Text: [BSD 3-Clause License](#bsd-3-clause-license).
  - **Abseil**, Apache-2.0 (Dawn's own copy). See [Abseil](#abseil).
  - **SPIRV-Tools**, Apache-2.0. Copyright The Khronos Group Inc., Google Inc. / Google LLC and other
    SPIRV-Tools contributors (per-file notices upstream). Upstream:
    https://github.com/KhronosGroup/SPIRV-Tools. Text: [Apache License 2.0](#apache-license-20).
  - **SPIR-V Headers / grammar tables** (compiled into SPIRV-Tools), Khronos MIT-style license.
    Copyright (c) 2014-2024 The Khronos Group Inc. Upstream:
    https://github.com/KhronosGroup/SPIRV-Headers. Text:
    [Khronos SPIR-V License](#khronos-spir-v-license).

### Abseil

- License: Apache-2.0 (text: [Apache License 2.0](#apache-license-20)). No NOTICE file.
- Copyright 2017-2024 The Abseil Authors (years vary per file)
- Copyright 2016 Google Inc. All Rights Reserved.
- Upstream: https://github.com/abseil/abseil-cpp. Two copies are linked: release 20240722.0
  (built from source for Aurora) and the copy compiled into Dawn's prebuilt library.

### {fmt}

- License: MIT, with an optional exception for embedded object code (text:
  [MIT License](#mit-license) and the exception below it).
- Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
- Upstream: https://github.com/fmtlib/fmt (12.1.0).

### xxHash

- License: BSD-2-Clause (text: [BSD 2-Clause License](#bsd-2-clause-license)). Only the library
  (`xxhash.c`) is used; the GPL-licensed `xxhsum` tool is not built.
- Copyright (c) 2012-2021 Yann Collet
- Upstream: https://github.com/Cyan4973/xxHash (0.8.3).

### libpng

- License: PNG Reference Library License version 2 (text: [libpng License](#libpng-license)).
- Copyright (c) 1995-2026 The PNG Reference Library Authors.
- Copyright (c) 2018-2026 Cosmin Truta.
- Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
- Copyright (c) 1996-1997 Andreas Dilger.
- Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.
- Upstream: https://github.com/pnggroup/libpng (1.6.58).

### FreeType

- License: FreeType is offered under the FreeType License (FTL) or the GNU GPL v2. This app uses it
  under the **FreeType License** (text: [FreeType License (FTL)](#freetype-license-ftl)).
- Copyright (C) 1996-2026 by David Turner, Robert Wilhelm, and Werner Lemberg.
- Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All
  rights reserved. This software is based in part on the work of the FreeType Team.
- Upstream: https://freetype.org (2.14.3). Built without HarfBuzz, Brotli, bzip2 and PNG support; gzip
  support uses the device's system zlib, so FreeType's bundled zlib copy is not included.
- Parts of FreeType under their own licenses, both compiled in:
  - **BDF and PCF font drivers**, MIT (text: [MIT License](#mit-license)).
    Copyright (C) 2001-2002 by Francesco Zappa Nardelli (BDF driver);
    Copyright (C) 2000 by Francesco Zappa Nardelli (PCF driver).
  - **BDF library code and `fthash.c`**, MIT-style license of the Computing Research Labs (text:
    [FreeType BDF/hash License](#freetype-bdfhash-license)).
    Copyright 2000 Computing Research Labs, New Mexico State University;
    Copyright 2001-2002, 2011 Francesco Zappa Nardelli (`bdflib.c`, `bdf.h`);
    Copyright 2001-2015 Francesco Zappa Nardelli (`fthash.c`).

### Dear ImGui

- License: MIT (text: [MIT License](#mit-license)).
- Copyright (c) 2014-2025 Omar Cornut
- Upstream: https://github.com/ocornut/imgui (1.91.9b, docking branch), including the SDL3, SDL
  renderer and WebGPU backends and the FreeType font loader.
- Dear ImGui embeds:
  - **ProggyClean.ttf** (default font data), MIT. Copyright (c) 2004, 2005 Tristan Grimmer.
    Upstream: https://www.proggyfonts.net. Text: [MIT License](#mit-license).
  - Modified copies of **stb_rect_pack, stb_textedit and stb_truetype** (public domain option); see
    [Public domain and CC0 components](#public-domain-and-cc0-components).

### SQLite

- License: public domain (no notice required). The SQLite source carries no copyright; its authors
  disclaim copyright.
- Upstream: https://sqlite.org (3.51.3, amalgamation).

### libadrenotools and liblinkernsbypass

- License: BSD-2-Clause (text: [BSD 2-Clause License](#bsd-2-clause-license)).
- Copyright (c) 2021, Billy Laws (libadrenotools)
- Copyright (c) 2021, Billy Laws (liblinkernsbypass)
- Upstream: https://github.com/bylaws/libadrenotools (commit
  8fae8ce254dfc1344527e05301e43f37dea2df80) and https://github.com/bylaws/liblinkernsbypass.
- In this APK they are the four hook libraries `libmain_hook.so`, `libfile_redirect_hook.so`,
  `libgsl_alloc_hook.so` and `libhook_impl.so`.

### Khronos OpenXR loader for Android

- License: the loader source is offered under Apache-2.0 or MIT; this app uses it under
  **Apache-2.0** (text: [Apache License 2.0](#apache-license-20)). The loader package
  (`org.khronos.openxr:openxr_loader_for_android:1.1.63`) ships the Apache-2.0 license and no NOTICE
  file; the OpenXR-SDK source tree has no NOTICE file either.
- Copyright (c) 2017-2026 The Khronos Group Inc.
- Copyright (c) 2017-2019 Valve Corporation
- Copyright (c) 2017-2019 LunarG, Inc.
- Copyright (c) 2019 Collabora, Ltd.
- Upstream: https://github.com/KhronosGroup/OpenXR-SDK. Shipped unmodified as
  `lib/arm64-v8a/libopenxr_loader.so`.
- The loader binary statically includes:
  - **jsoncpp**, public domain or MIT; used under the MIT License (text:
    [MIT License](#mit-license)). Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors.
    Upstream: https://github.com/open-source-parsers/jsoncpp
  - **jnipp**, MIT (text: [MIT License](#mit-license)). Copyright (c) 2016 Mitchell Dowd.
    Upstream: https://github.com/mitchdowd/jnipp
  - **android-jni-wrappers**, BSL-1.0. Copyright 2020-2021, Collabora, Ltd. The Boost Software
    License does not require its notice for machine-executable object code, so its text is not
    reproduced here.
  - LLVM libc++ (static), see below.

### LLVM C++ runtime (libc++, libc++abi, libunwind, compiler-rt builtins)

- License: Apache-2.0 WITH LLVM-exception (text: [Apache License 2.0](#apache-license-20) and
  [LLVM Exceptions](#llvm-exceptions-to-the-apache-20-license)); older libc++ code is also under the
  legacy LLVM license (text: [LLVM legacy license](#llvm-legacy-license-libc)).
- Copyright (c) LLVM Project contributors; legacy code: Copyright (c) 2009-2019 by the contributors
  listed in CREDITS.TXT.
- Upstream: https://github.com/llvm/llvm-project. `libc++_shared.so` is shipped as provided by the
  Android NDK r29 (29.0.14206865); small parts of libunwind and compiler-rt are linked into each
  native library by the NDK toolchain.

### Luckiest Guy font

- License: Apache-2.0 (text: [Apache License 2.0](#apache-license-20); also in
  `smstrikers-port/extern/fonts/LuckiestGuy-LICENSE.txt`). No NOTICE file.
- Copyright (c) 2010 by Brian J. Bonislawsky DBA Astigmatic (AOETI).
- Upstream: https://fonts.google.com/specimen/Luckiest+Guy. The font file
  (`smstrikers-port/extern/fonts/LuckiestGuy-Regular.ttf`) is embedded unmodified in `libmain.so` as
  data (`src/platform/xr_font_luckiest.h`) and drawn with Dear ImGui's copy of stb_truetype for the
  goal banner.

### Opus audio codec (libopus)

- License: the Opus BSD-style license (text: [Opus License](#opus-license)).
- Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO,
  Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla, Amazon.
- Upstream: https://opus-codec.org (release 1.5.2, built at build time from the release tarball,
  https://github.com/xiph/opus/releases/download/v1.5.2/opus-1.5.2.tar.gz, unmodified). Codes the
  online voice chat (`src/platform/xr_voice.cpp`).
- Opus is subject to the royalty-free patent licenses specified at
  https://datatracker.ietf.org/ipr/1524/ (Xiph.Org Foundation), https://datatracker.ietf.org/ipr/1914/
  (Microsoft Corporation) and https://datatracker.ietf.org/ipr/1526/ (Broadcom Corporation).

### rcheevos

- License: MIT (text: [MIT License](#mit-license)).
- Copyright (c) 2018 RetroAchievements.org.
- Upstream: https://github.com/RetroAchievements/rcheevos (release v12.5.0, built at build time from
  the release tarball, https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v12.5.0.tar.gz,
  unmodified; the zip, encrypted-3DS, RAIntegration and libretro parts are left out). Runs the
  RetroAchievements set in the game (`src/platform/xr_ra.cpp`).

### LAPACK and NumPy lapack_lite (guest converter)

- License: BSD-3-Clause (text: [BSD 3-Clause License](#bsd-3-clause-license)).
- LAPACK: Copyright (c) 1992-2013 The University of Tennessee and The University of Tennessee Research
  Foundation; Copyright (c) 2000-2013 The University of California Berkeley; Copyright (c) 2006-2013
  The University of Colorado Denver. All rights reserved.
- NumPy: Copyright (c) 2005-2025, NumPy Developers. All rights reserved.
- `android/app/src/main/java/io/github/newcoke/strikers/guests/pics/Lapack.java` is a Java translation of
  the LAPACK 3.2.2 least-squares path (dgelsd and the routines it calls) as NumPy's `lapack_lite` builds
  it, so the guest kits' colours come out the same as the PC tools'. Only that path is translated.

### Kenney Input Prompts

- License: CC0 1.0 Universal (public domain dedication); no notice required.
- "Input Prompts (1.5A)", created and distributed by Kenney (www.kenney.nl). The pack's license file
  ships in the APK as `assets/input-prompts/LICENSE-Kenney.txt`.

---

## Public domain and CC0 components

These are in the APK but need no notice; they are listed for completeness.

- **SQLite** 3.51.3 (in `libmain.so`): public domain.
- **stb_image** 2.30 (in SDL, `src/video/stb_image.h`), and 2.29 in
  `smstrikers-port/src/platform/third_party/stb_image.h` (alpha 1.13: reads the cup suite pictures):
  offered as MIT or public domain; used under the public-domain option. https://github.com/nothings/stb
- **stb_rect_pack, stb_textedit, stb_truetype** (modified copies in Dear ImGui): offered as MIT or
  public domain; used under the public-domain option. https://github.com/nothings/stb
- **miniz** 1.15 (in SDL, `src/video/miniz.h`): public domain (Unlicense).
- **SDL helpers**: public-domain CRC16/CRC32 and murmur3 code, and code taken from the Public Domain
  C Library (PDCLib).
- **Dusklight** (https://github.com/TwilitRealm/dusklight), CC0 1.0: the SDL audio transport in
  `smstrikers-port/src/platform/audio_out.cpp` is adapted from it.
- **Dolphin's LaggedFibonacciGenerator** (CC0), followed by the RVZ reader in
  `smstrikers-port/src/platform/disc.c` (the rest of that RVZ section is GPL-2.0-or-later, see the
  notes at the top).
- **LZMA SDK** (Igor Pavlov; `LzmaDec.c`, `Lzma2Dec.c` and their headers, in
  `smstrikers-port/extern/lzma`, unmodified, from mGBA's copy): public domain. Reads .rvz/.wia disc
  images made with LZMA or LZMA2 compression. https://www.7-zip.org/sdk.html
- **Kenney Input Prompts** 1.5A: CC0 1.0.

---

## License texts

### MIT License

Applies, with these copyright lines, to:

- Aurora: Copyright (c) 2022 Luke Street
- magic_enum: Copyright (c) 2019 - 2021 Daniil Goncharov
- MusyX: Copyright (c) 2023 Axiomatic Data Laboratories
- {fmt}: Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors (see also the
  optional exception below)
- Dear ImGui: Copyright (c) 2014-2025 Omar Cornut
- ProggyClean font: Copyright (c) 2004, 2005 Tristan Grimmer
- FreeType BDF driver: Copyright (C) 2001-2002 by Francesco Zappa Nardelli
- FreeType PCF driver: Copyright (C) 2000 by Francesco Zappa Nardelli
- jsoncpp: Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors
- jnipp: Copyright (c) 2016 Mitchell Dowd
- rcheevos: Copyright (c) 2018 RetroAchievements.org

```
Copyright (c) <year> <copyright holders>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

{fmt} adds this optional exception to the MIT License:

```
--- Optional exception to the license ---

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into a machine-executable object form of such
source code, you may redistribute such embedded portions in such object form
without including the above copyright and permission notices.
```

jsoncpp states its licensing as follows (the MIT License text it refers to is the one above):

```
The JsonCpp library's source code, including accompanying documentation,
tests and demonstration applications, are licensed under the following
conditions...

Baptiste Lepilleur and The JsonCpp Authors explicitly disclaim copyright in all
jurisdictions which recognize such a disclaimer. In such jurisdictions,
this software is released into the Public Domain.

In jurisdictions which do not recognize Public Domain property (e.g. Germany as of
2010), this software is Copyright (c) 2007-2010 by Baptiste Lepilleur and
The JsonCpp Authors, and is released under the terms of the MIT License (see below).

In jurisdictions which recognize Public Domain property, the user of this
software may choose to accept it either as 1) Public Domain, 2) under the
conditions of the MIT License (see below), or 3) under the terms of dual
Public Domain/MIT License conditions described here, as they choose.
```

### BSD 2-Clause License

Applies, with these copyright lines, to:

- libadrenotools: Copyright (c) 2021, Billy Laws
- liblinkernsbypass: Copyright (c) 2021, Billy Laws
- xxHash: Copyright (c) 2012-2021 Yann Collet

```
Copyright (c) <year>, <copyright holder>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### BSD 3-Clause License

Applies, with these copyright lines, to:

- Dawn and Tint: Copyright 2017-2025 The Dawn & Tint Authors
- webgpu.h: Copyright (c) 2019, "WebGPU native" developers

```
Copyright <year> <copyright holder>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### Opus License

Applies to the Opus audio codec (libopus) 1.5.2.

```
Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic,
                    Jean-Marc Valin, Timothy B. Terriberry,
                    CSIRO, Gregory Maxwell, Mark Borgerding,
                    Erik de Castro Lopo, Mozilla, Amazon

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of Internet Society, IETF or IETF Trust, nor the
names of specific contributors, may be used to endorse or promote
products derived from this software without specific prior written
permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### zstd BSD License

Applies to Zstandard (zstd).

```
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### bzip2 License

Applies to bzip2 / libbzip2 1.0.8.

```
This program, "bzip2", the associated library "libbzip2", and all
documentation, are copyright (C) 1996-2019 Julian R Seward.  All
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. The origin of this software must not be misrepresented; you must
   not claim that you wrote the original software.  If you use this
   software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.

3. Altered source versions must be plainly marked as such, and must
   not be misrepresented as being the original software.

4. The name of the author may not be used to endorse or promote
   products derived from this software without specific prior written
   permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Julian Seward, jseward@acm.org
bzip2/libbzip2 version 1.0.8 of 13 July 2019
```

### yuv2rgb BSD License

Applies to yuv2rgb (bundled in SDL).

```
Copyright (c) 2016, Adrien Descamps
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of yuv2rgb nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### ODE BSD-style License

Applies to the Open Dynamics Engine (chosen over the LGPL-2.1 option).

```
This is the BSD-style license for the Open Dynamics Engine
----------------------------------------------------------

Open Dynamics Engine
Copyright (c) 2001-2004, Russell L. Smith.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

Neither the names of ODE's copyright owner nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### zlib License

Applies, with these copyright lines, to:

- SDL 3 (native library and Java glue): Copyright (C) 1997-2026 Sam Lantinga; Copyright (C) Valve
  Corporation; Copyright 2023 Collabora Ltd.; Copyright (C) 2023 Max Maisel; Copyright (C) 2025
  Simon Wood; Copyright (C) 2025 Michal Malý; Copyright (C) 2025 Katharine Chui; Copyright (C) 2025
  Bernat Arlandis; Copyright (C) 2025 Mitchell Cairns; Copyright (C) 2025 Zuiki Inc.; Copyright (C)
  2001-2011 Andreas Schiffler; Copyright (c) 1998-2021 Gareth McCaughan
- libogc-derived code in Aurora: Copyright (C) 2004 Michael Wiedenbauer (shagkur), Dave Murphy
  (WinterMute)

```
Copyright (C) <year> <copyright holder>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### HIDAPI original license

Applies to HIDAPI as bundled in SDL (HIDAPI is offered under GPLv3, a BSD-style license, or this
original license; this app uses the original license).

```
 HIDAPI - Multi-Platform library for
 communication with HID devices.

 Copyright 2009, Alan Ott, Signal 11 Software.
 All Rights Reserved.

 This software may be used by anyone for any reason so
 long as the copyright notice in the source files
 remains intact.
```

### Apache License 2.0

Applies, with these copyright lines, to:

- Abseil (both copies): Copyright 2017-2024 The Abseil Authors; Copyright 2016 Google Inc.
- SPIRV-Tools (inside Dawn): Copyright The Khronos Group Inc., Google Inc. / Google LLC and other
  SPIRV-Tools contributors
- Luckiest Guy font: Copyright (c) 2010 by Brian J. Bonislawsky DBA Astigmatic (AOETI)
- Khronos OpenXR loader: Copyright (c) 2017-2026 The Khronos Group Inc.; Copyright (c) 2017-2019
  Valve Corporation; Copyright (c) 2017-2019 LunarG, Inc.; Copyright (c) 2019 Collabora, Ltd.
- LLVM libc++, libc++abi, libunwind, compiler-rt builtins: LLVM Project contributors (together with
  the LLVM Exceptions below)

NOTICE files: none of these components distributes a NOTICE file with the version used here
(checked: the Abseil 20240722.0 source tree, the Luckiest Guy font package, the OpenXR loader 1.1.63 package and the OpenXR-SDK
source tree, and the LLVM notices in Android NDK r29), so there is no NOTICE content to reproduce.
For SPIRV-Tools, see "Not included / uncertain".

```
                                 Apache License
                           Version 2.0, January 2004
                        http://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

### LLVM Exceptions to the Apache 2.0 License

Applies to the LLVM runtime components (libc++, libc++abi, libunwind, compiler-rt builtins).

```
---- LLVM Exceptions to the Apache 2.0 License ----

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into an Object form of such source code, you
may redistribute such embedded portions in such Object form without complying
with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

In addition, if you combine or link compiled forms of this Software with
software that is licensed under the GPLv2 ("Combined Software") and if a
court of competent jurisdiction determines that the patent provision (Section
3), the indemnity provision (Section 9) or other Section of the License
conflicts with the conditions of the GPLv2, you may retroactively and
prospectively choose to deem waived or otherwise exclude such Section(s) of
the License, but only in their entirety and only with respect to the Combined
Software.
```

### LLVM legacy license (libc++)

Older libc++ and libc++abi code is dual-licensed under the University of Illinois/NCSA Open Source
License and the MIT License (MIT text above, with the copyright line "Copyright (c) 2009-2014 by the
contributors listed in CREDITS.TXT").

```
University of Illinois/NCSA
Open Source License

Copyright (c) 2009-2019 by the contributors listed in CREDITS.TXT

All rights reserved.

Developed by:

    LLVM Team

    University of Illinois at Urbana-Champaign

    http://llvm.org

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal with
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimers.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimers in the
      documentation and/or other materials provided with the distribution.

    * Neither the names of the LLVM Team, University of Illinois at
      Urbana-Champaign, nor the names of its contributors may be used to
      endorse or promote products derived from this Software without specific
      prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.
```

### libpng License

Applies to libpng.

```
COPYRIGHT NOTICE, DISCLAIMER, and LICENSE
=========================================

PNG Reference Library License version 2
---------------------------------------

 * Copyright (c) 1995-2026 The PNG Reference Library Authors.
 * Copyright (c) 2018-2026 Cosmin Truta.
 * Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson.
 * Copyright (c) 1996-1997 Andreas Dilger.
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

The software is supplied "as is", without warranty of any kind,
express or implied, including, without limitation, the warranties
of merchantability, fitness for a particular purpose, title, and
non-infringement.  In no event shall the Copyright owners, or
anyone distributing the software, be liable for any damages or
other liability, whether in contract, tort or otherwise, arising
from, out of, or in connection with the software, or the use or
other dealings in the software, even if advised of the possibility
of such damage.

Permission is hereby granted to use, copy, modify, and distribute
this software, or portions hereof, for any purpose, without fee,
subject to the following restrictions:

 1. The origin of this software must not be misrepresented; you
    must not claim that you wrote the original software.  If you
    use this software in a product, an acknowledgment in the product
    documentation would be appreciated, but is not required.

 2. Altered source versions must be plainly marked as such, and must
    not be misrepresented as being the original software.

 3. This Copyright notice may not be removed or altered from any
    source or altered source distribution.


PNG Reference Library License version 1 (for libpng 0.5 through 1.6.35)
-----------------------------------------------------------------------

libpng versions 1.0.7, July 1, 2000, through 1.6.35, July 15, 2018 are
Copyright (c) 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson, are
derived from libpng-1.0.6, and are distributed according to the same
disclaimer and license as libpng-1.0.6 with the following individuals
added to the list of Contributing Authors:

    Simon-Pierre Cadieux
    Eric S. Raymond
    Mans Rullgard
    Cosmin Truta
    Gilles Vollant
    James Yu
    Mandar Sahastrabuddhe
    Google Inc.
    Vadim Barkov

and with the following additions to the disclaimer:

    There is no warranty against interference with your enjoyment of
    the library or against infringement.  There is no warranty that our
    efforts or the library will fulfill any of your particular purposes
    or needs.  This library is provided with all faults, and the entire
    risk of satisfactory quality, performance, accuracy, and effort is
    with the user.

Some files in the "contrib" directory and some configure-generated
files that are distributed with libpng have other copyright owners, and
are released under other open source licenses.

libpng versions 0.97, January 1998, through 1.0.6, March 20, 2000, are
Copyright (c) 1998-2000 Glenn Randers-Pehrson, are derived from
libpng-0.96, and are distributed according to the same disclaimer and
license as libpng-0.96, with the following individuals added to the
list of Contributing Authors:

    Tom Lane
    Glenn Randers-Pehrson
    Willem van Schaik

libpng versions 0.89, June 1996, through 0.96, May 1997, are
Copyright (c) 1996-1997 Andreas Dilger, are derived from libpng-0.88,
and are distributed according to the same disclaimer and license as
libpng-0.88, with the following individuals added to the list of
Contributing Authors:

    John Bowler
    Kevin Bracey
    Sam Bushell
    Magnus Holmgren
    Greg Roelofs
    Tom Tanner

Some files in the "scripts" directory have other copyright owners,
but are released under this license.

libpng versions 0.5, May 1995, through 0.88, January 1996, are
Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.

For the purposes of this copyright and license, "Contributing Authors"
is defined as the following set of individuals:

    Andreas Dilger
    Dave Martindale
    Guy Eric Schalnat
    Paul Schmidt
    Tim Wegner

The PNG Reference Library is supplied "AS IS".  The Contributing
Authors and Group 42, Inc. disclaim all warranties, expressed or
implied, including, without limitation, the warranties of
merchantability and of fitness for any purpose.  The Contributing
Authors and Group 42, Inc. assume no liability for direct, indirect,
incidental, special, exemplary, or consequential damages, which may
result from the use of the PNG Reference Library, even if advised of
the possibility of such damage.

Permission is hereby granted to use, copy, modify, and distribute this
source code, or portions hereof, for any purpose, without fee, subject
to the following restrictions:

 1. The origin of this source code must not be misrepresented.

 2. Altered versions must be plainly marked as such and must not
    be misrepresented as being the original source.

 3. This Copyright notice may not be removed or altered from any
    source or altered source distribution.

The Contributing Authors and Group 42, Inc. specifically permit,
without fee, and encourage the use of this source code as a component
to supporting the PNG file format in commercial products.  If you use
this source code in a product, acknowledgment is not required but would
be appreciated.
```

### FreeType License (FTL)

Applies to FreeType (chosen over the GPLv2 option). As the FTL requires for binary distribution:
Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All
rights reserved. This software is based in part on the work of the FreeType Team.

```
                    The FreeType Project LICENSE
                    ----------------------------

                            2006-Jan-27

                    Copyright 1996-2002, 2006 by
          David Turner, Robert Wilhelm, and Werner Lemberg



Introduction
============

  The FreeType  Project is distributed in  several archive packages;
  some of them may contain, in addition to the FreeType font engine,
  various tools and  contributions which rely on, or  relate to, the
  FreeType Project.

  This  license applies  to all  files found  in such  packages, and
  which do not  fall under their own explicit  license.  The license
  affects  thus  the  FreeType   font  engine,  the  test  programs,
  documentation and makefiles, at the very least.

  This  license   was  inspired  by  the  BSD,   Artistic,  and  IJG
  (Independent JPEG  Group) licenses, which  all encourage inclusion
  and  use of  free  software in  commercial  and freeware  products
  alike.  As a consequence, its main points are that:

    o We don't promise that this software works. However, we will be
      interested in any kind of bug reports. (`as is' distribution)

    o You can  use this software for whatever you  want, in parts or
      full form, without having to pay us. (`royalty-free' usage)

    o You may not pretend that  you wrote this software.  If you use
      it, or  only parts of it,  in a program,  you must acknowledge
      somewhere  in  your  documentation  that  you  have  used  the
      FreeType code. (`credits')

  We  specifically  permit  and  encourage  the  inclusion  of  this
  software, with  or without modifications,  in commercial products.
  We  disclaim  all warranties  covering  The  FreeType Project  and
  assume no liability related to The FreeType Project.


  Finally,  many  people  asked  us  for  a  preferred  form  for  a
  credit/disclaimer to use in compliance with this license.  We thus
  encourage you to use the following text:

   """
    Portions of this software are copyright © <year> The FreeType
    Project (https://freetype.org).  All rights reserved.
   """

  Please replace <year> with the value from the FreeType version you
  actually use.


Legal Terms
===========

0. Definitions
--------------

  Throughout this license,  the terms `package', `FreeType Project',
  and  `FreeType  archive' refer  to  the  set  of files  originally
  distributed  by the  authors  (David Turner,  Robert Wilhelm,  and
  Werner Lemberg) as the `FreeType Project', be they named as alpha,
  beta or final release.

  `You' refers to  the licensee, or person using  the project, where
  `using' is a generic term including compiling the project's source
  code as  well as linking it  to form a  `program' or `executable'.
  This  program is  referred to  as  `a program  using the  FreeType
  engine'.

  This  license applies  to all  files distributed  in  the original
  FreeType  Project,   including  all  source   code,  binaries  and
  documentation,  unless  otherwise  stated   in  the  file  in  its
  original, unmodified form as  distributed in the original archive.
  If you are  unsure whether or not a particular  file is covered by
  this license, you must contact us to verify this.

  The FreeType  Project is copyright (C) 1996-2000  by David Turner,
  Robert Wilhelm, and Werner Lemberg.  All rights reserved except as
  specified below.

1. No Warranty
--------------

  THE FREETYPE PROJECT  IS PROVIDED `AS IS' WITHOUT  WARRANTY OF ANY
  KIND, EITHER  EXPRESS OR IMPLIED,  INCLUDING, BUT NOT  LIMITED TO,
  WARRANTIES  OF  MERCHANTABILITY   AND  FITNESS  FOR  A  PARTICULAR
  PURPOSE.  IN NO EVENT WILL ANY OF THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE  FOR ANY DAMAGES CAUSED  BY THE USE OR  THE INABILITY TO
  USE, OF THE FREETYPE PROJECT.

2. Redistribution
-----------------

  This  license  grants  a  worldwide, royalty-free,  perpetual  and
  irrevocable right  and license to use,  execute, perform, compile,
  display,  copy,   create  derivative  works   of,  distribute  and
  sublicense the  FreeType Project (in  both source and  object code
  forms)  and  derivative works  thereof  for  any  purpose; and  to
  authorize others  to exercise  some or all  of the  rights granted
  herein, subject to the following conditions:

    o Redistribution of  source code  must retain this  license file
      (`FTL.TXT') unaltered; any  additions, deletions or changes to
      the original  files must be clearly  indicated in accompanying
      documentation.   The  copyright   notices  of  the  unaltered,
      original  files must  be  preserved in  all  copies of  source
      files.

    o Redistribution in binary form must provide a  disclaimer  that
      states  that  the software is based in part of the work of the
      FreeType Team,  in  the  distribution  documentation.  We also
      encourage you to put an URL to the FreeType web page  in  your
      documentation, though this isn't mandatory.

  These conditions  apply to any  software derived from or  based on
  the FreeType Project,  not just the unmodified files.   If you use
  our work, you  must acknowledge us.  However, no  fee need be paid
  to us.

3. Advertising
--------------

  Neither the  FreeType authors and  contributors nor you  shall use
  the name of the  other for commercial, advertising, or promotional
  purposes without specific prior written permission.

  We suggest,  but do not require, that  you use one or  more of the
  following phrases to refer  to this software in your documentation
  or advertising  materials: `FreeType Project',  `FreeType Engine',
  `FreeType library', or `FreeType Distribution'.

  As  you have  not signed  this license,  you are  not  required to
  accept  it.   However,  as  the FreeType  Project  is  copyrighted
  material, only  this license, or  another one contracted  with the
  authors, grants you  the right to use, distribute,  and modify it.
  Therefore,  by  using,  distributing,  or modifying  the  FreeType
  Project, you indicate that you understand and accept all the terms
  of this license.

4. Contacts
-----------

  There are two mailing lists related to FreeType:

    o freetype@nongnu.org

      Discusses general use and applications of FreeType, as well as
      future and  wanted additions to the  library and distribution.
      If  you are looking  for support,  start in  this list  if you
      haven't found anything to help you in the documentation.

    o freetype-devel@nongnu.org

      Discusses bugs,  as well  as engine internals,  design issues,
      specific licenses, porting, etc.

  Our home page can be found at

    https://freetype.org


--- end of FTL.TXT ---
```

### FreeType BDF/hash License

Applies to FreeType's BDF library code (`src/bdf/bdflib.c`, `src/bdf/bdf.h`) and
`src/base/fthash.c`, with these copyright lines:

- Copyright 2000 Computing Research Labs, New Mexico State University
- Copyright 2001-2002, 2011 Francesco Zappa Nardelli (`bdflib.c`, `bdf.h`)
- Copyright 2001-2015 Francesco Zappa Nardelli (`fthash.c`)

```
Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE COMPUTING RESEARCH LAB OR NEW MEXICO STATE UNIVERSITY BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### Khronos SPIR-V License

Applies to the SPIR-V headers and grammar data compiled into SPIRV-Tools inside Dawn.

```
Copyright (c) 2014-2024 The Khronos Group Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and/or associated documentation files (the "Materials"),
to deal in the Materials without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Materials, and to permit persons to whom the
Materials are furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Materials.

MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS KHRONOS
STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS SPECIFICATIONS AND
HEADER INFORMATION ARE LOCATED AT https://www.khronos.org/registry/

THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM,OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR OTHER DEALINGS
IN THE MATERIALS.
```

---

## Not included / uncertain

Not included (checked, and not in the APK):

- **Tracy profiler** (BSD-3-Clause): its client library is on the link line, but it is built with
  `TRACY_ENABLE=OFF`, and `libmain.so` contains no Tracy code (checked in its symbol table).
- **FFmpeg**: the build option is on, but no FFmpeg libraries were found at configure time, so
  nothing is linked (no FFmpeg symbols in `libmain.so`).
- **nod, RmlUi, zlib-ng and Aurora's own zstd fetch**: disabled or not fetched in this
  configuration.
- **zlib**: FreeType and the disc reader use the device's system `libz.so`, which is not shipped.
  Other system libraries the app links against (`libvulkan.so`, `libEGL`/`libGLESv*`, `libOpenSLES`,
  `libandroid`, `liblog`, `libm`, `libdl`) are also provided by the device, not the APK.
- **libadrenotools core** (`libadrenotools.a`): it is linked only into the Vulkan shim
  `libvulkan.so`, which the Quest build leaves out of the APK; only the four hook libraries ship
  (listed above).
- **SDL parts compiled but not in the binary**: `src/events/imKStoUCS.c` (X11-style license) is
  compiled only for X11/Wayland, and SDL's `src/libm` (derived from Sun's fdlibm) is compiled, but
  none of its functions survive linking because the device's libm is used.
- **Khronos API headers** (Vulkan, OpenXR, EGL, OpenGL ES; Apache-2.0 or MIT): used only as
  declarations at compile time. They are not listed as components.

Uncertain:

- **Dawn's third-party code**: the prebuilt Dawn package has no license or notices file. Its
  contents were identified from the object list of `libwebgpu_dawn.a` and the symbols in
  `libmain.so` (Tint, Abseil, SPIRV-Tools, SPIR-V grammar). The exact versions Dawn pinned, the
  SPIRV-Tools copyright lines, and whether that SPIRV-Tools version carries a NOTICE file could not be
  checked offline. The SPIR-V license text above comes from another local copy of the Khronos
  headers, so its years may differ from Dawn's copy. Any Vulkan or OpenGL helper code generated from
  Khronos registry files is not listed separately.
- **OpenXR loader's bundled code**: jsoncpp, jnipp and android-jni-wrappers were identified from
  strings in the loader binary. Their license texts come from a local OpenXR-SDK 1.1.63 source tree,
  not from the loader package itself.
- **kabufuda**: Aurora's build describes its memory card code as "based on kabufuda"
  (https://github.com/AxioDL/kabufuda, MIT). Aurora's sources carry no separate kabufuda notice, so
  none is reproduced here.
- **TPVR**: comments in `smstrikers-port/src/platform/xr_quest.cpp` say parts of the Quest image
  export follow code from TPVR (https://github.com/JoeyAW/TPVR, built on Dusklight). The local
  reference copy has no license file, so its license could not be confirmed.
