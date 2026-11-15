package xbox_mc.compat;

import me.jellysquid.mods.sodium.client.gl.tessellation.GlIndexType;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Mutable;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

// fewer draw calls, part 1. sodium writes the indices of each face group of a
// chunk relative to that group's smallest vertex (its own base vertex) and picks
// the smallest index type per group, 8 bit included. so no two groups can be
// drawn together, and d3d11 has no 8 bit indices (angle converts them on the cpu).
// here: indices stay relative to the chunk (base vertex 0) and are always 16 bit
// (32 bit only for huge chunks), so neighbouring groups can be merged into one
// draw (RegionChunkRendererMixin).
@Mixin(targets = "me.jellysquid.mods.sodium.client.model.IndexBufferBuilder$Result", remap = false)
public class IndexResultMixin {
    @Shadow @Final @Mutable private int minIndex;
    @Shadow @Final private int maxIndex;
    @Shadow @Final @Mutable private GlIndexType format;

    @Inject(method = "<init>", at = @At("RETURN"), remap = false)
    private void xbox_mc$chunkRelative(CallbackInfo ci) {
        this.minIndex = 0;
        this.format = this.maxIndex < 65536 ? GlIndexType.UNSIGNED_SHORT : GlIndexType.UNSIGNED_INT;
    }
}
