// step 5: the launcher screen. logs in (shows the microsoft code on screen),
// checks the account owns minecraft, then downloads minecraft and starts it.
// the game itself starts in step 6. log goes to LocalState\launcher.log

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
    static final String VERSION = "1.17.1";

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
        log.println("xbox-mc launcher, minecraft " + VERSION);

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
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
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
            Font.center(q, "MINECRAFT JAVA " + VERSION, cx, y, s, 0.75f, 0.75f, 0.8f);
            y += s * 24;

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

            Font.center(q, status, cx, y, s, 0.85f, 0.85f, 0.9f);
            y += s * 14;

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
            if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, pad)
                    && pad.buttons(GLFW_GAMEPAD_BUTTON_B) == GLFW_PRESS && (finished || bad)) {
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
    static void startGame(Path local, Auth.Account a, Downloader.Result r, Fabric.Result fab) throws Exception {
        Path game = local.resolve("game");
        // some mods (ferritecore) write config\... before anything made the folder
        java.nio.file.Files.createDirectories(game.resolve("config"));
        java.util.List<Path> jars = new java.util.ArrayList<>();
        if (fab != null) jars.addAll(fab.libraries);
        for (Path lib : r.libraries) {
            String n = lib.getFileName().toString();
            if (n.equals("lwjgl-3.2.2.jar") || n.equals("lwjgl-glfw-3.2.2.jar") || n.equals("lwjgl-opengl-3.2.2.jar")) continue;
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
            "--version", VERSION,
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
        log.println("starting " + mainClass + " with " + urls.size() + " jars");
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

            Downloader d = new Downloader(local.resolve("game"), new Downloader.Ui() {
                public void status(String text) { setStatus(text); }
                public void progress(int dn, int tt, long b) { done = dn; total = tt; bytes = b; }
            }, log);
            Downloader.Result r = d.get(VERSION);
            log.println("client " + r.clientJar + ", " + r.libraries.size() + " libraries, assets " + r.assetIndex);
            Fabric.defaultOptions(local.resolve("game"));

            // fabric + mods. if that part breaks the game still starts, just vanilla
            try {
                Fabric f = new Fabric(local.resolve("game"), new Downloader.Ui() {
                    public void status(String text) { setStatus(text); }
                    public void progress(int dn, int tt, long b) { done = dn; total = tt; bytes = b; }
                }, log);
                Fabric.Result fr = f.loader(VERSION);
                f.mods(VERSION, install);
                Fabric.tuneSettingsOnce(local.resolve("game"), log);
                fabric = fr;
            } catch (Exception e) {
                e.printStackTrace(log);
                log.println("fabric didn't work, starting vanilla");
            }
            files = r;
            setStatus("starting minecraft " + VERSION + (fabric != null ? " with fabric" : ""));
            finished = true;
        } catch (Throwable e) {
            e.printStackTrace(log);
            error = e.getMessage() == null ? e.toString() : e.getMessage();
            setStatus("something went wrong");
        }
    }

    static void setStatus(String s) {
        status = s;
        log.println("status: " + s);
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
