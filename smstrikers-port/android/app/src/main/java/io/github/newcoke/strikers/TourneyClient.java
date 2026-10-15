package io.github.newcoke.strikers;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;

/**
 * strikers-quest r45: the tournament hub's connection for the launcher panel (LobbyActivity): one
 * WebSocket to wss://&lt;room server&gt;/tourney/&lt;KEY&gt; (KEY = Version letter + online protocol, so only
 * players who can play each other meet), held on a thread of its own while the Tournaments page is up.
 * It says hello with the player's id and team name, passes on every state the hub sends (on the UI
 * thread), sends the panel's requests, and reconnects by itself. See server/README.md, Tournaments.
 */
final class TourneyClient {
    interface Listener {
        /** A new state from the hub (UI thread). */
        void onState(JSONObject state);
        /** Connected or not (UI thread); `why` says what went wrong, or null. */
        void onLink(boolean connected, String why);
    }

    private static final String TAG = "strikers_tourney";

    private final Context context;  // the panel: its words for onLink
    private final String host;
    private final String key;
    private final String pid;
    private volatile String name;
    private volatile int star;  // the team's logo (StarLogo), -1 none
    private final Listener listener;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean running;
    private volatile int handle = -1;
    private final java.util.concurrent.LinkedBlockingQueue<String> outbox = new java.util.concurrent.LinkedBlockingQueue<>();
    private Thread thread;

    TourneyClient(Context context, String host, String key, String pid, String name, int star, Listener listener) {
        this.context = context;
        this.host = host;
        this.key = key;
        this.pid = pid;
        this.name = name;
        this.star = star;
        this.listener = listener;
    }

    void start() {
        if (running) return;
        running = true;
        thread = new Thread(this::loop, "strikers-tourney");
        thread.setDaemon(true);
        thread.start();
    }

    void stop() {
        running = false;
        final int h = handle;
        handle = -1;
        if (h >= 0) NetWs.close(h);
    }

    /**
     * A request ({"t":"join","size":4} and the like); false when not connected. r45c: queued for the
     * connection's thread, never written here: the panel calls this on the UI thread, where Android
     * refuses network writes (NetworkOnMainThreadException), and r45/r45b's joins died that way.
     */
    boolean send(JSONObject message) {
        if (handle < 0) return false;
        outbox.offer(message.toString());
        return true;
    }

    /** A new team name or logo: kept for the next hello, and sent now if connected. */
    void identity(String newName, int newStar) {
        name = newName;
        star = newStar;
        send(hello());
    }

    private JSONObject hello() {
        JSONObject m = new JSONObject();
        try {
            m.put("t", "hello");
            m.put("pid", pid);
            m.put("name", name);
            m.put("star", star);
        } catch (JSONException e) {
            // cannot happen with these values
        }
        return m;
    }

    private void loop() {
        final byte[] buf = new byte[64 * 1024];
        int failures = 0;
        while (running) {
            final int h = NetWs.open(host, 443, "/tourney/" + key, 6000);
            if (h < 0) {
                failures++;
                ui.post(() -> listener.onLink(false, context.getString(R.string.tourney_unreachable)));
                SystemClock.sleep(Math.min(10000, 2000L * failures));
                continue;
            }
            if (!running) {
                NetWs.close(h);
                return;
            }
            handle = h;
            failures = 0;
            NetWs.sendText(h, hello().toString());
            ui.post(() -> listener.onLink(true, null));
            long lastPing = SystemClock.elapsedRealtime();
            boolean sendFailed = false;
            while (running) {
                // The panel's requests, from this thread (see send).
                for (String out = outbox.poll(); out != null; out = outbox.poll()) {
                    if (!NetWs.sendText(h, out)) {
                        outbox.offer(out);  // again on the next connection
                        sendFailed = true;
                        break;
                    }
                }
                if (sendFailed) break;
                final long now = SystemClock.elapsedRealtime();
                if (now - lastPing >= 20000) {
                    if (!NetWs.sendText(h, "{\"t\":\"ping\"}")) break;
                    lastPing = now;
                }
                final int n = NetWs.recv(h, buf, 100);
                if (n == -1) break;
                if (n < 0) continue;
                try {
                    final JSONObject m = new JSONObject(new String(buf, 0, n, StandardCharsets.UTF_8));
                    if ("state".equals(m.optString("t"))) {
                        ui.post(() -> listener.onState(m));
                    } else if ("error".equals(m.optString("t"))) {
                        // r45b: on the page, not only in the log (a refused join said nothing).
                        final String msg = m.optString("msg");
                        Log.w(TAG, "hub: " + msg);
                        ui.post(() -> listener.onLink(true, msg));
                    }
                } catch (JSONException e) {
                    Log.w(TAG, "hub sent something that isn't JSON");
                }
            }
            handle = -1;
            NetWs.close(h);
            if (running) {
                ui.post(() -> listener.onLink(false, context.getString(R.string.tourney_reconnecting)));
                SystemClock.sleep(1000);
            }
        }
    }
}
