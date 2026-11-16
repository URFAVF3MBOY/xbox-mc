package xbox_mc.compat21;

import java.nio.file.Files;
import java.nio.file.Path;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

// controllable unpacks SDL2.dll into the temp folder and loads it from there.
// the xbox only loads dlls from the app's own package, so the app ships SDL2.dll
// in natives21\ and this points controllable's unpack folder at that. the file is
// already there, so nothing gets unpacked and it loads from the package
@Mixin(targets = "com.mrcrayfish.controllable_sdl.jna.SdlNativeLibraryLoader", remap = false)
public abstract class SdlFromPackageMixin {
    @Inject(method = "getExtractionPath", at = @At("HEAD"), cancellable = true, require = 0)
    private static void xmc$packageNatives(CallbackInfoReturnable<Path> cir) {
        String install = System.getProperty("xmc.install");
        if (install == null) return;
        Path natives = Path.of(install, "natives21");
        if (Files.exists(natives.resolve("SDL2.dll"))) cir.setReturnValue(natives);
    }
}
