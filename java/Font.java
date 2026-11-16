// 5x7 pixel font so the launcher can show text (the login code mostly).
// uppercase, digits and a few symbols, every lit pixel becomes a quad.

import java.util.HashMap;
import java.util.Map;

public final class Font {
    static final Map<Character, String[]> glyphs = new HashMap<>();

    static void g(char c, String... rows) { glyphs.put(c, rows); }

    static {
        g('A', " ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #");
        g('B', "#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### ");
        g('C', " ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### ");
        g('D', "#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### ");
        g('E', "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####");
        g('F', "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    ");
        g('G', " ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####");
        g('H', "#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #");
        g('I', " ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### ");
        g('J', "  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  ");
        g('K', "#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #");
        g('L', "#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####");
        g('M', "#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #");
        g('N', "#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #");
        g('O', " ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### ");
        g('P', "#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    ");
        g('Q', " ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #");
        g('R', "#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #");
        g('S', " ####", "#    ", "#    ", " ### ", "    #", "    #", "#### ");
        g('T', "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ");
        g('U', "#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### ");
        g('V', "#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  ");
        g('W', "#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # ");
        g('X', "#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #");
        g('Y', "#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  ");
        g('Z', "#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####");
        g('0', " ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### ");
        g('1', "  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### ");
        g('2', " ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####");
        g('3', "#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### ");
        g('4', "   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # ");
        g('5', "#####", "#    ", "#### ", "    #", "    #", "#   #", " ### ");
        g('6', "  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### ");
        g('7', "#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   ");
        g('8', " ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### ");
        g('9', " ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  ");
        g('.', "     ", "     ", "     ", "     ", "     ", " ##  ", " ##  ");
        g(':', "     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     ");
        g('/', "     ", "    #", "   # ", "  #  ", " #   ", "#    ", "     ");
        g('-', "     ", "     ", "     ", "#####", "     ", "     ", "     ");
        g('%', "##   ", "##  #", "   # ", "  #  ", " #   ", "#  ##", "   ##");
        g('(', "   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # ");
        g(')', " #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   ");
        g('!', "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  ");
        g('?', " ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  ");
        g(',', "     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   ");
        g('\'', "  #  ", "  #  ", " #   ", "     ", "     ", "     ", "     ");
        g('_', "     ", "     ", "     ", "     ", "     ", "     ", "#####");
        g('=', "     ", "     ", "#####", "     ", "#####", "     ", "     ");
        g('+', "     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     ");
        g('<', "   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # ");
        g('>', " #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   ");
    }

    // width in pixels of text at this scale (6 columns per char incl. the gap)
    public static float width(String text, float scale) {
        return text.length() * 6 * scale - scale;
    }

    // appends quads (x, y, r, g, b per vertex, 2 triangles per lit pixel)
    public static void draw(Quads q, String text, float x, float y, float scale, float r, float g, float b) {
        text = text.toUpperCase();
        for (int n = 0; n < text.length(); n++) {
            String[] rows = glyphs.get(text.charAt(n));
            if (rows != null) {
                for (int row = 0; row < 7; row++) {
                    for (int col = 0; col < 5; col++) {
                        if (rows[row].charAt(col) == '#') {
                            q.rect(x + col * scale, y + row * scale, scale, scale, r, g, b);
                        }
                    }
                }
            }
            x += 6 * scale;
        }
    }

    public static void center(Quads q, String text, float cx, float y, float scale, float r, float g, float b) {
        draw(q, text, cx - width(text, scale) / 2, y, scale, r, g, b);
    }

    // growable float buffer of colored quads in pixel coords
    public static final class Quads {
        float[] data = new float[1 << 16];
        int n;

        public void clear() { n = 0; }

        public void rect(float x, float y, float w, float h, float r, float g, float b) {
            if (n + 30 > data.length) data = java.util.Arrays.copyOf(data, data.length * 2);
            v(x, y, r, g, b); v(x + w, y, r, g, b); v(x + w, y + h, r, g, b);
            v(x, y, r, g, b); v(x + w, y + h, r, g, b); v(x, y + h, r, g, b);
        }

        private void v(float x, float y, float r, float g, float b) {
            data[n++] = x; data[n++] = y; data[n++] = r; data[n++] = g; data[n++] = b;
        }

        public int vertices() { return n / 5; }
    }
}
