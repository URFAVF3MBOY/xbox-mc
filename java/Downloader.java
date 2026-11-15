// downloads minecraft 1.16.5 from mojang into LocalState\game:
// version json, client jar, libraries (windows ones), asset index + objects.
// everything is sha1 checked, files that are already there and match get skipped.
// only after login, the files come from mojang's servers, nothing is in the package.

import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public final class Downloader {
    static final String MANIFEST = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
    static final String RESOURCES = "https://resources.download.minecraft.net/";

    public interface Ui {
        void status(String text);
        void progress(int done, int total, long bytes);
    }

    static final class Item {
        final String url, sha1;
        final Path file;
        final long size;
        Item(String url, String sha1, long size, Path file) {
            this.url = url; this.sha1 = sha1; this.size = size; this.file = file;
        }
    }

    // what the launch needs afterwards
    public static final class Result {
        public Map<String, Object> version;
        public Path clientJar;
        public List<Path> libraries = new ArrayList<>();
        public Path assetsDir;
        public String assetIndex;
    }

    private final Path game;
    private final Ui ui;
    private final java.io.PrintStream log;

    public Downloader(Path game, Ui ui, java.io.PrintStream log) {
        this.game = game;
        this.ui = ui;
        this.log = log;
    }

    public Result get(String id) throws Exception {
        ui.status("looking up minecraft " + id);
        Object manifest = Auth.getJson(MANIFEST, null);
        Object entry = null;
        for (Object v : Json.arr(Json.get(manifest, "versions"))) {
            if (id.equals(Json.str(v, "id"))) entry = v;
        }
        if (entry == null) throw new IOException("mojang has no version " + id);

        Path versionJson = game.resolve("versions/" + id + "/" + id + ".json");
        fetch(new Item(Json.str(entry, "url"), Json.str(entry, "sha1"), -1, versionJson));
        Result r = new Result();
        r.version = Json.obj(Json.parse(Files.readString(versionJson)));

        List<Item> items = new ArrayList<>();
        Object client = Json.get(r.version, "downloads", "client");
        r.clientJar = game.resolve("versions/" + id + "/" + id + ".jar");
        items.add(new Item(Json.str(client, "url"), Json.str(client, "sha1"), size(client), r.clientJar));

        for (Object lib : Json.arr(r.version.get("libraries"))) {
            if (!allowed(Json.get(lib, "rules"))) continue;
            Object art = Json.get(lib, "downloads", "artifact");
            // natives-windows jars are skipped, the app has its own lwjgl natives
            if (art == null) continue;
            Path p = game.resolve("libraries/" + Json.str(art, "path"));
            items.add(new Item(Json.str(art, "url"), Json.str(art, "sha1"), size(art), p));
            r.libraries.add(p);
        }

        Object ai = r.version.get("assetIndex");
        r.assetIndex = Json.str(ai, "id");
        r.assetsDir = game.resolve("assets");
        Path indexFile = r.assetsDir.resolve("indexes/" + r.assetIndex + ".json");
        fetch(new Item(Json.str(ai, "url"), Json.str(ai, "sha1"), size(ai), indexFile));
        Object index = Json.parse(Files.readString(indexFile));
        for (Map.Entry<String, Object> e : Json.obj(Json.get(index, "objects")).entrySet()) {
            String hash = Json.str(e.getValue(), "hash");
            String sub = hash.substring(0, 2) + "/" + hash;
            items.add(new Item(RESOURCES + sub, hash, size(e.getValue()), r.assetsDir.resolve("objects/" + sub)));
        }

        fetchAll(items);
        return r;
    }

    static long size(Object o) {
        Object s = Json.get(o, "size");
        return s == null ? -1 : ((Number) s).longValue();
    }

    // mojang's library rules, only windows matters here
    static boolean allowed(Object rules) {
        if (rules == null) return true;
        boolean ok = false;
        for (Object rule : Json.arr(rules)) {
            String os = Json.str(rule, "os", "name");
            if (os == null || os.equals("windows")) ok = "allow".equals(Json.str(rule, "action"));
        }
        return ok;
    }

    private void fetchAll(List<Item> items) throws Exception {
        List<Item> todo = new ArrayList<>();
        ui.status("checking files");
        for (Item it : items) if (!valid(it)) todo.add(it);
        log.println(items.size() + " files, " + todo.size() + " to download");
        if (todo.isEmpty()) return;

        ui.status("downloading minecraft");
        AtomicInteger done = new AtomicInteger();
        AtomicLong bytes = new AtomicLong();
        // a few at a time, thousands of tiny asset files at once just choke
        ExecutorService pool = Executors.newFixedThreadPool(6);
        try {
            List<Future<?>> jobs = new ArrayList<>();
            for (Item it : todo) {
                jobs.add(pool.submit(() -> {
                    Exception last = null;
                    for (int attempt = 0; attempt < 3; attempt++) {
                        try {
                            fetch(it);
                            bytes.addAndGet(Files.size(it.file));
                            ui.progress(done.incrementAndGet(), todo.size(), bytes.get());
                            return null;
                        } catch (Exception e) {
                            last = e;
                        }
                    }
                    throw last;
                }));
            }
            for (Future<?> f : jobs) f.get();
        } finally {
            pool.shutdownNow();
        }
    }

    private boolean valid(Item it) throws IOException {
        if (!Files.exists(it.file)) return false;
        if (it.size >= 0 && Files.size(it.file) != it.size) return false;
        return it.sha1 == null || it.sha1.equalsIgnoreCase(sha1(it.file));
    }

    private void fetch(Item it) throws Exception {
        if (valid(it)) return;
        Files.createDirectories(it.file.getParent());
        Path tmp = it.file.resolveSibling(it.file.getFileName() + ".part");
        HttpRequest req = HttpRequest.newBuilder(URI.create(it.url)).timeout(Duration.ofMinutes(5)).GET().build();
        HttpResponse<InputStream> r = Auth.http.send(req, HttpResponse.BodyHandlers.ofInputStream());
        if (r.statusCode() != 200) throw new IOException(it.url + " said " + r.statusCode());
        try (InputStream in = r.body()) {
            Files.copy(in, tmp, StandardCopyOption.REPLACE_EXISTING);
        }
        String got = sha1(tmp);
        if (it.sha1 != null && !it.sha1.equalsIgnoreCase(got)) {
            Files.deleteIfExists(tmp);
            throw new IOException("sha1 mismatch for " + it.url);
        }
        // plain move, ATOMIC_MOVE isn't always allowed in the app folder
        Files.move(tmp, it.file, StandardCopyOption.REPLACE_EXISTING);
    }

    static String sha1(Path p) throws IOException {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-1");
            byte[] buf = new byte[1 << 16];
            try (InputStream in = Files.newInputStream(p)) {
                int n;
                while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            }
            StringBuilder b = new StringBuilder();
            for (byte x : md.digest()) b.append(String.format("%02x", x));
            return b.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException(e);
        }
    }
}
