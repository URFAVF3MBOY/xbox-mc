// step 4 test: an lwjgl window through my glfw3.dll, gl through mobileglues.
// green background + white triangle = it all works. left stick / arrow keys
// move the triangle, A / space changes the color. log goes to LocalState\java.log

import static org.lwjgl.glfw.GLFW.*;
import static org.lwjgl.opengl.GL11.*;
import static org.lwjgl.opengl.GL15.*;
import static org.lwjgl.opengl.GL20.*;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintStream;
import java.nio.FloatBuffer;
import org.lwjgl.Version;
import org.lwjgl.glfw.GLFWErrorCallback;
import org.lwjgl.glfw.GLFWGamepadState;
import org.lwjgl.opengl.GL;
import org.lwjgl.system.MemoryStack;

public class WindowTest {
    static PrintStream out;

    public static void main(String[] args) throws Exception {
        out = new PrintStream(new FileOutputStream(new File(System.getProperty("user.home"), "java.log")), true, "UTF-8");
        System.setOut(out);
        System.setErr(out);
        out.println("lwjgl " + Version.getVersion());

        GLFWErrorCallback.createPrint(out).set();
        if (!glfwInit()) throw new IllegalStateException("glfwInit failed");
        out.println("glfw " + glfwGetVersionString());

        // same hints minecraft 1.16.5 sets
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_NATIVE_CONTEXT_API);
        long window = glfwCreateWindow(854, 480, "xbox-mc", 0, 0);
        if (window == 0) throw new IllegalStateException("glfwCreateWindow failed");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        GL.createCapabilities();
        out.println("GL_VERSION " + glGetString(GL_VERSION));
        out.println("GL_RENDERER " + glGetString(GL_RENDERER));
        out.println("GL 2.0 caps: " + GL.getCapabilities().OpenGL20 + ", GL 3.2: " + GL.getCapabilities().OpenGL32);

        glfwSetKeyCallback(window, (w, key, scancode, action, mods) ->
                out.println("key " + key + " scancode " + scancode + " action " + action + " mods " + mods));
        glfwSetCharCallback(window, (w, c) -> out.println("char " + new String(Character.toChars(c))));
        glfwSetFramebufferSizeCallback(window, (w, width, height) -> out.println("framebuffer " + width + "x" + height));

        int program = program();
        int offset = glGetUniformLocation(program, "offset");
        int color = glGetUniformLocation(program, "color");
        int vbo = glGenBuffers();
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, new float[] { 0f, 0.3f, -0.3f, -0.25f, 0.3f, -0.25f }, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, false, 0, 0);
        int err = glGetError();
        out.println("setup gl error: " + err);

        float x = 0, y = 0;
        float[][] colors = { { 1, 1, 1 }, { 1, 0.8f, 0.2f }, { 0.4f, 0.7f, 1 }, { 1, 0.4f, 0.6f } };
        int colorIndex = 0;
        boolean wasA = false;
        int frames = 0;
        double last = glfwGetTime();
        GLFWGamepadState pad = GLFWGamepadState.malloc();
        boolean padSeen = false;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            float dx = 0, dy = 0;
            boolean a = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
            if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) dx -= 1;
            if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) dx += 1;
            if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) dy += 1;
            if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) dy -= 1;
            if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1) && glfwGetGamepadState(GLFW_JOYSTICK_1, pad)) {
                if (!padSeen) {
                    out.println("gamepad: " + glfwGetGamepadName(GLFW_JOYSTICK_1));
                    padSeen = true;
                }
                float lx = pad.axes(GLFW_GAMEPAD_AXIS_LEFT_X), ly = pad.axes(GLFW_GAMEPAD_AXIS_LEFT_Y);
                if (Math.abs(lx) > 0.15f) dx += lx;
                if (Math.abs(ly) > 0.15f) dy -= ly;
                a |= pad.buttons(GLFW_GAMEPAD_BUTTON_A) == GLFW_PRESS;
            }
            if (a && !wasA) {
                colorIndex = (colorIndex + 1) % colors.length;
                out.println("color " + colorIndex);
            }
            wasA = a;
            x = Math.max(-0.7f, Math.min(0.7f, x + dx * 0.02f));
            y = Math.max(-0.7f, Math.min(0.7f, y + dy * 0.02f));

            try (MemoryStack stack = MemoryStack.stackPush()) {
                var w = stack.mallocInt(1);
                var h = stack.mallocInt(1);
                glfwGetFramebufferSize(window, w, h);
                glViewport(0, 0, w.get(0), h.get(0));
            }
            glClearColor(0.0f, 0.6f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUseProgram(program);
            glUniform2f(offset, x, y);
            float[] c = colors[colorIndex];
            glUniform3f(color, c[0], c[1], c[2]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glfwSwapBuffers(window);

            frames++;
            double now = glfwGetTime();
            if (now - last >= 5.0) {
                out.println(String.format("%.1f fps", frames / (now - last)));
                frames = 0;
                last = now;
            }
        }
        out.println("window closed");
        glfwTerminate();
    }

    static int program() {
        int vs = shader(GL_VERTEX_SHADER, "#version 120\n"
                + "attribute vec2 pos;\n"
                + "uniform vec2 offset;\n"
                + "void main() { gl_Position = vec4(pos + offset, 0.0, 1.0); }\n");
        int fs = shader(GL_FRAGMENT_SHADER, "#version 120\n"
                + "uniform vec3 color;\n"
                + "void main() { gl_FragColor = vec4(color, 1.0); }\n");
        int p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glBindAttribLocation(p, 0, "pos");
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
