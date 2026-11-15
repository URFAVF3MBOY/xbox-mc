package xbox_mc.bench;

import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.class_310;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

// dev only, for benchmarks on my pc: if game\benchworld.txt exists, load that
// world straight from the title screen (same world every run = comparable fps)
@Mixin(targets = "net.minecraft.class_442", remap = false)
public class TitleScreenMixin {
    private static boolean xbox_mc$done;

    @Inject(method = "method_25426", at = @At("RETURN"), remap = false)
    private void xbox_mc$bench(CallbackInfo ci) {
        if (xbox_mc$done) return;
        xbox_mc$done = true;
        try {
            Path f = Path.of("benchworld.txt");
            if (!Files.exists(f)) return;
            String world = Files.readString(f).trim();
            System.out.println("bench: loading world " + world);
            class_310 mc = class_310.method_1551();
            mc.execute(() -> mc.method_29606(world));
        } catch (Exception e) {
            System.out.println("bench: " + e);
        }
    }
}
