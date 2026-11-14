# Third-party components

| Component | Used for | License |
| --- | --- | --- |
| [ANGLE](https://github.com/google/angle) (built with vcpkg, `x64-uwp`) | OpenGL ES on top of Direct3D 11 | BSD-3-Clause |
| [zlib](https://zlib.net) (ANGLE dependency) | compression | zlib |
| Microsoft VCLibs 14.0 framework, `d3dcompiler_47.dll` | C++ runtime and HLSL compiler from the Windows SDK | Microsoft redistributable terms |

| [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) with my windows patches in `mobileglues/` | desktop OpenGL on top of GLES | LGPL-2.1 (`mobileglues/LICENSE`) |

Planned: OpenJDK, LWJGL, Fabric. Minecraft itself is never included; the launcher
downloads it from Mojang after sign-in.
