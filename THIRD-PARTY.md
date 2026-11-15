# Third-party components

| Component | Used for | License |
| --- | --- | --- |
| [ANGLE](https://github.com/google/angle) (built with vcpkg, `x64-uwp`) | OpenGL ES on top of Direct3D 11 | BSD-3-Clause |
| [zlib](https://zlib.net) (ANGLE dependency) | compression | zlib |
| Microsoft VCLibs 14.0 framework, `d3dcompiler_47.dll` | C++ runtime and HLSL compiler from the Windows SDK | Microsoft redistributable terms |
| [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) with my windows patches in `mobileglues/` | desktop OpenGL on top of GLES | LGPL-2.1 (`mobileglues/LICENSE`) |
| [OpenJDK](https://adoptium.net) 17 (Temurin, cut down with jlink) | the java runtime | GPLv2 with Classpath Exception (`jre\legal` in the package) |
| [LWJGL](https://www.lwjgl.org) 3.2.2 | java bindings for GL and GLFW | BSD-3-Clause (`licenses/LWJGL.txt`) |
| [JNA](https://github.com/java-native-access/jna) 5.8.0, only `jnidispatch.dll` | minecraft uses it for system info | Apache-2.0 or LGPL-2.1 |

The app package has all the license texts in `licenses\` and `jre\legal\`.

Not in the package, downloaded by the launcher on the console: Minecraft (from
Mojang, after sign-in), Fabric Loader (from Fabric) and the mods Fabric API,
Sodium, Sodium Extra, Reese's Sodium Options, Lithium, LazyDFU, FerriteCore,
Starlight and Entity Culling (from Modrinth). Sodium is patched at runtime by my
own mod in compatmod/, Sodium itself isn't changed or shipped.
