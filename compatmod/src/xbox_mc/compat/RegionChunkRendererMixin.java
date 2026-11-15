package xbox_mc.compat;

import java.util.List;
import me.jellysquid.mods.sodium.client.gl.device.RenderDevice;
import me.jellysquid.mods.sodium.client.gl.tessellation.GlIndexType;
import me.jellysquid.mods.sodium.client.gl.util.ElementRange;
import me.jellysquid.mods.sodium.client.gl.util.MultiDrawBatch;
import me.jellysquid.mods.sodium.client.model.quad.properties.ModelQuadFacing;
import me.jellysquid.mods.sodium.client.model.vertex.type.ChunkVertexType;
import me.jellysquid.mods.sodium.client.render.chunk.ChunkCameraContext;
import me.jellysquid.mods.sodium.client.render.chunk.ChunkGraphicsState;
import me.jellysquid.mods.sodium.client.render.chunk.RenderSection;
import me.jellysquid.mods.sodium.client.render.chunk.ShaderChunkRenderer;
import me.jellysquid.mods.sodium.client.render.chunk.data.ChunkRenderBounds;
import me.jellysquid.mods.sodium.client.render.chunk.passes.BlockRenderPass;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

// fewer draw calls, part 2. on the xbox one there's no real multi-draw (GLES 3.0),
// so every entry in sodium's multi-draw list becomes its own draw call through
// mobileglues, angle and d3d11, which is a lot of cpu time on the jaguar.
// sodium adds one entry per visible face group of a chunk. the groups are stored
// back to back (UP, DOWN, EAST, WEST, SOUTH, NORTH, UNASSIGNED), and with
// IndexResultMixin they share base vertex and index type, so neighbouring visible
// groups get merged into one entry here. with block face culling off every chunk
// is exactly one draw.
@Mixin(targets = "me.jellysquid.mods.sodium.client.render.chunk.RegionChunkRenderer", remap = false)
public abstract class RegionChunkRendererMixin extends ShaderChunkRenderer {
    @Shadow @Final private MultiDrawBatch[] batches;
    @Shadow @Final private boolean isBlockFaceCullingEnabled;

    private RegionChunkRendererMixin(RenderDevice device, ChunkVertexType vertexType) {
        super(device, vertexType);
    }

    // current run of merged face groups
    private long xbox_mc$runPointer, xbox_mc$runEnd;
    private int xbox_mc$runCount, xbox_mc$runBase;
    private GlIndexType xbox_mc$runType;

    @Inject(method = "buildDrawBatches", at = @At("HEAD"), cancellable = true, remap = false)
    private void xbox_mc$merged(List<RenderSection> sections, BlockRenderPass pass, ChunkCameraContext camera,
                                CallbackInfoReturnable<Boolean> cir) {
        for (MultiDrawBatch batch : this.batches) {
            batch.begin();
        }

        boolean translucent = pass.isTranslucent();
        int n = sections.size();
        for (int s = 0; s < n; s++) {
            // translucent chunks go back to front, like sodium's sortedChunks
            RenderSection render = sections.get(translucent ? n - 1 - s : s);
            ChunkGraphicsState state = render.getGraphicsState(pass);
            if (state == null) continue;

            ChunkRenderBounds bounds = render.getBounds();
            long indexOffset = state.getIndexSegment().getOffset();
            int baseVertex = state.getVertexSegment().getOffset() / this.vertexFormat.getStride();

            this.xbox_mc$runType = null;
            for (ModelQuadFacing facing : ModelQuadFacing.VALUES) {
                ElementRange part = state.getModelPart(facing);
                // a missing group takes no space, so it doesn't break a run
                if (part == null) continue;
                if (!this.xbox_mc$visible(facing, camera, bounds)) {
                    this.xbox_mc$flush();
                    continue;
                }
                long pointer = indexOffset + part.elementPointer();
                int base = baseVertex + part.baseVertex();
                GlIndexType type = part.indexType();
                if (type == this.xbox_mc$runType && pointer == this.xbox_mc$runEnd && base == this.xbox_mc$runBase) {
                    this.xbox_mc$runCount += part.elementCount();
                } else {
                    this.xbox_mc$flush();
                    this.xbox_mc$runType = type;
                    this.xbox_mc$runPointer = pointer;
                    this.xbox_mc$runCount = part.elementCount();
                    this.xbox_mc$runBase = base;
                }
                this.xbox_mc$runEnd = this.xbox_mc$runPointer + (long) this.xbox_mc$runCount * type.getStride();
            }
            this.xbox_mc$flush();
        }

        boolean nonEmpty = false;
        for (MultiDrawBatch batch : this.batches) {
            batch.end();
            nonEmpty |= !batch.isEmpty();
        }
        cir.setReturnValue(nonEmpty);
    }

    private void xbox_mc$flush() {
        if (this.xbox_mc$runType != null && this.xbox_mc$runCount > 0) {
            this.batches[this.xbox_mc$runType.ordinal()].add(this.xbox_mc$runPointer, this.xbox_mc$runCount, this.xbox_mc$runBase);
        }
        this.xbox_mc$runType = null;
    }

    // same tests as sodium's buildDrawBatches
    private boolean xbox_mc$visible(ModelQuadFacing facing, ChunkCameraContext camera, ChunkRenderBounds bounds) {
        if (facing == ModelQuadFacing.UNASSIGNED || !this.isBlockFaceCullingEnabled) return true;
        switch (facing) {
            case UP: return camera.posY > bounds.y1;
            case DOWN: return camera.posY < bounds.y2;
            case EAST: return camera.posX > bounds.x1;
            case WEST: return camera.posX < bounds.x2;
            case SOUTH: return camera.posZ > bounds.z1;
            case NORTH: return camera.posZ < bounds.z2;
            default: return true;
        }
    }
}
