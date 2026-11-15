package xbox_mc.compat;

import me.jellysquid.mods.sodium.client.gl.shader.uniform.GlUniformBlock;
import me.jellysquid.mods.sodium.client.render.chunk.shader.ShaderBindingContext;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

// the chunk shader has no ubo_DrawParameters block anymore (ShaderLoaderMixin),
// so looking it up would throw. hand back a block object that isn't tied to
// anything; binding the buffer to it later just does nothing useful.
@Mixin(targets = "me.jellysquid.mods.sodium.client.render.chunk.shader.ChunkShaderInterface", remap = false)
public class ChunkShaderInterfaceMixin {
    @Redirect(method = "<init>", remap = false, at = @At(value = "INVOKE",
            target = "Lme/jellysquid/mods/sodium/client/render/chunk/shader/ShaderBindingContext;"
                    + "bindUniformBlock(Ljava/lang/String;I)Lme/jellysquid/mods/sodium/client/gl/shader/uniform/GlUniformBlock;"))
    private GlUniformBlock xbox_mc$noBlock(ShaderBindingContext context, String name, int binding) {
        return new GlUniformBlock(binding);
    }
}
