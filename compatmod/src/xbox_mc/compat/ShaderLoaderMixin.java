package xbox_mc.compat;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

// the xbox one's gpu driver can't link shaders that use uniform blocks. sodium
// keeps the chunk offsets of a render region in one, but they never change
// (x*16, y*16, z*16 per chunk), so the chunk shader can just work them out
// from the chunk index. this swaps the two shader includes that do it.
// the files are matched by their content, so no minecraft classes are needed.
@Mixin(targets = "me.jellysquid.mods.sodium.client.gl.shader.ShaderLoader", remap = false)
public class ShaderLoaderMixin {
    private static final String BLOCK = "uniform ubo_DrawParameters";
    private static final String TRANSLATION = "#define _draw_translation Chunks[_draw_id].offset.xyz";

    // chunk index = x * 32 + y * 8 + z (sodium's RenderRegion, 8 x 4 x 8 chunks)
    private static final String COMPUTED = "#define _draw_translation (vec3(float(_draw_id >> 5u), "
            + "float((_draw_id >> 3u) & 3u), float(_draw_id & 7u)) * 16.0)";

    @Inject(method = "getShaderSource", at = @At("RETURN"), cancellable = true, remap = false)
    private static void xbox_mc$rewrite(CallbackInfoReturnable<String> cir) {
        String src = cir.getReturnValue();
        if (src == null) return;
        if (src.contains(BLOCK)) {
            cir.setReturnValue("// xbox-mc: no uniform block here, see chunk_vertex.glsl\n");
        } else if (src.contains(TRANSLATION)) {
            cir.setReturnValue(src.replace(TRANSLATION, COMPUTED));
        }
    }
}
