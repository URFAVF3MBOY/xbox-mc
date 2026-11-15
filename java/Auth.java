// microsoft login for minecraft java:
// device code -> xbox live -> xsts -> minecraft services -> check the account
// owns the game -> profile. no offline mode, if any step fails there's no game.
//
// the client id comes from client_id.txt (LocalState first, then the package).
// it's not in the repo, see NOTES.md. a guid means an azure app id
// (login.microsoftonline.com), the old 16 digit ids go through login.live.com.
// the refresh token is kept in LocalState\account.json so the code only has to
// be typed in once.

import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class Auth {
    public static final class Account {
        public String name, uuid, accessToken, xuid;
    }

    // what the screen shows while logging in
    public interface Ui {
        void status(String text);
        void code(String url, String code);
    }

    static final HttpClient http = HttpClient.newBuilder()
            .connectTimeout(Duration.ofSeconds(20))
            .followRedirects(HttpClient.Redirect.NORMAL)
            .build();

    private final Path local;
    private final String clientId;
    private final boolean azure;
    private final Ui ui;
    private final java.io.PrintStream log;

    public Auth(Path local, Path install, Ui ui, java.io.PrintStream log) throws IOException {
        this.local = local;
        this.ui = ui;
        this.log = log;
        String id = null;
        for (Path p : new Path[] { local.resolve("client_id.txt"), install.resolve("client_id.txt") }) {
            if (Files.exists(p)) {
                id = Files.readString(p).trim();
                log.println("client id from " + p);
                break;
            }
        }
        if (id == null || id.isEmpty()) throw new IOException("no client id (client_id.txt is missing)");
        clientId = id;
        azure = id.contains("-");
        log.println("login flow: " + (azure ? "azure (microsoftonline)" : "live.com"));
    }

    public Account login() throws Exception {
        Map<String, Object> msa = null;
        Path saved = local.resolve("account.json");
        if (Files.exists(saved)) {
            try {
                Map<String, Object> s = Json.obj(Json.parse(Files.readString(saved)));
                if (clientId.equals(s.get("client_id"))) {
                    ui.status("signing in again");
                    msa = refresh((String) s.get("refresh_token"));
                }
            } catch (Exception e) {
                log.println("saved login didn't work: " + e);
            }
        }
        if (msa == null) msa = deviceCode();

        Map<String, Object> keep = new LinkedHashMap<>();
        keep.put("client_id", clientId);
        keep.put("refresh_token", msa.get("refresh_token"));
        Files.writeString(saved, Json.write(keep));

        String msToken = (String) msa.get("access_token");

        ui.status("xbox live");
        Map<String, Object> xblBody = new LinkedHashMap<>();
        Map<String, Object> props = new LinkedHashMap<>();
        props.put("AuthMethod", "RPS");
        props.put("SiteName", "user.auth.xboxlive.com");
        props.put("RpsTicket", (azure ? "d=" : "t=") + msToken);
        xblBody.put("Properties", props);
        xblBody.put("RelyingParty", "http://auth.xboxlive.com");
        xblBody.put("TokenType", "JWT");
        Object xbl = postJson("https://user.auth.xboxlive.com/user/authenticate", xblBody, null);
        String xblToken = Json.str(xbl, "Token");
        String uhs = Json.str(xbl, "DisplayClaims", "xui", 0, "uhs");

        ui.status("xbox live (xsts)");
        Map<String, Object> xstsBody = new LinkedHashMap<>();
        Map<String, Object> xprops = new LinkedHashMap<>();
        xprops.put("SandboxId", "RETAIL");
        xprops.put("UserTokens", List.of(xblToken));
        xstsBody.put("Properties", xprops);
        xstsBody.put("RelyingParty", "rp://api.minecraftservices.com/");
        xstsBody.put("TokenType", "JWT");
        HttpResponse<String> xr = send(jsonRequest("https://xsts.auth.xboxlive.com/xsts/authorize", xstsBody, null));
        if (xr.statusCode() == 401) {
            String x = Json.str(Json.parse(xr.body()), "XErr");
            throw new IOException(xstsError(x));
        }
        Object xsts = check(xr);
        String xstsToken = Json.str(xsts, "Token");

        ui.status("minecraft login");
        Map<String, Object> mcBody = new LinkedHashMap<>();
        mcBody.put("identityToken", "XBL3.0 x=" + uhs + ";" + xstsToken);
        HttpResponse<String> mr = send(jsonRequest("https://api.minecraftservices.com/authentication/login_with_xbox", mcBody, null));
        if (mr.statusCode() == 403) {
            throw new IOException("minecraft rejected the app id (needs approval from mojang)");
        }
        Object mc = check(mr);
        String mcToken = Json.str(mc, "access_token");

        ui.status("checking the account owns minecraft");
        Object ent = getJson("https://api.minecraftservices.com/entitlements/mcstore", mcToken);
        boolean owns = false;
        Object items = Json.get(ent, "items");
        if (items != null) {
            for (Object it : Json.arr(items)) {
                String n = Json.str(it, "name");
                if ("game_minecraft".equals(n) || "product_minecraft".equals(n)) owns = true;
            }
        }
        if (!owns) throw new IOException("this account doesn't own minecraft java edition");

        HttpResponse<String> pr = send(HttpRequest.newBuilder(URI.create("https://api.minecraftservices.com/minecraft/profile"))
                .header("Authorization", "Bearer " + mcToken).GET().build());
        if (pr.statusCode() == 404) throw new IOException("no minecraft profile yet, make one on minecraft.net first");
        Object profile = check(pr);

        Account a = new Account();
        a.name = Json.str(profile, "name");
        a.uuid = Json.str(profile, "id");
        a.accessToken = mcToken;
        a.xuid = Json.str(xbl, "DisplayClaims", "xui", 0, "xid");
        log.println("logged in as " + a.name);
        return a;
    }

    private Map<String, Object> deviceCode() throws Exception {
        ui.status("getting a login code");
        Object dc;
        if (azure) {
            dc = postForm("https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode",
                    "client_id", clientId, "scope", "XboxLive.signin offline_access");
        } else {
            dc = postForm("https://login.live.com/oauth20_connect.srf",
                    "client_id", clientId, "scope", "service::user.auth.xboxlive.com::MBI_SSL",
                    "response_type", "device_code");
        }
        String code = Json.str(dc, "user_code");
        String url = Json.str(dc, "verification_uri");
        String device = Json.str(dc, "device_code");
        Object iv = Json.get(dc, "interval");
        long interval = iv == null ? 5 : ((Number) iv).longValue();
        Object ex = Json.get(dc, "expires_in");
        long deadline = System.currentTimeMillis() + (ex == null ? 900 : ((Number) ex).longValue()) * 1000;
        ui.code(url, code);
        ui.status("waiting for the code to be entered");

        String tokenUrl = azure ? "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"
                : "https://login.live.com/oauth20_token.srf";
        while (System.currentTimeMillis() < deadline) {
            Thread.sleep(interval * 1000);
            HttpResponse<String> r = send(formRequest(tokenUrl,
                    "client_id", clientId, "device_code", device,
                    "grant_type", "urn:ietf:params:oauth:grant-type:device_code"));
            Object body = Json.parse(r.body());
            String err = Json.str(body, "error");
            if (err == null && r.statusCode() == 200) {
                ui.code(null, null);
                return Json.obj(body);
            }
            if ("authorization_pending".equals(err)) continue;
            if ("slow_down".equals(err)) { interval += 5; continue; }
            throw new IOException("login failed: " + err + " " + Json.str(body, "error_description"));
        }
        throw new IOException("the login code ran out, restart to get a new one");
    }

    private Map<String, Object> refresh(String token) throws Exception {
        String url = azure ? "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"
                : "https://login.live.com/oauth20_token.srf";
        String scope = azure ? "XboxLive.signin offline_access" : "service::user.auth.xboxlive.com::MBI_SSL";
        return Json.obj(check(send(formRequest(url, "client_id", clientId, "refresh_token", token,
                "grant_type", "refresh_token", "scope", scope))));
    }

    static String xstsError(String code) {
        if ("2148916233".equals(code)) return "this microsoft account has no xbox profile yet";
        if ("2148916235".equals(code)) return "xbox live isn't available in this account's country";
        if ("2148916236".equals(code) || "2148916237".equals(code)) return "this account needs adult verification (korea)";
        if ("2148916238".equals(code)) return "child account, it has to be added to a family by an adult";
        return "xsts said no (" + code + ")";
    }

    // ---- http ----

    static HttpRequest formRequest(String url, String... kv) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < kv.length; i += 2) {
            if (b.length() > 0) b.append('&');
            b.append(URLEncoder.encode(kv[i], StandardCharsets.UTF_8)).append('=')
             .append(URLEncoder.encode(kv[i + 1], StandardCharsets.UTF_8));
        }
        return HttpRequest.newBuilder(URI.create(url))
                .header("Content-Type", "application/x-www-form-urlencoded")
                .timeout(Duration.ofSeconds(30))
                .POST(HttpRequest.BodyPublishers.ofString(b.toString())).build();
    }

    static HttpRequest jsonRequest(String url, Object body, String bearer) {
        HttpRequest.Builder r = HttpRequest.newBuilder(URI.create(url))
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .timeout(Duration.ofSeconds(30))
                .POST(HttpRequest.BodyPublishers.ofString(Json.write(body)));
        if (bearer != null) r.header("Authorization", "Bearer " + bearer);
        return r.build();
    }

    static HttpResponse<String> send(HttpRequest r) throws IOException, InterruptedException {
        return http.send(r, HttpResponse.BodyHandlers.ofString());
    }

    static Object check(HttpResponse<String> r) throws IOException {
        if (r.statusCode() / 100 != 2) {
            String body = r.body();
            if (body.length() > 300) body = body.substring(0, 300);
            throw new IOException(r.request().uri().getHost() + " said " + r.statusCode() + ": " + body);
        }
        return Json.parse(r.body());
    }

    static Object postForm(String url, String... kv) throws Exception {
        return check(send(formRequest(url, kv)));
    }

    static Object postJson(String url, Object body, String bearer) throws Exception {
        return check(send(jsonRequest(url, body, bearer)));
    }

    static Object getJson(String url, String bearer) throws Exception {
        HttpRequest.Builder r = HttpRequest.newBuilder(URI.create(url)).timeout(Duration.ofSeconds(30)).GET();
        if (bearer != null) r.header("Authorization", "Bearer " + bearer);
        return check(send(r.build()));
    }
}
