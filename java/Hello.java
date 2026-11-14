// step 3 test: proves java runs inside the app.
// the app has no console, so everything goes to java.log in LocalState (user.home)

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.lang.management.CompilationMXBean;
import java.lang.management.ManagementFactory;
import java.nio.file.Files;
import java.nio.file.Path;

public class Hello {
    public static void main(String[] args) throws Exception {
        File log = new File(System.getProperty("user.home"), "java.log");
        PrintStream out = new PrintStream(new FileOutputStream(log), true, "UTF-8");
        System.setOut(out);
        System.setErr(out);

        out.println("hello from java on " + System.getProperty("os.name") + " " + System.getProperty("os.version"));
        for (String k : new String[] { "java.version", "java.vm.name", "java.vm.info", "java.home",
                "user.home", "user.dir", "java.io.tmpdir" }) {
            out.println(k + " = " + System.getProperty(k));
        }
        Runtime rt = Runtime.getRuntime();
        out.println("cpus = " + rt.availableProcessors());
        out.println("max heap = " + rt.maxMemory() / (1024 * 1024) + " MB");

        // something hot enough for the jit to pick up
        long t0 = System.nanoTime();
        long sum = 0;
        for (int round = 0; round < 50; round++) sum += work(2_000_000);
        long ms = (System.nanoTime() - t0) / 1_000_000;
        out.println("loop took " + ms + " ms (sum " + sum + ")");

        CompilationMXBean jit = ManagementFactory.getCompilationMXBean();
        if (jit == null) {
            out.println("jit: none (interpreter only)");
        } else {
            out.println("jit: " + jit.getName() + ", compile time " + jit.getTotalCompilationTime() + " ms");
        }

        // threads + temp files, minecraft needs both
        Thread t = new Thread(() -> out.println("second thread ok"));
        t.start();
        t.join();
        Path tmp = Files.createTempFile("xmc", ".txt");
        Files.writeString(tmp, "ok");
        out.println("temp file ok: " + Files.readString(tmp));
        Files.delete(tmp);

        // zip.dll (Inflater/Deflater), every jar minecraft reads goes through it
        byte[] data = "minecraft ".repeat(1000).getBytes("UTF-8");
        java.util.zip.Deflater def = new java.util.zip.Deflater();
        def.setInput(data);
        def.finish();
        byte[] packed = new byte[data.length];
        int plen = def.deflate(packed);
        def.end();
        java.util.zip.Inflater inf = new java.util.zip.Inflater();
        inf.setInput(packed, 0, plen);
        byte[] back = new byte[data.length];
        int blen = inf.inflate(back);
        inf.end();
        out.println("zip ok: " + data.length + " -> " + plen + " -> " + blen + " bytes, same = "
                + java.util.Arrays.equals(data, back));

        out.println("done");
    }

    static long work(int n) {
        long s = 0;
        for (int i = 0; i < n; i++) s += (i ^ (i >>> 3)) % 7;
        return s;
    }
}
