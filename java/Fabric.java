// fabric for the downloaded minecraft: the loader + its libraries from fabric's
// meta server, a few performance mods from modrinth, and low default settings
// for the first start. everything sha1 checked like the game files.

import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class Fabric {
    static final String META = "https://meta.fabricmc.net/v2/versions/loader/";
    static final String MODRINTH = "https://api.modrinth.com/v2/project/";

    // the mods that get installed and kept up to date. all client side, about
    // speed, memory or settings, nothing that changes the game itself.
    // sodium needs my xbox-mc-compat mod on the xbox one (its chunk shader uses a
    // uniform block, the xbox one's gpu driver can't link those)
    static final String[] MODS = { "fabric-api", "sodium", "sodium-extra", "reeses-sodium-options",
            "lithium", "lazydfu", "ferrite-core", "starlight", "entityculling" };

    public static final class Result {
        public String mainClass;
        public List<Path> libraries = new ArrayList<>();
        public String loaderVersion;
    }

    private final Path game;
    private final Downloader.Ui ui;
    private final java.io.PrintStream log;

    public Fabric(Path game, Downloader.Ui ui, java.io.PrintStream log) {
        this.game = game;
        this.ui = ui;
        this.log = log;
    }

    public Result loader(String mc) throws Exception {
        ui.status("looking up fabric");
        String loaderVer = null;
        for (Object v : Json.arr(Auth.getJson(META + mc, null))) {
            if (Boolean.TRUE.equals(Json.get(v, "loader", "stable"))) {
                loaderVer = Json.str(v, "loader", "version");
                break;
            }
        }
        if (loaderVer == null) throw new IOException("fabric has no loader for " + mc);
        Object profile = Auth.getJson(META + mc + "/" + loaderVer + "/profile/json", null);

        Result r = new Result();
        r.loaderVersion = loaderVer;
        r.mainClass = Json.str(profile, "mainClass");
        List<Downloader.Item> items = new ArrayList<>();
        for (Object lib : Json.arr(Json.get(profile, "libraries"))) {
            String name = Json.str(lib, "name");
            String base = Json.str(lib, "url");
            if (base == null) base = "https://maven.fabricmc.net/";
            if (!base.endsWith("/")) base += "/";
            String path = mavenPath(name);
            String sha1 = Json.str(lib, "sha1");
            if (sha1 == null) sha1 = fetchText(base + path + ".sha1").trim().split("\\s+")[0];
            Object sz = Json.get(lib, "size");
            long size = sz == null ? -1 : ((Number) sz).longValue();
            Path file = game.resolve("libraries/" + path);
            items.add(new Downloader.Item(base + path, sha1, size, file));
            r.libraries.add(file);
        }
        new Downloader(game, ui, log).fetchAll(items, "downloading fabric " + loaderVer);
        log.println("fabric " + loaderVer + ", " + r.libraries.size() + " libraries, main " + r.mainClass);
        return r;
    }

    // installs/updates my mod list in game\mods. mods dropped in by hand stay
    public void mods(String mc, Path install) throws Exception {
        ui.status("checking mods");
        Path dir = game.resolve("mods");
        Files.createDirectories(dir);

        // my own mods ship with the app (install\mods), copied every start so
        // they're always the version that belongs to this build
        Path bundled = install.resolve("mods");
        if (Files.isDirectory(bundled)) {
            try (java.util.stream.Stream<Path> files = Files.list(bundled)) {
                for (Path jar : (Iterable<Path>) files::iterator) {
                    Files.copy(jar, dir.resolve(jar.getFileName()), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                    log.println("bundled mod " + jar.getFileName());
                }
            }
        }
        Path listFile = dir.resolve(".xbox-mc-mods.json");
        Map<String, Object> installed = Files.exists(listFile)
                ? Json.obj(Json.parse(Files.readString(listFile))) : new LinkedHashMap<>();

        List<Downloader.Item> items = new ArrayList<>();
        Map<String, Object> now = new LinkedHashMap<>();
        for (String slug : MODS) {
            Object file;
            try {
                file = latestFile(slug, mc);
            } catch (Exception e) {
                // modrinth down or no build for this version: keep what's there
                log.println("mod " + slug + ": " + e.getMessage());
                if (installed.containsKey(slug)) now.put(slug, installed.get(slug));
                continue;
            }
            if (file == null) {
                log.println("mod " + slug + ": no fabric build for " + mc);
                continue;
            }
            String name = Json.str(file, "filename");
            Object sz = Json.get(file, "size");
            items.add(new Downloader.Item(Json.str(file, "url"), Json.str(file, "hashes", "sha1"),
                    sz == null ? -1 : ((Number) sz).longValue(), dir.resolve(name)));
            now.put(slug, name);
            Object old = installed.get(slug);
            if (old != null && !old.equals(name)) Files.deleteIfExists(dir.resolve((String) old));
        }
        new Downloader(game, ui, log).fetchAll(items, "downloading mods");
        // mods I took off the list get removed again
        for (Map.Entry<String, Object> e : installed.entrySet()) {
            if (!now.containsKey(e.getKey()) && java.util.Arrays.asList(MODS).indexOf(e.getKey()) < 0) {
                Files.deleteIfExists(dir.resolve((String) e.getValue()));
                log.println("mod " + e.getKey() + " removed");
            }
        }
        Files.writeString(listFile, Json.write(now));
        log.println("mods: " + now.values());
    }

    private Object latestFile(String slug, String mc) throws Exception {
        String q = "?loaders=" + URLEncoder.encode("[\"fabric\"]", StandardCharsets.UTF_8)
                + "&game_versions=" + URLEncoder.encode("[\"" + mc + "\"]", StandardCharsets.UTF_8);
        HttpRequest req = HttpRequest.newBuilder(URI.create(MODRINTH + slug + "/version" + q))
                .header("User-Agent", "xbox-mc/0.1")
                .timeout(Duration.ofSeconds(30)).GET().build();
        Object versions = Auth.check(Auth.send(req));
        // newest first; prefer a release over a beta
        Object pick = null;
        for (Object v : Json.arr(versions)) {
            if ("release".equals(Json.str(v, "version_type"))) { pick = v; break; }
            if (pick == null) pick = v;
        }
        if (pick == null) return null;
        Object primary = null;
        for (Object f : Json.arr(Json.get(pick, "files"))) {
            if (primary == null || Boolean.TRUE.equals(Json.get(f, "primary"))) primary = f;
        }
        return primary;
    }

    // low settings for the first start only, the xbox one cpu is slow.
    // after that options.txt belongs to the player
    public static void defaultOptions(Path game) throws IOException {
        Path f = game.resolve("options.txt");
        if (Files.exists(f)) return;
        Files.createDirectories(game);
        Files.writeString(f, String.join("\n",
                "renderDistance:6",
                "graphicsMode:0",
                "renderClouds:\"false\"",
                "particles:2",
                "entityShadows:false",
                "biomeBlendRadius:0",
                "mipmapLevels:0",
                "maxFps:260",
                "enableVsync:false",
                "entityDistanceScaling:0.75",
                "") );
    }

    // one time changes for installs from before they were the default, done once
    // (marker file), after that the settings belong to the player again:
    // - sodium block face culling off: with xbox-mc-compat every chunk is then one
    //   draw call instead of up to 7 (much less cpu on the xbox, gpu has room)
    // - no 60 fps cap and no vsync (the first-start defaults had both)
    public static void tuneSettingsOnce(Path game, java.io.PrintStream log) {
        tuneCullingOnce(game, log);
        Path marker = game.resolve(".xbox-mc-tuned-1");
        if (Files.exists(marker)) return;
        try {
            Path sodium = game.resolve("config/sodium-options.json");
            Map<String, Object> root = Files.exists(sodium) ? Json.obj(Json.parse(Files.readString(sodium))) : new LinkedHashMap<>();
            Object perf = root.get("performance");
            Map<String, Object> p = perf == null ? new LinkedHashMap<>() : Json.obj(perf);
            p.put("use_block_face_culling", Boolean.FALSE);
            root.put("performance", p);
            Files.createDirectories(sodium.getParent());
            Files.writeString(sodium, Json.write(root));

            Path options = game.resolve("options.txt");
            if (Files.exists(options)) {
                List<String> lines = new ArrayList<>();
                for (String l : Files.readAllLines(options)) {
                    if (l.startsWith("maxFps:")) l = "maxFps:260";
                    else if (l.startsWith("enableVsync:")) l = "enableVsync:false";
                    lines.add(l);
                }
                Files.write(options, lines);
            }
            Files.writeString(marker, "done\n");
            log.println("settings tuned once: no block face culling, no fps cap, no vsync");
        } catch (Exception e) {
            log.println("couldn't tune settings: " + e);
        }
    }

    // entityculling re-traces every entity every 10 ms on its own thread, which
    // kept a whole jaguar core busy on the xbox. 40 ms still looks fine.
    // the mod writes its config on the first start, so this waits until it's there.
    static void tuneCullingOnce(Path game, java.io.PrintStream log) {
        Path marker = game.resolve(".xbox-mc-tuned-2");
        Path cfg = game.resolve("config/entityculling.json");
        if (Files.exists(marker) || !Files.exists(cfg)) return;
        try {
            Map<String, Object> root = Json.obj(Json.parse(Files.readString(cfg)));
            // my json parser reads every number as a double, keep whole ones whole
            for (Map.Entry<String, Object> e : root.entrySet()) {
                if (e.getValue() instanceof Double d && d == Math.rint(d)) e.setValue(d.longValue());
            }
            root.put("sleepDelay", 40L);
            Files.writeString(cfg, Json.write(root));
            Files.writeString(marker, "done\n");
            log.println("settings tuned once: entityculling sleepDelay 40");
        } catch (Exception e) {
            log.println("couldn't tune entityculling: " + e);
        }
    }

    static String mavenPath(String coords) {
        String[] p = coords.split(":");
        String group = p[0].replace('.', '/'), artifact = p[1], version = p[2];
        String classifier = p.length > 3 ? "-" + p[3] : "";
        return group + "/" + artifact + "/" + version + "/" + artifact + "-" + version + classifier + ".jar";
    }

    static String fetchText(String url) throws Exception {
        HttpResponse<String> r = Auth.send(HttpRequest.newBuilder(URI.create(url)).timeout(Duration.ofSeconds(30)).GET().build());
        if (r.statusCode() != 200) throw new IOException(url + " said " + r.statusCode());
        return r.body();
    }
}
