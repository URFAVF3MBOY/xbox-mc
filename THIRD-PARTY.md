# Third-party components

| Component | Used for | License |
| --- | --- | --- |
| [ANGLE](https://github.com/google/angle) (built with vcpkg, `x64-uwp`) | OpenGL ES on top of Direct3D 11 | BSD-3-Clause |
| [zlib](https://zlib.net) (ANGLE dependency) | compression | zlib |
| Microsoft VCLibs 14.0 framework, `d3dcompiler_47.dll` | C++ runtime and HLSL compiler from the Windows SDK | Microsoft redistributable terms |
| [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) with my windows patches in `mobileglues/` | desktop OpenGL on top of GLES | LGPL-2.1 (`mobileglues/LICENSE`) |
| [OpenJDK](https://adoptium.net) 17 (Temurin, cut down with jlink) | the java runtime | GPLv2 with Classpath Exception (`jre\legal` in the package) |
| [LWJGL](https://www.lwjgl.org) 3.2.2 | java bindings for GL and GLFW | BSD-3-Clause (`licenses/LWJGL.txt`) |

The app package has all the license texts in `licenses\` and `jre\legal\`.

Planned: Fabric. Minecraft itself is never included; the launcher downloads it
from Mojang after sign-in.
