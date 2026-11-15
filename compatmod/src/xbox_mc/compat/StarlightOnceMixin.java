package xbox_mc.compat;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

// when a chunk section arrives from the server (class_2826.method_12258 = read),
// starlight scans all 4096 blocks for its transparency data twice: once at the
// end of recalcBlockCounts (lithium calls that from read) and once more at the end
// of read itself. both run after the section is fully read, so the second scan
// gives the same result. this lets only the first one run inside a read.
// initKnownTransparenciesData is the method starlight's mixin adds to the class,
// so this applies after starlight (priority 1100). if starlight isn't there the
// injector just doesn't apply (require = 0).
@Mixin(targets = "net.minecraft.class_2826", remap = false, priority = 1100)
public abstract class StarlightOnceMixin {
    private boolean xmc$reading;
    private int xmc$scans;

    @Inject(method = "method_12258(Lnet/minecraft/class_2540;)V", at = @At("HEAD"))
    private void xmc$readStart(CallbackInfo ci) {
        xmc$reading = true;
        xmc$scans = 0;
    }

    @Inject(method = "method_12258(Lnet/minecraft/class_2540;)V", at = @At("RETURN"))
    private void xmc$readEnd(CallbackInfo ci) {
        xmc$reading = false;
    }

    @Inject(method = "initKnownTransparenciesData()V", at = @At("HEAD"), cancellable = true, require = 0)
    private void xmc$onlyOnce(CallbackInfo ci) {
        if (xmc$reading && xmc$scans++ > 0) ci.cancel();
    }
}
