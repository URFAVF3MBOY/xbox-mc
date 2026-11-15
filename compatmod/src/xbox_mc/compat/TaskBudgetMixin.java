package xbox_mc.compat;

import java.util.Queue;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

// the client runs every queued task (mostly packets from the integrated server)
// once per frame, all of them. walking into new chunks queues a pile of chunk
// packets and each one is slow to unpack on the jaguar, so a frame could take
// over a second. this gives the queue a time budget per frame instead, the rest
// waits for the next frame (order stays the same). a big backlog gets more time
// so it can't fall behind for good.
//
// class_1255 = ThreadExecutor, method_5383 = runTasks, method_16075 = runTask,
// field_5750 = the task queue. only the client (class_310) is changed, the
// integrated server uses the same class and keeps the vanilla behaviour.
@Mixin(targets = "net.minecraft.class_1255", remap = false)
public abstract class TaskBudgetMixin {
    private static final long BUDGET_NS = 4_000_000L;
    private static final long BACKLOG_BUDGET_NS = 12_000_000L;
    private static final int BACKLOG = 400;

    @Shadow @Final private Queue<?> field_5750;

    @Shadow public abstract boolean method_16075();

    @Inject(method = "method_5383()V", at = @At("HEAD"), cancellable = true)
    private void xmc$budgetTasks(CallbackInfo ci) {
        if (!"net.minecraft.class_310".equals(this.getClass().getName())) return;
        long budget = field_5750.size() > BACKLOG ? BACKLOG_BUDGET_NS : BUDGET_NS;
        long end = System.nanoTime() + budget;
        // always at least one task, so nothing can stall completely
        while (this.method_16075() && System.nanoTime() < end) { }
        ci.cancel();
    }
}
