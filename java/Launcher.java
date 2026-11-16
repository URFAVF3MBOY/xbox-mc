// the launcher screen. logs in (shows the microsoft code on screen), checks the
// account owns minecraft, then shows the version switcher and PLAY. after PLAY it
// downloads that version + fabric and starts it in this same jvm.
// log goes to LocalState\launcher.log

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL15.*;
import static org.lwjgl.opengl.GL20.*;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.nio.file.Path;
import org.lwjgl.glfw.GLFWGamepadState;
import org.lwjgl.opengl.GL;
import org.lwjgl.system.MemoryStack;

public class Launcher {
    // the versions on offer. runtime = the java + lwjgl the app has to run with
    // for it (17: java 17 + lwjgl 3.2.2, 21: java 21 + lwjgl 3.3.3), folder = its
    // own game folder in LocalState so mods and settings don't mix.
    // switching to a version with the other runtime restarts the app
    static final String[][] VERSIONS = {
        { "1.17.1", "17", "game" },
        { "1.21.11", "21", "game-1.21.11" },
    };
    static volatile int selected;
    static volatile boolean ready, playPressed, restarting, restartFailed;
    static final Object playLock = new Object();

    static String version() { return VERSIONS[selected][0]; }
    static String runtime() { return System.getProperty("xmc.runtime", "17"); }

    static PrintStream log;

    // written by the worker thread, read by the render loop
    static volatile String status = "starting";
    static volatile String codeUrl, code;
    static volatile String error;
    static volatile String logUrl;    // the host's log web server, from logurl.txt
    static volatile String player;
    static volatile int done, total;
    static volatile long bytes;
    static volatile boolean finished;
    static volatile Auth.Account account;
    static volatile Downloader.Result files;
    static volatile Fabric.Result fabric;   // null = plain vanilla

    public static void main(String[] args) throws Exception {
        Path local = new File(System.getProperty("user.home")).toPath();
        Path install = new File(System.getProperty("xmc.install", ".")).toPath();
        log = new PrintStream(new FileOutputStream(local.resolve("launcher.log").toFile()), true, "UTF-8");
        System.setOut(log);
        System.setErr(log);
        loadSelection(local);
        log.println("xbox-mc launcher, java " + runtime() + ", picked minecraft " + version());
        startStartupProfile(local);

        Thread worker = new Thread(() -> work(local, install), "launcher");
        worker.setDaemon(true);
        worker.start();

        if (!glfwInit()) throw new IllegalStateException("glfwInit failed");
        long window = glfwCreateWindow(1280, 720, "xbox-mc", 0, 0);
        if (window == 0) throw new IllegalStateException("glfwCreateWindow failed");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        GL.createCapabilities();

        int program = program();
        int screen = glGetUniformLocation(program, "screen");
        int vbo = glGenBuffers();
        Font.Quads q = new Font.Quads();
        GLFWGamepadState pad = GLFWGamepadState.malloc();

        double readyAt = -1;
        int frame = 0;
        boolean wasLeft = false, wasRight = false, wasPlay = false;
        int ticks = 0;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // version switcher + play: d-pad / LB RB / arrow keys switch, A / enter plays
            boolean padOk = glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, pad);
            boolean left = glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS
                    || padOk && (pad.buttons(GLFW_GAMEPAD_BUTTON_DPAD_LEFT) == GLFW_PRESS || pad.buttons(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER) == GLFW_PRESS);
            boolean right = glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS
                    || padOk && (pad.buttons(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT) == GLFW_PRESS || pad.buttons(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER) == GLFW_PRESS);
            boolean play = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS
                    || padOk && pad.buttons(GLFW_GAMEPAD_BUTTON_A) == GLFW_PRESS;
            // LocalState\play.now = PLAY (testing on the pc without a controller)
            if (++ticks % 30 == 0) {
                try {
                    if (java.nio.file.Files.deleteIfExists(local.resolve("play.now"))) play = true;
                } catch (Exception e) {
                    // can't delete it, ignore
                }
            }
            boolean choosing = ready && !playPressed && !restarting && error == null;
            if (choosing) {
                if (left && !wasLeft) selected = (selected + VERSIONS.length - 1) % VERSIONS.length;
                if (right && !wasRight) selected = (selected + 1) % VERSIONS.length;
                if (play && !wasPlay) play(local);
            }
            wasLeft = left;
            wasRight = right;
            wasPlay = play;
            if (restarting && !restartFailed && java.nio.file.Files.exists(local.resolve("restart.failed"))) restartFailed = true;
            if (logUrl == null && frame++ % 60 == 0) {
                try {
                    logUrl = java.nio.file.Files.readString(local.resolve("logurl.txt")).trim();
                } catch (Exception e) {
                    // the host hasn't written it (yet)
                }
            }
            // show the green "downloaded" screen for a moment, then start the game
            if (finished && files != null) {
                if (readyAt < 0) readyAt = glfwGetTime();
                else if (glfwGetTime() - readyAt > 1.5) break;
            }
            int w, h;
            try (MemoryStack stack = MemoryStack.stackPush()) {
                var pw = stack.mallocInt(1);
                var ph = stack.mallocInt(1);
                glfwGetFramebufferSize(window, pw, ph);
                w = pw.get(0);
                h = ph.get(0);
            }
            glViewport(0, 0, w, h);

            boolean bad = error != null;
            if (bad) glClearColor(0.35f, 0.08f, 0.08f, 1);
            else if (finished) glClearColor(0.05f, 0.3f, 0.12f, 1);
            else glClearColor(0.09f, 0.09f, 0.11f, 1);
            glClear(GL_COLOR_BUFFER_BIT);

            q.clear();
            float s = Math.max(2, h / 180f);   // text pixel size, scales with the screen
            float cx = w / 2f;
            float y = h * 0.12f;
            Font.center(q, "XBOX-MC", cx, y, s * 2, 0.4f, 0.8f, 0.5f);
            y += s * 26;
            Font.center(q, "MINECRAFT JAVA " + version(), cx, y, s, 0.75f, 0.75f, 0.8f);
            y += s * 24;

            if (choosing || restarting) {
                // version switcher
                String label = "<   " + version() + "  FABRIC   >";
                float bw = Font.width(label, s * 1.5f) + s * 16, bh = s * 18;
                q.rect(cx - bw / 2, y, bw, bh, 0.16f, 0.16f, 0.2f);
                Font.center(q, label, cx, y + (bh - s * 1.5f * 7) / 2, s * 1.5f, 0.85f, 0.85f, 0.95f);
                y += bh + s * 8;
                // play button
                String pl = restarting ? "RESTARTING" : "PLAY";
                float pw = Math.max(bw * 0.6f, Font.width(pl, s * 2.5f) + s * 24), ph = s * 26;
                q.rect(cx - pw / 2 - s, y - s, pw + s * 2, ph + s * 2, 0.55f, 0.95f, 0.65f);
                q.rect(cx - pw / 2, y, pw, ph, 0.18f, 0.55f, 0.28f);
                Font.center(q, pl, cx, y + (ph - s * 2.5f * 7) / 2, s * 2.5f, 1, 1, 1);
                y += ph + s * 10;
                if (restartFailed) {
                    Font.center(q, "CLOSE THE APP AND OPEN IT AGAIN", cx, y, s, 1, 0.8f, 0.4f);
                    y += s * 12;
                    Font.center(q, "TO START " + version(), cx, y, s, 1, 0.8f, 0.4f);
                    y += s * 14;
                } else if (!restarting) {
                    Font.center(q, "A  PLAY      LB RB  VERSION", cx, y, s * 0.9f, 0.55f, 0.55f, 0.6f);
                    y += s * 14;
                    if (!VERSIONS[selected][1].equals(runtime())) {
                        Font.center(q, "THE APP RESTARTS FOR THIS VERSION", cx, y, s * 0.9f, 0.55f, 0.55f, 0.6f);
                        y += s * 14;
                    }
                }
            }

            String c = code;
            if (c != null) {
                Font.center(q, "ON YOUR PHONE OR PC GO TO", cx, y, s, 1, 1, 1);
                y += s * 12;
                Font.center(q, shortUrl(codeUrl), cx, y, s * 1.5f, 0.5f, 0.8f, 1);
                y += s * 18;
                Font.center(q, "AND ENTER THIS CODE", cx, y, s, 1, 1, 1);
                y += s * 14;
                Font.center(q, c, cx, y, s * 3, 1, 0.9f, 0.3f);
                y += s * 32;
            }

            if (!choosing && !restarting) {
                Font.center(q, status, cx, y, s, 0.85f, 0.85f, 0.9f);
                y += s * 14;
            }

            int t = total;
            if (t > 0 && !finished) {
                float bw = w * 0.5f, bh = s * 4;
                q.rect(cx - bw / 2, y, bw, bh, 0.25f, 0.25f, 0.3f);
                q.rect(cx - bw / 2, y, bw * done / t, bh, 0.4f, 0.8f, 0.5f);
                y += bh + s * 6;
                Font.center(q, done + " / " + t + " FILES  " + (bytes >> 20) + " MB", cx, y, s, 0.7f, 0.7f, 0.75f);
                y += s * 14;
            }
            if (player != null) {
                Font.center(q, "SIGNED IN AS " + player, cx, y, s, 0.6f, 1, 0.6f);
                y += s * 14;
            }
            if (bad) {
                for (String line : wrap(error, 60)) {
                    Font.center(q, line, cx, y, s, 1, 0.7f, 0.7f);
                    y += s * 12;
                }
            }
            if (logUrl != null) {
                Font.center(q, "LOGS: " + logUrl, cx, h - s * 16, s * 0.8f, 0.5f, 0.5f, 0.55f);
            }

            glUseProgram(program);
            glUniform2f(screen, w, h);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, java.util.Arrays.copyOf(q.data, q.n), GL_STREAM_DRAW);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0, 2, GL_FLOAT, false, 20, 0);
            glVertexAttribPointer(1, 3, GL_FLOAT, false, 20, 8);
            glDrawArrays(GL_TRIANGLES, 0, q.vertices());
            glfwSwapBuffers(window);

            // B closes the app (same as the home button would)
            if (padOk && pad.buttons(GLFW_GAMEPAD_BUTTON_B) == GLFW_PRESS && (finished || bad)) {
                glfwSetWindowShouldClose(window, true);
            }
        }
        glfwDestroyWindow(window);
        glfwTerminate();

        if (finished && files != null) startGame(local, account, files, fabric);
    }

    // runs minecraft in this same jvm (there can only be one per process), with
    // its jars in their own class loader. lwjgl core/glfw/opengl are already
    // loaded from the app's own jars (same 3.2.2), so those three are left out.
    // with fabric the main class is fabric's KnotClient, which finds the game
    // and its libraries through java.class.path, so that gets set to the same jars.
    // "record 60 s profile" on the log server's settings page creates jfr.now,
    // this records a flight recorder profile while playing and saves it as
    // LocalState\xbox.jfr (the log server offers it for download)
    // "profile the next startup" on the settings page leaves jfrstart.on behind.
    // then the first 3 minutes of this start get recorded as startup.jfr, same
    // samplers-only recording as the in-game profile
    static void startStartupProfile(Path local) {
        Path trigger = local.resolve("jfrstart.on");
        if (!java.nio.file.Files.exists(trigger)) return;
        try {
            java.nio.file.Files.deleteIfExists(trigger);
            jdk.jfr.Recording rec = new jdk.jfr.Recording();
            rec.enable("jdk.ExecutionSample").withPeriod(java.time.Duration.ofMillis(20));
            rec.enable("jdk.NativeMethodSample").withPeriod(java.time.Duration.ofMillis(20));
            rec.enable("jdk.GarbageCollection");
            rec.enable("jdk.GCPhasePause");
            rec.start();
            log.println("profile: recording the startup (3 min)");
            Thread t = new Thread(() -> {
                try {
                    Thread.sleep(180_000);
                    rec.stop();
                    rec.dump(local.resolve("startup.jfr"));
                    rec.close();
                    log.println("profile: saved startup.jfr");
                } catch (Throwable e) {
                    log.println("profile: " + e);
                }
            }, "startup profile");
            t.setDaemon(true);
            t.start();
        } catch (Throwable e) {
            log.println("profile: " + e);
        }
    }

    // LocalState\version.txt: the version picked last time
    static void loadSelection(Path local) {
        try {
            String v = java.nio.file.Files.readString(local.resolve("version.txt")).trim();
            for (int i = 0; i < VERSIONS.length; i++) if (VERSIONS[i][0].equals(v)) selected = i;
        } catch (Exception e) {
            // first start: the first version, or the one that fits this runtime
            for (int i = 0; i < VERSIONS.length; i++) if (VERSIONS[i][1].equals(runtime())) { selected = i; break; }
        }
    }

    // PLAY: same runtime -> the worker downloads and starts it. other runtime ->
    // runtime.txt for the host, then the host restarts the app (restart.now)
    static void play(Path local) {
        String[] v = VERSIONS[selected];
        try {
            java.nio.file.Files.writeString(local.resolve("version.txt"), v[0] + "\n");
            if (!v[1].equals(runtime())) {
                java.nio.file.Files.writeString(local.resolve("runtime.txt"), v[1] + "\n" + v[2] + "\n");
                java.nio.file.Files.writeString(local.resolve("restart.now"), "\n");
                restarting = true;
                setStatus("restarting for minecraft " + v[0]);
                return;
            }
        } catch (Exception e) {
            e.printStackTrace(log);
            error = "couldn't save the version: " + e;
            return;
        }
        synchronized (playLock) {
            playPressed = true;
            playLock.notifyAll();
        }
    }

    static void startProfileWatcher(Path local) {
        Thread t = new Thread(() -> {
            Path trigger = local.resolve("jfr.now");
            while (true) {
                try {
                    Thread.sleep(2000);
                    if (!java.nio.file.Files.exists(trigger)) continue;
                    java.nio.file.Files.deleteIfExists(trigger);
                    log.println("profile: recording 60 s");
                    // only the samplers + gc. jfr's periodic os events (cpu load,
                    // processes, network...) crash the jvm in the xbox sandbox
                    try (jdk.jfr.Recording rec = new jdk.jfr.Recording()) {
                        rec.enable("jdk.ExecutionSample").withPeriod(java.time.Duration.ofMillis(20));
                        rec.enable("jdk.NativeMethodSample").withPeriod(java.time.Duration.ofMillis(20));
                        rec.enable("jdk.GarbageCollection");
                        rec.enable("jdk.GCPhasePause");
                        rec.start();
                        Thread.sleep(60_000);
                        rec.stop();
                        rec.dump(local.resolve("xbox.jfr"));
                    }
                    log.println("profile: saved xbox.jfr");
                } catch (InterruptedException e) {
                    return;
                } catch (Throwable e) {
                    log.println("profile: " + e);
                }
            }
        }, "profile watcher");
        t.setDaemon(true);
        t.start();
    }

    static void startGame(Path local, Auth.Account a, Downloader.Result r, Fabric.Result fab) throws Exception {
        Path game = local.resolve(VERSIONS[selected][2]);
        // some mods (ferritecore) write config\... before anything made the folder
        java.nio.file.Files.createDirectories(game.resolve("config"));
        java.util.List<Path> jars = new java.util.ArrayList<>();
        if (fab != null) jars.addAll(fab.libraries);
        for (Path lib : r.libraries) {
            // lwjgl core/glfw/opengl come from the app (same version as the game's)
            String n = lib.getFileName().toString();
            if (n.matches("lwjgl(-glfw|-opengl)?-[0-9.]+\\.jar")) continue;
            // newer versions list their natives jars as libraries. lwjgl would unpack
            // the dlls from them into temp, and the xbox can't load dlls from there.
            // the app ships all of them in its natives folder instead
            if (n.contains("-natives-")) continue;
            jars.add(lib);
        }
        jars.add(r.clientJar);
        java.util.List<java.net.URL> urls = new java.util.ArrayList<>();
        StringBuilder cp = new StringBuilder();
        for (Path j : jars) {
            urls.add(j.toUri().toURL());
            if (cp.length() > 0) cp.append(java.io.File.pathSeparatorChar);
            cp.append(j);
        }
        // lwjgl is already loaded by the app (its natives can only be loaded once),
        // fabric normally refuses classes that aren't on its own class path
        System.setProperty("fabric.debug.disableClassPathIsolation", "true");

        String[] gameArgs = {
            "--username", a.name,
            "--version", version(),
            "--gameDir", game.toString(),
            "--assetsDir", r.assetsDir.toString(),
            "--assetIndex", r.assetIndex,
            "--uuid", a.uuid,
            "--accessToken", a.accessToken,
            "--userType", "msa",
            "--versionType", "release",
        };
        System.setProperty("minecraft.launcher.brand", "xbox-mc");
        System.setProperty("minecraft.launcher.version", "0.1");
        System.setProperty("java.class.path", cp.toString());
        System.setProperty("fabric.gameJarPath", r.clientJar.toString());

        String mainClass = fab != null ? fab.mainClass : (String) r.version.get("mainClass");
        startProfileWatcher(local);
        log.println(String.format("starting %s with %d jars (%.1f s)", mainClass, urls.size(),
                java.lang.management.ManagementFactory.getRuntimeMXBean().getUptime() / 1000.0));
        ClassLoader cl = new java.net.URLClassLoader(urls.toArray(new java.net.URL[0]), Launcher.class.getClassLoader());
        Thread.currentThread().setContextClassLoader(cl);
        Class<?> main = Class.forName(mainClass, true, cl);
        main.getMethod("main", String[].class).invoke(null, (Object) gameArgs);
        log.println("minecraft main returned");
    }

    static void work(Path local, Path install) {
        try {
            Auth auth = new Auth(local, install, new Auth.Ui() {
                public void status(String text) { setStatus(text); }
                public void code(String url, String c) { codeUrl = url; code = c; }
            }, log);
            Auth.Account a = auth.login();
            player = a.name;
            account = a;

            // wait for PLAY on the version screen
            ready = true;
            setStatus("ready");
            synchronized (playLock) {
                while (!playPressed) playLock.wait();
            }
            String mc = version();
            // downloads (libraries, assets, versions) are shared by all versions,
            // mods/config/saves live in the version's own folder
            Path root = local.resolve("game");
            Path gameDir = local.resolve(VERSIONS[selected][2]);

            Downloader d = new Downloader(root, new Downloader.Ui() {
                public void status(String text) { setStatus(text); }
                public void progress(int dn, int tt, long b) { done = dn; total = tt; bytes = b; }
            }, log);
            Downloader.Result r = d.get(mc);
            log.println("client " + r.clientJar + ", " + r.libraries.size() + " libraries, assets " + r.assetIndex);
            Fabric.defaultOptions(gameDir);

            // fabric + mods. if that part breaks the game still starts, just vanilla
            java.nio.file.Files.deleteIfExists(local.resolve("padmod.on"));
            try {
                Fabric f = new Fabric(root, gameDir, new Downloader.Ui() {
                    public void status(String text) { setStatus(text); }
                    public void progress(int dn, int tt, long b) { done = dn; total = tt; bytes = b; }
                }, log);
                Fabric.Result fr = f.loader(mc);
                f.mods(mc, install);
                if (mc.equals("1.17.1")) Fabric.tuneSettingsOnce(gameDir, log);
                fabric = fr;
                // tells glfw3.dll the game brings its own controller support
                Path padFlag = local.resolve("padmod.on");
                if (Fabric.hasPadMod(mc)) java.nio.file.Files.writeString(padFlag, mc + "\n");
                else java.nio.file.Files.deleteIfExists(padFlag);
            } catch (Exception e) {
                e.printStackTrace(log);
                log.println("fabric didn't work, starting vanilla");
            }
            files = r;
            setStatus("starting minecraft " + mc + (fabric != null ? " with fabric" : ""));
            finished = true;
        } catch (Throwable e) {
            e.printStackTrace(log);
            error = e.getMessage() == null ? e.toString() : e.getMessage();
            setStatus("something went wrong");
        }
    }

    static void setStatus(String s) {
        status = s;
        // seconds since the jvm started, to see where startup time goes
        double up = java.lang.management.ManagementFactory.getRuntimeMXBean().getUptime() / 1000.0;
        log.println(String.format("status: %s (%.1f s)", s, up));
    }

    static String shortUrl(String u) {
        if (u == null) return "";
        return u.replaceFirst("^https?://", "").replaceFirst("^www\\.", "");
    }

    static java.util.List<String> wrap(String text, int width) {
        java.util.List<String> lines = new java.util.ArrayList<>();
        StringBuilder line = new StringBuilder();
        for (String word : text.split("\\s+")) {
            if (line.length() + word.length() + 1 > width && line.length() > 0) {
                lines.add(line.toString());
                line.setLength(0);
            }
            if (line.length() > 0) line.append(' ');
            line.append(word.length() > width ? word.substring(0, width) : word);
        }
        if (line.length() > 0) lines.add(line.toString());
        return lines.size() > 6 ? lines.subList(0, 6) : lines;
    }

    static int program() {
        int vs = shader(GL_VERTEX_SHADER, "#version 120\n"
                + "attribute vec2 pos;\n"
                + "attribute vec3 col;\n"
                + "uniform vec2 screen;\n"
                + "varying vec3 v;\n"
                + "void main() {\n"
                + "  v = col;\n"
                + "  gl_Position = vec4(pos.x / screen.x * 2.0 - 1.0, 1.0 - pos.y / screen.y * 2.0, 0.0, 1.0);\n"
                + "}\n");
        int fs = shader(GL_FRAGMENT_SHADER, "#version 120\n"
                + "varying vec3 v;\n"
                + "void main() { gl_FragColor = vec4(v, 1.0); }\n");
        int p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glBindAttribLocation(p, 0, "pos");
        glBindAttribLocation(p, 1, "col");
        glLinkProgram(p);
        if (glGetProgrami(p, GL_LINK_STATUS) == 0) throw new IllegalStateException("link: " + glGetProgramInfoLog(p));
        return p;
    }

    static int shader(int type, String src) {
        int s = glCreateShader(type);
        glShaderSource(s, src);
        glCompileShader(s);
        if (glGetShaderi(s, GL_COMPILE_STATUS) == 0) throw new IllegalStateException("shader: " + glGetShaderInfoLog(s));
        return s;
    }
}
