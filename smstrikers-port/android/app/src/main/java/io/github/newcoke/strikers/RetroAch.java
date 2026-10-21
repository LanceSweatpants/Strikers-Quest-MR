package io.github.newcoke.strikers;

import android.content.Context;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

/**
 * strikers-quest r46: RetroAchievements (Lance: use "THEIR strikers achievement set"), the part the
 * launcher panel does: log in (the account's name and password, sent once; only the token RA gives
 * back is kept, in ra-login.txt in the app's private storage, which other apps cannot read), fetch
 * Super Mario Strikers' set and which of it
 * the player has, and keep the set as RA sent it (ra-set.json) for the game's side. Every call is a
 * POST to RA's own dorequest.php, as rcheevos makes it (rc_api_user.c / rc_api_runtime.c), and blocks:
 * call it off the UI thread. Casual (softcore) only: hardcore is for clients RA has approved.
 */
final class RetroAch {
    static final String HOST = "https://retroachievements.org/dorequest.php";
    /** Super Mario Strikers (GameCube) on retroachievements.org/game/25476. */
    static final int GAME_ID = 25476;
    static final String LOGIN_FILE = "ra-login.txt";
    static final String SET_FILE = "ra-set.json";
    /** The achievement RA's server adds for a client it has not approved (a notice, never earned). */
    static final int WARNING_ID = 101000001;

    static final class Login {
        final String user, token;
        Login(String user, String token) {
            this.user = user;
            this.token = token;
        }
    }

    static final class Achievement {
        int id, points, flags;
        String title, description, badge;
        boolean unlocked;
    }

    private RetroAch() {}

    /** RA's rule: a client names itself, uniquely, with its version and the system's. */
    static String userAgent(Context context) {
        String version = "0";
        try {
            version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
            version = version.replaceAll("[^0-9.].*$", "");
        } catch (Exception e) {
            // the version is only a label here
        }
        return "QuestStrikers/" + version + " (Android " + Build.VERSION.RELEASE + ")";
    }

    private static String form(String... kv) throws IOException {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i + 1 < kv.length; i += 2) {
            if (sb.length() > 0) sb.append('&');
            sb.append(kv[i]).append('=').append(URLEncoder.encode(kv[i + 1], "UTF-8"));
        }
        return sb.toString();
    }

    private static JSONObject post(Context context, String body) throws IOException, JSONException {
        HttpURLConnection c = (HttpURLConnection) new URL(HOST).openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(20000);
        c.setRequestMethod("POST");
        c.setDoOutput(true);
        c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
        c.setRequestProperty("User-Agent", userAgent(context));
        try (OutputStream out = c.getOutputStream()) {
            out.write(body.getBytes(StandardCharsets.UTF_8));
        }
        final int code = c.getResponseCode();
        InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        if (in != null) {
            try (InputStream s = in) {
                byte[] buf = new byte[16384];
                for (int n; (n = s.read(buf)) > 0; ) bytes.write(buf, 0, n);
            }
        }
        final String text = bytes.toString("UTF-8");
        JSONObject j = new JSONObject(text.isEmpty() ? "{}" : text);
        if (!j.optBoolean("Success", false)) {
            String why = j.optString("Error", "");
            throw new IOException(why.isEmpty() ? "RetroAchievements answered " + code : why);
        }
        return j;
    }

    /** Logs in with the password once and keeps the token (never the password). */
    static Login login(Context context, String user, String password) throws IOException, JSONException {
        JSONObject j = post(context, form("r", "login2", "u", user, "p", password));
        Login l = new Login(j.optString("User", user), j.optString("Token", ""));
        if (l.token.isEmpty()) throw new IOException("RetroAchievements gave no login token");
        save(context, l);
        return l;
    }

    static File file(Context context, String name) {
        return new File(StrikersHome.homeDir(context), name);
    }

    /** The login lives in the app's own private folder (the game's process reads it from there too). */
    static File loginFile(Context context) {
        return new File(context.getFilesDir(), LOGIN_FILE);
    }

    static void save(Context context, Login l) throws IOException {
        try (OutputStream out = new FileOutputStream(loginFile(context))) {
            out.write(("user=" + l.user + "\ntoken=" + l.token + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }

    /** The saved login, or null. */
    static Login saved(Context context) {
        File f = loginFile(context);
        if (!f.isFile()) return null;
        String user = "", token = "";
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[1024];
            int n = in.read(b);
            for (String line : new String(b, 0, Math.max(0, n), StandardCharsets.UTF_8).split("\n")) {
                if (line.startsWith("user=")) user = line.substring(5).trim();
                else if (line.startsWith("token=")) token = line.substring(6).trim();
            }
        } catch (IOException e) {
            return null;
        }
        return user.isEmpty() || token.isEmpty() ? null : new Login(user, token);
    }

    /**
     * r46d: what the game found when it last started (src/platform/xr_ra.cpp writes ra-status.txt in
     * the game's folder): whether RetroAchievements knows this game file and, when it doesn't, why,
     * in words. Null before the game has run with a login.
     */
    static String fileCheck(Context context) {
        File f = file(context, "ra-status.txt");
        if (!f.isFile()) return null;
        java.util.Map<String, String> kv = new java.util.HashMap<>();
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[2048];
            int n = in.read(b);
            for (String line : new String(b, 0, Math.max(0, n), StandardCharsets.UTF_8).split("\n")) {
                int eq = line.indexOf('=');
                if (eq > 0) kv.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
            }
        } catch (IOException e) {
            return null;
        }
        final String state = kv.getOrDefault("state", "");
        final String disc = kv.getOrDefault("disc", "");
        final String format = kv.getOrDefault("format", "");
        final boolean nkit = "1".equals(kv.get("nkit"));
        final String hash = kv.getOrDefault("hash", "");
        final String what = "This file: " + (disc.isEmpty() ? "?" : disc) + ", " + (format.isEmpty() ? "?" : format)
                + (nkit ? " (NKit)" : "") + (hash.isEmpty() ? "" : ", RetroAchievements ID " + hash) + ".";
        switch (state) {
            case "on":
                return "✓ RetroAchievements recognises your game file.";
            case "unknown":
                if (!disc.isEmpty() && !"G4QE".equals(disc)) {
                    return "✗ Achievements are off: RetroAchievements only supports the USA disc (G4QE), and"
                            + " your game file is another region's. " + what;
                }
                return "✗ Achievements are off: RetroAchievements doesn't know this copy of the game. It"
                        + " supports one: Super Mario Strikers (USA), the Redump dump (ID "
                        + kv.getOrDefault("want", "ea02ff3bdb24aa6ff6f13d5483e3f6c6") + "), as .iso, .rvz or"
                        + " .wia. A different, modified or damaged dump has another ID. " + what;
            case "unreadable":
                return "✗ Achievements are off: the game file couldn't be read for its ID. " + what;
            case "folder":
                return "✗ Achievements are off: the game is an unpacked folder. RetroAchievements needs the"
                        + " disc image file (.iso, .rvz or .wia).";
            default:
                return null;
        }
    }

    /**
     * r57: the set's badge pictures for the trophy shelf's board in the game (xr_quest.cpp): each
     * downloaded once from RA's media server, decoded and saved as raw 64 x 64 RGBA in ra-badges/
     * in the game's folder ("<BadgeName>.rgba"), from the set saved in ra-set.json. Blocks: call it
     * off the UI thread. Returns how many are there now.
     */
    static int fetchBadges(Context context) {
        File set = file(context, SET_FILE);
        if (!set.isFile()) return 0;
        java.util.List<String> names = new java.util.ArrayList<>();
        try (InputStream in = new java.io.FileInputStream(set)) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            for (int n; (n = in.read(buf)) > 0; ) bytes.write(buf, 0, n);
            JSONObject patch = new JSONObject(bytes.toString("UTF-8"));
            JSONObject data = patch.optJSONObject("PatchData");
            JSONArray all = data != null ? data.optJSONArray("Achievements") : null;
            for (int i = 0; all != null && i < all.length(); i++) {
                JSONObject a = all.optJSONObject(i);
                if (a == null || a.optInt("ID") == WARNING_ID) continue;
                String b = a.optString("BadgeName");
                if (b.matches("[0-9A-Za-z_]{1,16}")) names.add(b);
            }
        } catch (IOException | JSONException e) {
            return 0;
        }
        File dir = file(context, "ra-badges");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        int have = 0;
        for (String name : names) {
            File out = new File(dir, name + ".rgba");
            if (out.isFile() && out.length() == 64 * 64 * 4) {
                have++;
                continue;
            }
            try {
                HttpURLConnection c = (HttpURLConnection)
                        new URL("https://media.retroachievements.org/Badge/" + name + ".png").openConnection();
                c.setConnectTimeout(10000);
                c.setReadTimeout(15000);
                c.setRequestProperty("User-Agent", userAgent(context));
                if (c.getResponseCode() != 200) continue;
                ByteArrayOutputStream png = new ByteArrayOutputStream();
                try (InputStream s = c.getInputStream()) {
                    byte[] buf = new byte[16384];
                    for (int n; (n = s.read(buf)) > 0; ) png.write(buf, 0, n);
                }
                android.graphics.Bitmap bmp = android.graphics.BitmapFactory.decodeByteArray(png.toByteArray(), 0, png.size());
                if (bmp == null) continue;
                android.graphics.Bitmap scaled = android.graphics.Bitmap.createScaledBitmap(bmp, 64, 64, true);
                int[] argb = new int[64 * 64];
                scaled.getPixels(argb, 0, 64, 0, 0, 64, 64);
                byte[] rgba = new byte[64 * 64 * 4];
                for (int i = 0; i < argb.length; i++) {
                    rgba[i * 4] = (byte) (argb[i] >> 16);
                    rgba[i * 4 + 1] = (byte) (argb[i] >> 8);
                    rgba[i * 4 + 2] = (byte) argb[i];
                    rgba[i * 4 + 3] = (byte) (argb[i] >>> 24);
                }
                File tmp = new File(dir, name + ".tmp");
                try (OutputStream o = new FileOutputStream(tmp)) {
                    o.write(rgba);
                }
                //noinspection ResultOfMethodCallIgnored
                tmp.renameTo(out);
                have++;
            } catch (IOException | RuntimeException e) {
                // the next panel start tries again
            }
        }
        return have;
    }

    static void logout(Context context) {
        //noinspection ResultOfMethodCallIgnored
        loginFile(context).delete();
    }

    /**
     * The set (titles, descriptions, points, badges and RA's own conditions), saved as ra-set.json,
     * with which of it this account has unlocked in casual.
     */
    static java.util.List<Achievement> fetchSet(Context context, Login l) throws IOException, JSONException {
        JSONObject patch = post(context, form("r", "patch", "u", l.user, "t", l.token, "g", String.valueOf(GAME_ID)));
        try (OutputStream out = new FileOutputStream(file(context, SET_FILE))) {
            out.write(patch.toString(1).getBytes(StandardCharsets.UTF_8));
        }
        JSONObject unlocks = post(context, form("r", "unlocks", "u", l.user, "t", l.token, "g", String.valueOf(GAME_ID),
                "h", "0"));
        Set<Integer> have = new HashSet<>();
        JSONArray ids = unlocks.optJSONArray("UserUnlocks");
        for (int i = 0; ids != null && i < ids.length(); i++) have.add(ids.optInt(i));
        java.util.List<Achievement> list = new java.util.ArrayList<>();
        JSONObject data = patch.optJSONObject("PatchData");
        JSONArray all = data != null ? data.optJSONArray("Achievements") : null;
        for (int i = 0; all != null && i < all.length(); i++) {
            JSONObject a = all.optJSONObject(i);
            if (a == null || a.optInt("Flags", 3) != 3) continue;  // 3 = core; 5 = unofficial
            // r46: RA's notice to clients it has not approved, not one of the set's own.
            if (a.optInt("ID") == WARNING_ID) continue;
            Achievement x = new Achievement();
            x.id = a.optInt("ID");
            x.title = a.optString("Title");
            x.description = a.optString("Description");
            x.points = a.optInt("Points");
            x.badge = a.optString("BadgeName");
            x.flags = a.optInt("Flags");
            x.unlocked = have.contains(x.id);
            list.add(x);
        }
        return list;
    }
}
