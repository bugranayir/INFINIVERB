# INFINIVERB

**Stereo Space Reverberation Unit** — an algorithmic stereo reverb plug-in by fatbird Studios.
VST3 and Audio Unit for macOS (Apple Silicon and Intel); Windows to follow.

[fatbird-studios.com](https://fatbird-studios.com)

## Building

Requires CMake 3.22 or later and a C++17 compiler (Xcode on macOS). JUCE 8.0.12 is fetched
automatically and checked against its release commit.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target RVB1_VST3 RVB1_AU
```

Add `-DRVB1_UNIVERSAL_BINARY=ON` for an Apple Silicon + Intel build. `RVB1` is the project's
code name.

## Licence

Copyright © 2026 fatbird Studios.

INFINIVERB is free software, licensed under the GNU Affero General Public License v3 — see
[LICENSE](LICENSE). Third-party components and their licences are listed in
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

The names INFINIVERB and fatbird Studios and the fatbird Studios logo are not covered by the
AGPLv3: a modified or redistributed build must use a different name and remove them.
