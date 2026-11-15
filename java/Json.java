// tiny json reader/writer, the jdk doesn't have one and I don't want a whole
// library in the package just for the login and the mojang manifests.
// objects -> Map, arrays -> List, numbers -> Double, plus String/Boolean/null

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class Json {
    private final String s;
    private int i;

    private Json(String s) { this.s = s; }

    public static Object parse(String text) {
        Json j = new Json(text);
        j.ws();
        Object v = j.value();
        j.ws();
        if (j.i != j.s.length()) throw j.err("trailing data");
        return v;
    }

    @SuppressWarnings("unchecked")
    public static Map<String, Object> obj(Object o) { return (Map<String, Object>) o; }

    @SuppressWarnings("unchecked")
    public static List<Object> arr(Object o) { return (List<Object>) o; }

    // walk a path like "DisplayClaims", "xui", 0, "uhs"
    public static Object get(Object o, Object... path) {
        for (Object p : path) {
            if (o == null) return null;
            if (p instanceof Integer) {
                List<Object> l = arr(o);
                int n = (Integer) p;
                o = n < l.size() ? l.get(n) : null;
            } else {
                o = obj(o).get((String) p);
            }
        }
        return o;
    }

    public static String str(Object o, Object... path) {
        Object v = get(o, path);
        return v == null ? null : v.toString();
    }

    public static String quote(String v) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : v.toCharArray()) {
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        return b.append('"').toString();
    }

    // writes Map/List/String/Number/Boolean/null back out
    public static String write(Object v) {
        if (v == null) return "null";
        if (v instanceof String) return quote((String) v);
        if (v instanceof Number || v instanceof Boolean) return v.toString();
        StringBuilder b = new StringBuilder();
        if (v instanceof Map) {
            b.append('{');
            boolean first = true;
            for (Map.Entry<?, ?> e : ((Map<?, ?>) v).entrySet()) {
                if (!first) b.append(',');
                first = false;
                b.append(quote(e.getKey().toString())).append(':').append(write(e.getValue()));
            }
            return b.append('}').toString();
        }
        if (v instanceof List) {
            b.append('[');
            boolean first = true;
            for (Object o : (List<?>) v) {
                if (!first) b.append(',');
                first = false;
                b.append(write(o));
            }
            return b.append(']').toString();
        }
        throw new IllegalArgumentException("can't write " + v.getClass());
    }

    private RuntimeException err(String what) {
        return new IllegalArgumentException("json: " + what + " at " + i);
    }

    private void ws() {
        while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++;
    }

    private Object value() {
        if (i >= s.length()) throw err("end of input");
        char c = s.charAt(i);
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return string();
        if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
        if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
        if (s.startsWith("null", i)) { i += 4; return null; }
        return number();
    }

    private Map<String, Object> object() {
        Map<String, Object> m = new LinkedHashMap<>();
        i++;
        ws();
        if (s.charAt(i) == '}') { i++; return m; }
        while (true) {
            ws();
            String k = string();
            ws();
            if (s.charAt(i++) != ':') throw err("expected :");
            ws();
            m.put(k, value());
            ws();
            char c = s.charAt(i++);
            if (c == '}') return m;
            if (c != ',') throw err("expected , or }");
        }
    }

    private List<Object> array() {
        List<Object> l = new ArrayList<>();
        i++;
        ws();
        if (s.charAt(i) == ']') { i++; return l; }
        while (true) {
            ws();
            l.add(value());
            ws();
            char c = s.charAt(i++);
            if (c == ']') return l;
            if (c != ',') throw err("expected , or ]");
        }
    }

    private String string() {
        if (s.charAt(i) != '"') throw err("expected string");
        i++;
        StringBuilder b = new StringBuilder();
        while (true) {
            char c = s.charAt(i++);
            if (c == '"') return b.toString();
            if (c != '\\') { b.append(c); continue; }
            char e = s.charAt(i++);
            switch (e) {
                case 'n': b.append('\n'); break;
                case 't': b.append('\t'); break;
                case 'r': b.append('\r'); break;
                case 'b': b.append('\b'); break;
                case 'f': b.append('\f'); break;
                case 'u': b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; break;
                default: b.append(e);
            }
        }
    }

    private Double number() {
        int start = i;
        while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
        if (start == i) throw err("unexpected character");
        return Double.parseDouble(s.substring(start, i));
    }
}
