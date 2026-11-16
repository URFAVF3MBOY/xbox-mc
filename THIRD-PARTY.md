# Third-party components

| Component | Used for | License |
| --- | --- | --- |
| [ANGLE](https://github.com/google/angle) (built with vcpkg, `x64-uwp`) | OpenGL ES on top of Direct3D 11 | BSD-3-Clause |
| [zlib](https://zlib.net) (ANGLE dependency) | compression | zlib |
| Microsoft VCLibs 14.0 framework, `d3dcompiler_47.dll` | C++ runtime and HLSL compiler from the Windows SDK | Microsoft redistributable terms |
| [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) with my windows patches in `mobileglues/` | desktop OpenGL on top of GLES | LGPL-2.1 (`mobileglues/LICENSE`) |
| [OpenJDK](https://adoptium.net) 17 and 21 (Temurin, cut down with jlink) | the java runtimes | GPLv2 with Classpath Exception (`jre\legal`, `jre21\legal` in the package) |
| [LWJGL](https://www.lwjgl.org) 3.2.2 and 3.3.3 | java bindings for GL and GLFW | BSD-3-Clause (`licenses/LWJGL.txt`) |
| [FreeType](https://freetype.org) (`freetype.dll` from LWJGL 3.3.3) | fonts in minecraft 1.21 | FreeType License. Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved. |
| [JNA](https://github.com/java-native-access/jna) 5.8.0 and 5.17.0, only `jnidispatch.dll` | minecraft uses it for system info | Apache-2.0 or LGPL-2.1 |
| [SDL2](https://libsdl.org) 2.32.10, only `SDL2.dll` | controller input for Controllable on 1.21.11 | zlib |

The app package has all the license texts in `licenses\`, `jre\legal\` and `jre21\legal\`.

Not in the package, downloaded by the launcher on the console: Minecraft (from
Mojang, after sign-in), Fabric Loader (from Fabric), the mods from Modrinth
(Fabric API, Sodium, Sodium Extra, Reese's Sodium Options, Lithium, FerriteCore,
Entity Culling, and for 1.17.1 LazyDFU and Starlight, for 1.21.11 ImmediatelyFast)
and for 1.21.11 Controllable and Framework (from CurseForge). Sodium 0.3 is
patched at runtime by my own mod in compatmod/, Sodium itself isn't changed or
shipped.
