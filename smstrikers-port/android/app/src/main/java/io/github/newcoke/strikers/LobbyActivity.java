package io.github.newcoke.strikers;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputFilter;
import android.text.InputType;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.List;

/**
 * strikers-quest r39: the panel in front of the game. Play offline, host an online match (a
 * four-letter room code to tell your friend), or join one by typing its code. The game itself is
 * {@link StrikersActivity}, immersive; this is a flat Quest panel that starts it with the room
 * code (extra "room", "" for offline), which the game hands to its network layer (STRIKERS_ROOM).
 * In a room, both players pick Grudge Match and start: the match begins once both are in.
 *
 * <p>r39b: this panel runs in the app's main process and the game in a process of its own
 * (":game", see StrikersActivity.onDestroy). The "all files access" switch is asked for from here,
 * before the game is started.
 *
 * <p>r43: the Version button, Classic (alpha 1.2's game) or Plus (with what was added after it), and
 * why an online match didn't start (the friend on the other Version or another release).
 *
 * <p>r44: Quick match (a queue on the room server pairs you with anyone else searching on the same
 * Version and release, and both get a room code) and the Voice chat switch.
 */
public class LobbyActivity extends Activity {

    // strikers-quest r44: the room server (src/platform/xr_net.cpp kServerHost) and the online protocol
    // (kProtocol there): quick match only pairs headsets that can play each other.
    private static final String SERVER_HOST = "strikers-rooms.strikers-rooms.workers.dev";
    private static final int ONLINE_PROTOCOL = 5;
    private static final int REQUEST_MIC = 44;
    private int quickToken;          // r44: the search running (a newer one, or Cancel, ends it)
    private boolean searching;       // r44: the quick match page is up

    // strikers-quest r45: tournaments. The game is started for a tournament match with its match id
    // (StrikersActivity -> the network layer, which writes the result when the match ends), and comes
    // back here with EXTRA_SHOW = SHOW_TOURNEY to report it.
    static final String EXTRA_TOURNEY = "tourney";
    static final String EXTRA_SHOW = "show";
    static final String SHOW_TOURNEY = "tourney";
    static final String TOURNEY_RESULT = "tourney-result.txt";
    private static final String TEAM_NAME = "teamName";
    private static final String TEAM_STAR = "teamStar";
    private static final String PLAYER_ID = "playerId";
    private TourneyClient tourney;        // the hub's connection while the Tournaments page is up
    private org.json.JSONObject tourneyState;
    private LinearLayout tourneyBody;     // the part of the page each state redraws
    private TextView tourneyLink;
    private EditText tourneyNameField;    // r45b: the team name box, read when a tournament is tapped
    private String autoStartedMid = "";   // the match the countdown already ran for
    private int countdownToken;
    private boolean wasOnTourneys;        // the page was up when the panel went out of sight

    static final String EXTRA_ROOM = "room";
    // strikers-quest r43: the Version the panel shows (StrikersHome.CLASSIC / PLUS), so a game already
    // running on the other one can tell (StrikersActivity.onNewIntent).
    static final String EXTRA_EDITION = "edition";
    // strikers-quest r43: why the last online match didn't start, one line beside the game's log
    // (src/platform/xr_net.cpp writes "edition=..." or "protocol=..."; StrikersActivity this one).
    static final String ONLINE_NOTE = "online-note.txt";
    static final String NOTE_VERSION_CHANGED = "version-changed";
    private static final String PREFS = "lobby";
    private static final String LAST_CODE = "lastCode";
    // No I, O, 0 or 1: a code is read out loud or off a headset.
    private static final String CODE_LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    private static final int BG = Color.rgb(16, 20, 28);
    private static final int CARD = Color.rgb(30, 38, 52);
    private static final int ACCENT = Color.rgb(255, 196, 0);
    private static final int WARN = Color.rgb(255, 150, 140);
    private static final int MUTED = Color.rgb(150, 160, 175);

    private LinearLayout page;
    private SharedPreferences prefs;
    private TextView accessNote;  // r39b: under the buttons while the file access switch is off
    private int chooserToken;     // r41: the game file list being filled (a newer one wins)
    private Updater.Release update;  // r41: a newer release on GitHub, null when there is none
    private boolean onHome;          // r41: the home page is showing (the update check redraws it)

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER_HORIZONTAL);
        page.setBackgroundColor(BG);
        int pad = dp(28);
        page.setPadding(pad, pad, pad, pad);
        // r41: scrolls, for the game file list and a last error on a short panel.
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.setFillViewport(true);
        scroll.addView(page, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
        if (SHOW_TOURNEY.equals(getIntent().getStringExtra(EXTRA_SHOW))) {
            showTourneys();  // r45: back from a tournament match
        } else {
            showHome();
        }
        checkForUpdate();
    }

    private void showHome() {
        leaveTourneyPage();
        page.removeAllViews();
        onHome = true;
        title("Quest 3 Strikers", 30);
        if (update != null) {
            note(shortName(update) + " is out.").setTextColor(ACCENT);
            page.addView(button("Update now", true, v -> startUpdate()));
        }
        showLastError();
        showOnlineNote();
        note("Quest alpha. Online: both players pick Grudge Match in the same room, on the same Version.");
        page.addView(button("Play offline", true, v -> start("")));
        page.addView(button("Quick match", true, v -> showQuick()));  // r44
        page.addView(button("Tournaments", true, v -> showTourneys()));  // r45
        // r44d (Lance: "make sure everyones codes randomize"): a fresh code every time, not the last one.
        page.addView(button("Host an online match", false, v -> showHost(newCode())));
        page.addView(button("Join with a code", false, v -> showJoin()));
        page.addView(button("Voice chat: " + (voiceOn() ? "On" : "Off"), false, v -> toggleVoice()));  // r44
        page.addView(button("Controls", false, v -> showControls()));  // r46
        page.addView(button("RetroAchievements", false, v -> showRetroAch()));  // r46
        page.addView(button("Choose game file", false, v -> showChooser(null, null)));
        TextView game = note(gameStatus());
        game.setTextSize(14);
        // strikers-quest r43: Classic or Plus, by the app's own version below.
        page.addView(button("Version: " + editionName(StrikersHome.edition(this)), false, v -> showVersions()));
        TextView version = note(versionText());
        version.setTextSize(12);
        version.setTextColor(MUTED);
    }

    /** strikers-quest r41: GitHub's latest release, once per panel start, off the UI thread. */
    private void checkForUpdate() {
        final String own = versionName();
        new Thread(() -> {
            final Updater.Release r = Updater.newer(own);
            if (r == null) return;
            runOnUiThread(() -> {
                update = r;
                if (onHome) showHome();
            });
        }, "strikers-update-check").start();
    }

    /** strikers-quest r41: download the newer release and hand it to the system installer. */
    private void startUpdate() {
        final Updater.Release r = update;
        if (r == null) return;
        // The system's "install unknown apps" switch for this app, the first time. Where the
        // headset has no such screen, the installer itself asks.
        if (!getPackageManager().canRequestPackageInstalls()) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + getPackageName())));
                showHome();
                note("Allow this app to install its update, then come back here and press Update now"
                        + " again.").setTextColor(WARN);
                return;
            } catch (Exception e) {
                // fall through to the installer
            }
        }
        onHome = false;
        page.removeAllViews();
        title("Updating to " + shortName(r), 24);
        final TextView status = note("Downloading... 0%");
        page.addView(button("Back", false, v -> showHome()));
        new Thread(() -> {
            try {
                File apk = Updater.download(r, new File(getCacheDir(), "update"),
                        pct -> runOnUiThread(() -> status.setText("Downloading... " + pct + "%")));
                runOnUiThread(() -> status.setText("Installing. Confirm on the next screen: the app closes,"
                        + " updates, and you open it again."));
                Updater.install(this, apk);
            } catch (IOException | RuntimeException e) {
                runOnUiThread(() -> status.setText("The update didn't work (" + e.getMessage() + ").\nCheck"
                        + " the Quest's internet connection and try again, or get it from the GitHub page."));
            }
        }, "strikers-update").start();
    }

    /** strikers-quest r41: the system installer's answer (Updater.install). */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (intent != null && SHOW_TOURNEY.equals(intent.getStringExtra(EXTRA_SHOW))) {
            showTourneys();  // r45: back from a tournament match
            return;
        }
        if (intent == null || !Updater.ACTION_INSTALL_STATUS.equals(intent.getAction())) return;
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);  // the "Install?" screen
            if (confirm != null) startActivity(confirm);
        } else if (status != PackageInstaller.STATUS_SUCCESS) {
            String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            showHome();
            note("The update wasn't installed" + (msg != null ? ": " + msg : ".")).setTextColor(WARN);
        }
    }

    /** "alpha 1.2" out of "Quest 3 Strikers - alpha 1.2". */
    private static String shortName(Updater.Release r) {
        String name = r.name.replaceFirst("(?i)^quest 3 strikers\\s*[-–—:]\\s*", "");
        return name.isEmpty() ? r.tag : name.substring(0, 1).toUpperCase(java.util.Locale.ROOT) + name.substring(1);
    }

    /**
     * strikers-quest r41: the message the game closed with last time (fatal.c writes it; under the
     * immersive view its own message box is never seen), with an OK that clears it.
     */
    private void showLastError() {
        final File f = new File(StrikersHome.homeDir(this), "last-error.txt");
        if (!f.isFile()) return;
        String text = readText(f, 1500);
        String[] lines = text.split("\n");
        StringBuilder shown = new StringBuilder();
        for (int i = 0; i < lines.length && i < 12; i++) shown.append(i > 0 ? "\n" : "").append(lines[i]);
        TextView t = note("The game closed last time with this message:\n\n" + shown);
        t.setTextColor(WARN);
        t.setTextSize(14);
        page.addView(button("OK", false, v -> {
            //noinspection ResultOfMethodCallIgnored
            f.delete();
            showHome();
        }));
    }

    /**
     * strikers-quest r43: why the last online match didn't start, in words, with an OK that clears it.
     * The game comes back here when it refuses one (StrikersActivity), so this is what the player sees
     * first, with the Version button on the same page. Before r43 the reason was only in the log.
     */
    private void showOnlineNote() {
        final File f = new File(StrikersHome.homeDir(this), ONLINE_NOTE);
        if (!f.isFile()) return;
        String why = readText(f, 200);
        String mine = StrikersHome.edition(this);
        String text;
        if (why.startsWith("edition=")) {
            String theirs = StrikersHome.CLASSIC.equals(why.substring(8).trim()) ? StrikersHome.CLASSIC
                    : StrikersHome.PLUS;
            if (theirs.equals(mine)) {  // already switched to theirs
                //noinspection ResultOfMethodCallIgnored
                f.delete();
                return;
            }
            text = "Your friend is playing " + editionName(theirs) + ". You are on " + editionName(mine)
                    + ".\nOne of you: switch Version to match, then start the match again.";
        } else if (why.startsWith(NOTE_VERSION_CHANGED)) {
            text = "The game was still open on the other Version, so it closed. Press the same button"
                    + " again to play " + editionName(mine) + ".";
        } else {
            text = "Your friend has a different release of Quest 3 Strikers, so you can't play online"
                    + " together. Both of you need the same one (Update now, or the GitHub page).";
        }
        TextView t = note(text);
        t.setTextColor(WARN);
        page.addView(button("OK", false, v -> {
            //noinspection ResultOfMethodCallIgnored
            f.delete();
            showHome();
        }));
    }

    /** strikers-quest r43: one line for {@link #showOnlineNote} (StrikersActivity, in the game's process). */
    static void writeOnlineNote(android.content.Context context, String line) {
        try (java.io.OutputStream out = new java.io.FileOutputStream(
                new File(StrikersHome.homeDir(context), ONLINE_NOTE))) {
            out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            // Only the explanation is lost; the panel still opens.
        }
    }

    /** strikers-quest r43: "Classic 1.2" / "Plus 1.3". */
    private String editionName(String edition) {
        return StrikersHome.CLASSIC.equals(edition) ? "Classic 1.2" : plusName();
    }

    /**
     * strikers-quest r43: Plus is this release, numbered the way the releases are (alpha 1.2 was
     * versionName 0.1.2): "0.1.3-dev (r43)" is Plus 1.3.
     */
    private String plusName() {
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("^\\d+\\.(\\d+)\\.(\\d+)").matcher(versionName());
        return "Plus " + (m.find() ? m.group(1) + "." + m.group(2) : "1.3");
    }

    /**
     * strikers-quest r43: the Version picker (Lance: "we draw a line after 1.2 to notify versions after
     * that are no longer strikers as they know it"). A tap saves it at once; the game takes it when it
     * starts (StrikersHome.prepare -> STRIKERS_EDITION, src/platform/edition.cpp).
     */
    private void showVersions() {
        leaveTourneyPage();  // r45
        page.removeAllViews();
        onHome = false;
        title("Choose your version", 24);
        note("To play online, you and your friend need the same one.");
        final String current = StrikersHome.edition(this);
        page.addView(versionRow(StrikersHome.CLASSIC, "Mario Strikers as you know it", current));
        // The line: everything under it adds to the game.
        TextView label = note("Below this line: not Strikers as you know it");
        label.setTextColor(ACCENT);
        label.setTextSize(15);
        label.setPadding(0, dp(18), 0, dp(6));
        View line = new View(this);
        line.setBackgroundColor(ACCENT);
        page.addView(line, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(3)));
        page.addView(versionRow(StrikersHome.PLUS, "New: life-size goal scorer, GOAL SCORED BY banner,"
                + " longer goal replay, living crowd", current));
        page.addView(button("Back", false, v -> showHome()));
    }

    /** strikers-quest r43: one Version in the picker, its name large over what it is. */
    private Button versionRow(String edition, String about, String current) {
        String name = editionName(edition) + (edition.equals(current) ? "   (chosen)" : "");
        Button b = row(name + "\n" + about, true, v -> {
            if (!edition.equals(StrikersHome.edition(this))) {
                StrikersHome.chooseEdition(this, edition);
                endRunningGame();
                // A note that said to switch has been acted on.
                //noinspection ResultOfMethodCallIgnored
                new File(StrikersHome.homeDir(this), ONLINE_NOTE).delete();
            }
            showHome();
        });
        SpannableString text = new SpannableString(b.getText());
        text.setSpan(new RelativeSizeSpan(1.35f), 0, name.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        text.setSpan(new StyleSpan(Typeface.BOLD), 0, name.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        b.setText(text);
        return b;
    }

    /**
     * strikers-quest r43: a game left running in the background (the Quest button, then this app
     * again) keeps the Version it started with, and a start from here would only hand it a room
     * (StrikersActivity.onNewIntent). So a Version change ends it: the next start is a fresh game.
     * It is paused behind this panel, as when the system ends a background app for memory.
     */
    private void endRunningGame() {
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> running = am != null ? am.getRunningAppProcesses() : null;
        if (running == null) return;
        String game = getPackageName() + ":game";
        for (ActivityManager.RunningAppProcessInfo p : running) {
            if (game.equals(p.processName)) {
                android.util.Log.i("strikers", "Version changed: ending the game left running (pid " + p.pid + ")");
                android.os.Process.killProcess(p.pid);
            }
        }
    }

    /** strikers-quest r41: which game file a match would start with, for the home page. */
    private String gameStatus() {
        if (!StrikersHome.hasAllFilesAccess()) {
            return "Game file: the app needs file access to look for it.";
        }
        String path = StrikersHome.describeGameData(this);
        if (path == null) return "No game file found yet. Press \"Choose game file\".";
        File f = new File(path);
        if (f.isDirectory()) return "Game: the extracted disc in " + GameFile.shortPath(f);
        GameFile.Found found = GameFile.check(f);
        return (found.usable ? "Game: " + found.label : "Game file problem: " + found.problem)
                + "\n" + GameFile.shortPath(f);
    }

    /** Null when the game has a file it can read; otherwise what is wrong, in one sentence. */
    private String gameProblem() {
        String path = StrikersHome.describeGameData(this);
        if (path == null) return "Your game file wasn't found on this Quest.";
        File f = new File(path);
        if (f.isDirectory()) return null;
        GameFile.Found found = GameFile.check(f);
        return found.usable ? null : GameFile.shortPath(f) + ": " + found.problem;
    }

    /**
     * strikers-quest r41: every .iso, .rvz and .wia on the headset, checked, to tap the one to play.
     * code != null: the match the player asked for, started once a file is picked.
     */
    private void showChooser(String code, String reason) {
        if (!StrikersHome.hasAllFilesAccess() && askForFileAccess(this)) {
            showHome();
            accessNote = note("Turn on \"Allow access to manage all files\" so the game can find your"
                    + " game file, then come back here and press the same button again.");
            return;
        }
        leaveTourneyPage();  // r45
        page.removeAllViews();
        onHome = false;
        title("Choose your game file", 24);
        if (reason != null) note(reason).setTextColor(WARN);
        final TextView status = note("Looking for game files on this Quest...");
        final LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setGravity(Gravity.CENTER_HORIZONTAL);
        page.addView(list, wrap());
        page.addView(button("Look again", false, v -> showChooser(code, null)));
        page.addView(button("Back", false, v -> showHome()));
        final int token = ++chooserToken;
        new Thread(() -> {
            final List<GameFile.Found> found = GameFile.scan();
            runOnUiThread(() -> {
                if (token != chooserToken || list.getParent() != page) return;  // left this page
                if (found.isEmpty()) {
                    status.setText("No .iso, .rvz or .wia file found on this Quest.\n\nOn your PC, open"
                            + " SideQuest, click the folder icon, open Download, and drag your game file"
                            + " in. Then press Look again.");
                    return;
                }
                status.setText("Tap your game file:");
                String chosen = GameFile.chosen(this);
                for (GameFile.Found f : found) {
                    String where = GameFile.shortPath(f.file) + " · " + GameFile.size(f.file.length());
                    boolean isChosen = f.file.getAbsolutePath().equals(chosen);
                    String text = f.file.getName() + (isChosen ? "   (chosen)" : "") + "\n"
                            + (f.usable ? f.label : f.label + ": " + f.problem) + "\n" + where;
                    list.addView(row(text, f.usable, f.usable ? v -> {
                        GameFile.choose(this, f.file.getAbsolutePath());
                        if (code != null) start(code); else showHome();
                    } : null));
                }
            });
        }, "strikers-scan").start();
    }

    private void showHost(String code) {
        leaveTourneyPage();  // r45
        page.removeAllViews();
        onHome = false;
        title("Your room code", 24);
        TextView big = new TextView(this);
        big.setText(spaced(code));
        big.setTextColor(ACCENT);
        big.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        big.setTextSize(64);
        big.setGravity(Gravity.CENTER);
        big.setPadding(0, dp(8), 0, dp(8));
        page.addView(big, wrap());
        note("Tell your friend this code. Both of you: Grudge Match, pick your team, start.");
        page.addView(button("Start", true, v -> start(code)));
        page.addView(button("New code", false, v -> showHost(newCode())));
        page.addView(button("Back", false, v -> showHome()));
    }

    private void showJoin() {
        leaveTourneyPage();  // r45
        page.removeAllViews();
        onHome = false;
        title("Join a room", 24);
        note("Type the code your friend sees.");
        EditText field = new EditText(this);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        field.setFilters(new InputFilter[] {new InputFilter.AllCaps(), new InputFilter.LengthFilter(8)});
        field.setTextSize(40);
        field.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        field.setTextColor(Color.WHITE);
        field.setHintTextColor(Color.GRAY);
        field.setHint("CODE");
        field.setGravity(Gravity.CENTER);
        field.setImeOptions(EditorInfo.IME_ACTION_GO);
        field.setSingleLine(true);
        field.setText(prefs.getString(LAST_CODE, ""));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(360), ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        page.addView(field, lp);
        TextView problem = note("");
        Runnable join = () -> {
            String code = clean(field.getText().toString());
            if (code.length() < 3) {
                problem.setText("A code is at least 3 letters or numbers.");
                return;
            }
            start(code);
        };
        field.setOnEditorActionListener((v, actionId, event) -> {
            join.run();
            return true;
        });
        page.addView(button("Join", true, v -> join.run()));
        page.addView(button("Back", false, v -> showHome()));
        field.requestFocus();
    }

    /**
     * strikers-quest r44, Quick match (Lance: "a quick match option that would put you in with anyone
     * else online"): wait in the room server's queue for this Version and release (wss://.../quick/
     * P5 or C5) until someone else is there; both get the same fresh room code and start with it, the
     * way Join does.
     */
    private void showQuick() {
        if (!readyToStart(null)) return;  // the game file first: no use matching a game that can't start
        leaveTourneyPage();  // r45
        page.removeAllViews();
        onHome = false;
        searching = true;
        title("Quick match", 24);
        final TextView status = note("Looking for another player...");
        status.setTextColor(ACCENT);
        String edition = StrikersHome.edition(this);
        note("You'll be matched with someone playing " + editionName(edition) + " right now. Then both of"
                + " you: Grudge Match, pick your team, start." + (voiceOn() ? "" : "\nVoice chat is off"
                + " (the button on the first page)."));
        page.addView(button("Cancel", false, v -> {
            ++quickToken;
            searching = false;
            showHome();
        }));
        final int token = ++quickToken;
        final String key = (StrikersHome.CLASSIC.equals(edition) ? "C" : "P") + ONLINE_PROTOCOL;
        new Thread(() -> quickLoop(token, key, status), "strikers-quick").start();
    }

    /** strikers-quest r44: the queue, off the UI thread, reconnecting until matched or cancelled. */
    private void quickLoop(int token, String key, TextView status) {
        final byte[] buf = new byte[512];
        int failures = 0;
        while (token == quickToken) {
            final int h = NetWs.open(SERVER_HOST, 443, "/quick/" + key, 6000);
            if (h < 0) {
                failures++;
                runOnUiThread(() -> status.setText("Can't reach the online server. Trying again...\n(Check the"
                        + " Quest's internet connection.)"));
                android.os.SystemClock.sleep(Math.min(10000, 2000L * failures));
                continue;
            }
            failures = 0;
            String code = null;
            long lastPing = android.os.SystemClock.elapsedRealtime();
            while (token == quickToken) {
                final long now = android.os.SystemClock.elapsedRealtime();
                if (now - lastPing >= 5000) {
                    if (!NetWs.sendText(h, "ping")) break;
                    lastPing = now;
                }
                final int n = NetWs.recv(h, buf, 500);
                if (n == -1) break;  // gone: open a new one (the server drops a socket quiet for 20 s)
                if (n < 0) continue;
                final String msg = new String(buf, 0, n, StandardCharsets.UTF_8).trim();
                if (msg.startsWith("match ")) {
                    code = clean(msg.substring(6));
                    break;  // at once: the close behind it can come seconds later
                }
                if (msg.startsWith("waiting ")) {
                    final String count = msg.substring(8).trim();
                    runOnUiThread(() -> {
                        if (token == quickToken) {
                            status.setText("Looking for another player..." + ("1".equals(count) ? ""
                                    : "\n" + count + " players searching"));
                        }
                    });
                }
            }
            NetWs.close(h);
            if (code != null && code.length() >= 3) {
                final String room = code;
                runOnUiThread(() -> {
                    if (token == quickToken) matched(room);
                });
                return;
            }
        }
    }

    /** strikers-quest r44: a player was found; a moment to read what to do, then the game starts. */
    private void matched(String code) {
        searching = false;
        page.removeAllViews();
        title("Player found!", 28);
        TextView t = note("In the game: Grudge Match, pick your team, start. The match begins when you both"
                + " have.\n\n(Your room: " + spaced(code) + ")");
        t.setTextSize(18);
        final int token = ++quickToken;
        page.postDelayed(() -> {
            if (token == quickToken) start(code);
        }, 3000);
    }

    // --- strikers-quest r46: Controls (Lance: "some type of button mapping for this game so users can
    // customize their controls"). Each GameCube button gets a Quest button; xr_controls.txt in the
    // game's folder holds the map ("A=a", "Z=rg", ...), and the game looks at it about once a second
    // (xr_quest.cpp reload_controls), so a change applies even with the game open behind this panel.
    // The sticks, the stick clicks (views) and the left grip (moving the table or the screen) stay.

    private static final String[] ACTIONS = {"A", "B", "X", "Y", "Z", "L", "R", "START"};
    private static final String[] ACTION_LABELS = {"Pass (GameCube A)", "Shoot (GameCube B)",
            "Use your item (GameCube X)", "Hit (GameCube Y)", "Swap items (GameCube Z)", "GameCube L",
            "GameCube R", "Pause (Start)"};
    private static final String[] SOURCES = {"a", "b", "x", "y", "lt", "rt", "rg", "menu", "none"};
    private static final String[] SOURCE_LABELS = {"A button", "B button", "X button", "Y button",
            "Left trigger", "Right trigger", "Right grip", "Menu button", "Nothing"};
    private static final String[] DEFAULT_CONTROLS = {"a", "b", "x", "y", "rg", "lt", "rt", "menu"};

    private File controlsFile() {
        return new File(StrikersHome.homeDir(this), "xr_controls.txt");
    }

    /** The map as the game reads it: one source name per action, defaults for anything missing. */
    private String[] readControls() {
        String[] map = DEFAULT_CONTROLS.clone();
        for (String line : readText(controlsFile(), 400).split("\n")) {
            int eq = line.indexOf('=');
            if (eq <= 0) continue;
            String key = line.substring(0, eq).trim(), value = line.substring(eq + 1).trim().toLowerCase(java.util.Locale.ROOT);
            for (int i = 0; i < ACTIONS.length; i++) {
                if (ACTIONS[i].equalsIgnoreCase(key) && java.util.Arrays.asList(SOURCES).contains(value)) map[i] = value;
            }
        }
        return map;
    }

    private void writeControls(String[] map) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < ACTIONS.length; i++) sb.append(ACTIONS[i]).append('=').append(map[i]).append('\n');
        try (java.io.OutputStream out = new java.io.FileOutputStream(controlsFile())) {
            out.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            android.util.Log.w("strikers", "could not write " + controlsFile(), e);
        }
    }

    private static String sourceLabel(String source) {
        for (int i = 0; i < SOURCES.length; i++) if (SOURCES[i].equals(source)) return SOURCE_LABELS[i];
        return source;
    }

    private void showControls() {
        leaveTourneyPage();
        page.removeAllViews();
        onHome = false;
        title("Controls", 26);
        note("Tap a game button, then the Quest button you want for it. It changes right away, even with"
                + " the game open.");
        final String[] map = readControls();
        for (int i = 0; i < ACTIONS.length; i++) {
            final int action = i;
            final String head = ACTION_LABELS[i];
            final boolean changed = !map[i].equals(DEFAULT_CONTROLS[i]);
            Button b = row(head + "\n" + sourceLabel(map[i]) + (changed ? "   (changed)" : ""), true,
                    v -> showControlPick(action));
            SpannableString text = new SpannableString(b.getText());
            text.setSpan(new StyleSpan(Typeface.BOLD), 0, head.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            b.setText(text);
            page.addView(b);
        }
        TextView fixed = note("Always: left stick moves, right stick is the GameCube C-stick, clicking a stick"
                + " twice switches the view, and the left grip moves the table or the big screen.");
        fixed.setTextSize(14);
        fixed.setTextColor(MUTED);
        page.addView(button("Back to the default buttons", false, v -> {
            writeControls(DEFAULT_CONTROLS.clone());
            showControls();
        }));
        page.addView(button("Back", false, v -> showHome()));
    }

    /** One game button: the Quest buttons to pick from. One already taken swaps with it. */
    private void showControlPick(final int action) {
        page.removeAllViews();
        onHome = false;
        title(ACTION_LABELS[action], 24);
        final String[] map = readControls();
        note("Pick the Quest button for it. Now: " + sourceLabel(map[action]) + ".");
        for (int s = 0; s < SOURCES.length; s++) {
            final String source = SOURCES[s];
            String owner = "";
            for (int i = 0; i < ACTIONS.length; i++) {
                if (i != action && map[i].equals(source) && !source.equals("none")) owner = "   (now " + ACTION_LABELS[i] + ": they swap)";
            }
            page.addView(row(SOURCE_LABELS[s] + (source.equals(map[action]) ? "   (current)" : owner), true, v -> {
                final String old = map[action];
                if (!source.equals("none")) {
                    for (int i = 0; i < ACTIONS.length; i++) {
                        if (i != action && map[i].equals(source)) map[i] = old;  // the two swap
                    }
                }
                map[action] = source;
                writeControls(map);
                showControls();
            }));
        }
        page.addView(button("Back", false, v -> showControls()));
    }

    // --- strikers-quest r46: RetroAchievements (Lance: "its allowed to use THEIR strikers achievement
    // set"). Log in once; the page lists Super Mario Strikers' set and what this account has (RetroAch).
    // Off the UI thread throughout (Android refuses network calls there).

    private int raToken;  // the page's background work (a newer page, or Back, ends it)

    private void showRetroAch() {
        leaveTourneyPage();
        page.removeAllViews();
        onHome = false;
        final int token = ++raToken;
        title("RetroAchievements", 26);
        final RetroAch.Login login = RetroAch.saved(this);
        if (login == null) {
            note("Log in with your RetroAchievements account (retroachievements.org) to get Super Mario"
                    + " Strikers' achievements. Only a login token is kept on this Quest, never your password."
                    + " Achievements count in casual mode.");
            final EditText user = new EditText(this);
            user.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            user.setHint("Username");
            final EditText pass = new EditText(this);
            pass.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            pass.setHint("Password");
            for (EditText f : new EditText[] {user, pass}) {
                f.setTextSize(22);
                f.setTextColor(Color.WHITE);
                f.setHintTextColor(Color.GRAY);
                f.setSingleLine(true);
                f.setGravity(Gravity.CENTER);
                page.addView(f, new LinearLayout.LayoutParams(dp(360), ViewGroup.LayoutParams.WRAP_CONTENT));
            }
            final TextView status = note("");
            page.addView(button("Log in", true, v -> {
                final String u = user.getText().toString().trim(), p = pass.getText().toString();
                if (u.isEmpty() || p.isEmpty()) {
                    status.setText("Type your username and password.");
                    status.setTextColor(WARN);
                    return;
                }
                status.setText("Logging in...");
                status.setTextColor(MUTED);
                new Thread(() -> {
                    String error = null;
                    try {
                        RetroAch.login(this, u, p);
                    } catch (Exception e) {
                        error = e.getMessage() != null ? e.getMessage() : e.toString();
                    }
                    final String why = error;
                    runOnUiThread(() -> {
                        if (token != raToken) return;
                        if (why == null) {
                            showRetroAch();
                        } else {
                            status.setText("Couldn't log in: " + why);
                            status.setTextColor(WARN);
                        }
                    });
                }, "strikers-ra-login").start();
            }));
            page.addView(button("Back", false, v -> {
                ++raToken;
                showHome();
            }));
            return;
        }
        note("Logged in as " + login.user + ".");
        final String fileCheck = RetroAch.fileCheck(this);  // r46d: the game's last word on the file
        if (fileCheck != null) {
            TextView t = note(fileCheck);
            t.setTextColor(fileCheck.startsWith("✓") ? Color.rgb(140, 220, 140) : WARN);
        }
        final TextView status = note("Loading Super Mario Strikers' achievements...");
        final LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        page.addView(list, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        page.addView(button("Log out", false, v -> {
            ++raToken;
            RetroAch.logout(this);
            showRetroAch();
        }));
        page.addView(button("Back", false, v -> {
            ++raToken;
            showHome();
        }));
        new Thread(() -> {
            java.util.List<RetroAch.Achievement> set = null;
            String error = null;
            try {
                set = RetroAch.fetchSet(this, login);
            } catch (Exception e) {
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final java.util.List<RetroAch.Achievement> got = set;
            final String why = error;
            runOnUiThread(() -> {
                if (token != raToken || list.getParent() != page) return;
                if (got == null) {
                    status.setText("Couldn't load the achievements: " + why);
                    status.setTextColor(WARN);
                    return;
                }
                int have = 0, points = 0, all = 0;
                for (RetroAch.Achievement a : got) {
                    all += a.points;
                    if (a.unlocked) {
                        have++;
                        points += a.points;
                    }
                }
                status.setText(have + " of " + got.size() + " unlocked, " + points + " of " + all
                        + " points.\nThey unlock as you play, with a pop-up in the headset. (Logged in"
                        + " while the game was already running? Close it and start it again.)");
                status.setTextColor(Color.rgb(190, 200, 215));
                for (RetroAch.Achievement a : got) {
                    final String head = (a.unlocked ? "✓ " : "") + a.title + "   (" + a.points + " points)";
                    Button b = row(head + "\n" + a.description, a.unlocked, null);
                    b.setEnabled(false);
                    b.setTextColor(a.unlocked ? Color.WHITE : MUTED);
                    SpannableString text = new SpannableString(b.getText());
                    text.setSpan(new StyleSpan(Typeface.BOLD), 0, head.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                    b.setText(text);
                    list.addView(b);
                }
            });
        }, "strikers-ra-set").start();
    }

    // --- strikers-quest r44: Voice chat, voice.txt in the game's folder (src/platform/xr_voice.cpp reads
    // it at each online match and the pause panel's right stick writes it). Off until turned on here,
    // where Android's microphone question can be asked without stopping a match.

    private File voiceFile() {
        return new File(StrikersHome.homeDir(this), "voice.txt");
    }

    private boolean voiceOn() {
        return readText(voiceFile(), 8).startsWith("1");
    }

    private void writeVoice(boolean on) {
        try (java.io.OutputStream out = new java.io.FileOutputStream(voiceFile())) {
            out.write((on ? "1\n" : "0\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            android.util.Log.w("strikers", "could not write " + voiceFile(), e);
        }
    }

    private void toggleVoice() {
        if (voiceOn()) {
            writeVoice(false);
            showHome();
            return;
        }
        if (checkSelfPermission(android.Manifest.permission.RECORD_AUDIO)
                == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            voiceGranted();
        } else {
            requestPermissions(new String[] {android.Manifest.permission.RECORD_AUDIO}, REQUEST_MIC);
        }
    }

    private void voiceGranted() {
        writeVoice(true);
        // Whether the headset cancels its own speakers' sound out of the microphone (the game and the
        // friend's voice): if not, the friend hears an echo. For the log, to tell from a test.
        android.util.Log.i("strikers", "voice chat on; echo canceller "
                + (android.media.audiofx.AcousticEchoCanceler.isAvailable() ? "available" : "NOT available"));
        showHome();
        note("Voice chat is on: in online matches you hear each other. Turn it off here, or in the game's"
                + " pause screen (right stick down).");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode != REQUEST_MIC) return;
        if (results.length > 0 && results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            voiceGranted();
        } else {
            writeVoice(false);
            showHome();
            note("Voice chat needs the microphone. Press Voice chat again and choose Allow.").setTextColor(WARN);
        }
    }

    // --- strikers-quest r45: Tournaments (Lance: "people can name their teams ... and can join a
    // tournament, in which will only start when enough people join, but you can see how full tourneys
    // are or empty they are from that same menu ... 4 people tournaments and 8 people tournaments").
    // The hub (server/, Tourneys) keeps the lobbies and the brackets; this page shows them, joins and
    // leaves, starts this player's matches (a room code each, like Join) and reports their results.

    /** This headset's player id for the hub: 16 hex digits, made once and kept. */
    private String playerId() {
        String id = prefs.getString(PLAYER_ID, "");
        if (id.length() != 16) {
            StringBuilder sb = new StringBuilder();
            SecureRandom r = new SecureRandom();
            for (int i = 0; i < 16; i++) sb.append("0123456789abcdef".charAt(r.nextInt(16)));
            id = sb.toString();
            prefs.edit().putString(PLAYER_ID, id).apply();
        }
        return id;
    }

    private String teamName() {
        return prefs.getString(TEAM_NAME, "");
    }

    /** r45: the team's star player, its logo on the bracket (StarLogo), -1 none. */
    private int teamStar() {
        return prefs.getInt(TEAM_STAR, -1);
    }

    /** r45: the row of star players under the team name; the picked one is ringed in gold. */
    private void addStarPicker(final EditText name) {
        final TextView label = note("Your star player (your team's logo): " + StarLogo.name(teamStar()));
        label.setPadding(0, dp(8), 0, dp(2));
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        final android.widget.ImageView[] icons = new android.widget.ImageView[StarLogo.COUNT];
        final Runnable ring = () -> {
            for (int i = 0; i < icons.length; i++) {
                GradientDrawable bg = new GradientDrawable();
                bg.setShape(GradientDrawable.OVAL);
                bg.setColor(Color.TRANSPARENT);
                if (i == teamStar()) bg.setStroke(dp(3), ACCENT);
                icons[i].setBackground(bg);
            }
            label.setText("Your star player (your team's logo): " + StarLogo.name(teamStar()));
        };
        for (int i = 0; i < StarLogo.COUNT; i++) {
            final int star = i;
            android.widget.ImageView icon = new android.widget.ImageView(this);
            icon.setImageDrawable(new StarLogo(star));
            icon.setPadding(dp(5), dp(5), dp(5), dp(5));
            icon.setContentDescription(StarLogo.name(star));
            icon.setOnClickListener(v -> {
                prefs.edit().putInt(TEAM_STAR, star).apply();
                ring.run();
                if (tourney != null) tourney.identity(name.getText().toString().trim(), star);
            });
            icons[i] = icon;
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(54), dp(54));
            lp.setMargins(dp(3), 0, dp(3), 0);
            row.addView(icon, lp);
        }
        ring.run();
        page.addView(row, wrap());
    }

    /** r45: who has joined a lobby: their logos and names, four to a line. */
    private void addTeamChips(LinearLayout parent, org.json.JSONArray names, org.json.JSONArray stars) {
        if (names == null || names.length() == 0) return;
        LinearLayout line = null;
        for (int i = 0; i < names.length(); i++) {
            if (i % 4 == 0) {
                line = new LinearLayout(this);
                line.setOrientation(LinearLayout.HORIZONTAL);
                line.setGravity(Gravity.CENTER);
                parent.addView(line, wrap());
            }
            LinearLayout chip = new LinearLayout(this);
            chip.setOrientation(LinearLayout.HORIZONTAL);
            chip.setGravity(Gravity.CENTER_VERTICAL);
            chip.setPadding(dp(6), dp(4), dp(10), dp(4));
            android.widget.ImageView logo = new android.widget.ImageView(this);
            logo.setImageDrawable(new StarLogo(stars != null ? stars.optInt(i, -1) : -1));
            chip.addView(logo, new LinearLayout.LayoutParams(dp(26), dp(26)));
            TextView n = new TextView(this);
            n.setText(names.optString(i));
            n.setTextColor(Color.WHITE);
            n.setTextSize(14);
            n.setPadding(dp(5), 0, 0, 0);
            chip.addView(n, wrap());
            line.addView(chip, wrap());
        }
    }

    private void showTourneys() {
        leaveTourneyPage();
        page.removeAllViews();
        onHome = false;
        searching = false;
        ++quickToken;
        title("Tournaments", 26);
        note("4 or 8 players, knockout. It starts when it's full; each match is a Grudge Match, and you"
                + " come back here between rounds.");
        // The team name, kept as typed; the hub tidies it.
        TextView label = note("Your team name");
        label.setPadding(0, dp(4), 0, dp(2));
        final EditText name = new EditText(this);
        name.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_WORDS);
        name.setFilters(new InputFilter[] {new InputFilter.LengthFilter(16)});
        name.setTextSize(24);
        name.setTextColor(Color.WHITE);
        name.setHintTextColor(Color.GRAY);
        name.setHint("Team name");
        name.setGravity(Gravity.CENTER);
        name.setSingleLine(true);
        name.setImeOptions(EditorInfo.IME_ACTION_DONE);
        name.setText(teamName());
        final Runnable saveName = () -> {
            String n = name.getText().toString().trim();
            if (n.equals(teamName())) return;
            prefs.edit().putString(TEAM_NAME, n).apply();
            if (tourney != null) tourney.identity(n, teamStar());
        };
        name.setOnEditorActionListener((v, actionId, event) -> {
            saveName.run();
            return false;
        });
        name.setOnFocusChangeListener((v, focused) -> {
            if (!focused) saveName.run();
        });
        // r45b (Lance: tapping the 3/4 tournament did nothing): saved as it is typed, not only on Done,
        // so a tap on a tournament right after typing has the name.
        name.addTextChangedListener(new android.text.TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) {}
            @Override public void afterTextChanged(android.text.Editable s) {
                prefs.edit().putString(TEAM_NAME, s.toString().trim()).apply();
            }
        });
        tourneyNameField = name;
        page.addView(name, new LinearLayout.LayoutParams(dp(360), ViewGroup.LayoutParams.WRAP_CONTENT));
        addStarPicker(name);
        tourneyLink = note("Connecting...");
        tourneyLink.setTextSize(14);
        tourneyLink.setTextColor(MUTED);
        tourneyBody = new LinearLayout(this);
        tourneyBody.setOrientation(LinearLayout.VERTICAL);
        tourneyBody.setGravity(Gravity.CENTER_HORIZONTAL);
        page.addView(tourneyBody, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        page.addView(button("Back", false, v -> showHome()));
        final String key = (StrikersHome.CLASSIC.equals(StrikersHome.edition(this)) ? "C" : "P") + ONLINE_PROTOCOL;
        tourney = new TourneyClient(SERVER_HOST, key, playerId(), teamName(), teamStar(), new TourneyClient.Listener() {
            @Override public void onState(org.json.JSONObject state) {
                tourneyState = state;
                if (tourneyLink != null && tourneyLink.getCurrentTextColor() != WARN) tourneyLink.setText("");
                renderTourney();
            }

            @Override public void onLink(boolean connected, String why) {
                if (tourneyLink != null) {
                    tourneyLink.setText(why != null ? why : "");
                    tourneyLink.setTextColor(connected && why != null ? WARN : MUTED);
                }
                if (connected && why == null) sendPendingResult();
            }
        });
        tourney.start();
    }

    /** Off the Tournaments page: the hub's connection closes (a filling lobby lets go after 90 s). */
    private void leaveTourneyPage() {
        ++countdownToken;
        if (tourney != null) {
            tourney.stop();
            tourney = null;
        }
        tourneyBody = null;
        tourneyLink = null;
    }

    /** The result the game wrote after a tournament match, sent once the hub is there. */
    private void sendPendingResult() {
        final File f = new File(StrikersHome.homeDir(this), TOURNEY_RESULT);
        if (!f.isFile() || tourney == null) return;
        String mid = "", how = "played", code = "";
        int my = -1, their = -1;
        for (String line : readText(f, 400).split("\n")) {
            int eq = line.indexOf('=');
            if (eq <= 0) continue;
            String k = line.substring(0, eq).trim(), v = line.substring(eq + 1).trim();
            try {
                if (k.equals("mid")) mid = v;
                else if (k.equals("code")) code = v;
                else if (k.equals("my")) my = Integer.parseInt(v);
                else if (k.equals("their")) their = Integer.parseInt(v);
                else if (k.equals("how")) how = v;
            } catch (NumberFormatException e) {
                // a broken line: the report is left out
            }
        }
        // r45d: never counted down into again: the hub may still show this match ready for a moment,
        // and r45c started the just-played final over (the bot then quit at its 15 minutes).
        autoStartedMid = mid + "/" + code;
        if (mid.isEmpty() || my < 0 || their < 0) {
            //noinspection ResultOfMethodCallIgnored
            f.delete();
            return;
        }
        try {
            org.json.JSONObject m = new org.json.JSONObject();
            m.put("t", "result");
            m.put("mid", mid);
            m.put("my", my);
            m.put("their", their);
            m.put("how", "left".equals(how) ? "left" : "played");
            if (!code.isEmpty()) m.put("code", code);  // a report for a replayed match is refused
            if (tourney.send(m)) {
                //noinspection ResultOfMethodCallIgnored
                f.delete();
                android.util.Log.i("strikers", "tournament result sent: " + mid + " " + my + "-" + their + " " + how);
            }
        } catch (org.json.JSONException e) {
            // cannot happen with these values
        }
    }

    private void tourneyRequest(String type, String field, Object value) {
        if (tourney == null) return;
        try {
            org.json.JSONObject m = new org.json.JSONObject();
            m.put("t", type);
            if (field != null) m.put(field, value);
            if (!tourney.send(m) && tourneyLink != null) tourneyLink.setText("Not connected yet, try again in a moment.");
        } catch (org.json.JSONException e) {
            // cannot happen with these values
        }
    }

    /** "Final", "Semi-finals", "Quarter-finals", from the end. */
    private static String roundName(int round, int rounds) {
        final int fromEnd = rounds - 1 - round;
        return fromEnd == 0 ? "Final" : fromEnd == 1 ? "Semi-finals" : fromEnd == 2 ? "Quarter-finals"
                : "Round " + (round + 1);
    }

    private void renderTourney() {
        if (tourneyBody == null || tourneyState == null) return;
        tourneyBody.removeAllViews();
        final org.json.JSONObject t = tourneyState.optJSONObject("tourney");
        if (t == null) {
            renderLobbies();
        } else {
            renderBracket(t, tourneyState.optInt("you", -1));
        }
    }

    private void renderLobbies() {
        final org.json.JSONObject lobbies = tourneyState.optJSONObject("lobbies");
        final org.json.JSONObject me = tourneyState.optJSONObject("me");
        final int myLobby = me != null && !me.isNull("lobby") ? me.optInt("lobby", 0) : 0;
        if (teamName().isEmpty()) bodyNote("Pick a team name first (it shows in the lists and the bracket).", WARN);
        for (int size : new int[] {4, 8}) {
            final org.json.JSONObject l = lobbies != null ? lobbies.optJSONObject(String.valueOf(size)) : null;
            final org.json.JSONArray names = l != null ? l.optJSONArray("names") : null;
            final int count = names != null ? names.length() : 0;
            final String head = size + "-player tournament   " + count + "/" + size + " joined";
            final String sub = count == 0 ? "Nobody yet: be the first." : "Tap to join";
            final boolean in = myLobby == size;
            Button b = rowIn(tourneyBody, head + "\n" + (in ? "" : sub) + (in ? "You're in! Waiting for " + (size - count)
                    + " more..." : ""), true, v -> {
                if (in) return;
                if (teamName().isEmpty()) {
                    // r45b: said where it is seen (under the name), and the name box takes the typing.
                    if (tourneyLink != null) {
                        tourneyLink.setText("Type your team name above first, then tap the tournament again.");
                        tourneyLink.setTextColor(WARN);
                    }
                    if (tourneyNameField != null) tourneyNameField.requestFocus();
                    return;
                }
                if (tourney != null) tourney.identity(teamName(), teamStar());  // the name as typed
                tourneyRequest("join", "size", size);
                if (tourneyLink != null) {
                    tourneyLink.setText("Joining the " + size + "-player tournament...");
                    tourneyLink.setTextColor(MUTED);
                }
            });
            SpannableString text = new SpannableString(b.getText());
            text.setSpan(new RelativeSizeSpan(1.3f), 0, head.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            text.setSpan(new StyleSpan(Typeface.BOLD), 0, head.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            b.setText(text);
            addTeamChips(tourneyBody, names, l != null ? l.optJSONArray("stars") : null);  // who's in
            if (in) {
                Button leave = button("Leave the " + size + "-player tournament", false, v -> tourneyRequest("leave", null, null));
                tourneyBody.addView(leave);
            }
        }
        if (myLobby == 0) bodyNote("Tap one to join. It starts by itself when it's full.", MUTED);
        else bodyNote("Stay on this page: when it fills up, your first match starts from here.", MUTED);
    }

    private void renderBracket(org.json.JSONObject t, int you) {
        final org.json.JSONArray names = t.optJSONArray("names");
        final org.json.JSONArray rounds = t.optJSONArray("rounds");
        if (names == null || rounds == null) return;
        final int size = t.optInt("size", names.length());
        final int champion = t.isNull("champion") ? -1 : t.optInt("champion", -1);
        java.util.function.IntFunction<String> nameOf = i -> i >= 0 && i < names.length() ? names.optString(i) : "?";
        if (champion >= 0) {
            TextView c = bodyNote((champion == you ? "YOU WON THE TOURNAMENT!\n" : "Champions: ")
                    + nameOf.apply(champion), ACCENT);
            c.setTextSize(26);
            c.setTypeface(Typeface.DEFAULT_BOLD);
        }
        // This player's match: the ready one, or how the last one went.
        org.json.JSONObject mine = null, last = null;
        int mineRound = -1;
        for (int r = 0; r < rounds.length(); r++) {
            org.json.JSONArray ms = rounds.optJSONArray(r);
            for (int i = 0; ms != null && i < ms.length(); i++) {
                org.json.JSONObject m = ms.optJSONObject(i);
                if (m == null) continue;
                final boolean meIn = (!m.isNull("a") && m.optInt("a") == you) || (!m.isNull("b") && m.optInt("b") == you);
                if (!meIn) continue;
                if ("ready".equals(m.optString("status"))) {
                    mine = m;
                    mineRound = r;
                } else if ("done".equals(m.optString("status"))) {
                    last = m;
                }
            }
        }
        if (mine == null) ++countdownToken;  // r45d: no match of yours is ready: no countdown either
        if (mine != null) {
            final org.json.JSONObject m = mine;
            final boolean amA = m.optInt("a") == you;
            final int opp = amA ? (m.isNull("b") ? -1 : m.optInt("b")) : m.optInt("a");
            final String mid = m.optString("mid"), code = m.optString("code");
            final org.json.JSONObject reports = m.optJSONObject("reports");
            final boolean reported = reports != null && !reports.isNull(amA ? "a" : "b");
            final String what = roundName(mineRound, rounds.length()) + ": you vs " + nameOf.apply(opp)
                    + (m.optInt("replays", 0) > 0 ? " (replay)" : "");
            TextView h = bodyNote(what, ACCENT);
            h.setTextSize(22);
            if (reported) {
                ++countdownToken;  // r45d: a countdown still running for it stops
                bodyNote("Your result is in. Waiting for " + nameOf.apply(opp) + "'s.", MUTED);
            } else {
                tourneyBody.addView(button("Play your match", true, v -> {
                    ++countdownToken;
                    start(code, mid);
                }));
                bodyNote("Then both of you: Grudge Match, pick your team, start.", MUTED);
                // A short countdown the first time this match shows up, so a match starts without a
                // tap, but a player who just came back can still wait.
                if (!(mid + "/" + code).equals(autoStartedMid)) {  // r45d: a replay (new code) counts down again
                    autoStartedMid = mid + "/" + code;
                    final TextView count = bodyNote("Starting in 10...", Color.WHITE);
                    final int token = ++countdownToken;
                    for (int s = 9; s >= 0; s--) {
                        final int left = s;
                        page.postDelayed(() -> {
                            if (token != countdownToken || tourneyBody == null) return;
                            if (left > 0) {
                                count.setText("Starting in " + left + "...");
                            } else {
                                start(code, mid);
                            }
                        }, (10 - s) * 1000L);
                    }
                    tourneyBody.addView(button("Wait", false, v -> {
                        ++countdownToken;
                        count.setText("Press Play your match when you're ready.");
                    }));
                }
                final long readyAt = m.optLong("readyAt", 0);
                if (readyAt > 0 && System.currentTimeMillis() - readyAt > 5 * 60 * 1000L) {
                    tourneyBody.addView(button("Opponent never came? Claim the win", false,
                            v -> tourneyRequest("noshow", "mid", mid)));
                }
            }
        } else if (champion < 0 && last != null) {
            ++countdownToken;  // r45d: nothing to count down to
            final boolean won = !last.isNull("winner") && last.optInt("winner") == you;
            bodyNote(won ? "You won! Waiting for your next opponent..." : "You're out. Thanks for playing!",
                    won ? ACCENT : MUTED);
        }
        // The whole tree (Lance: "a playoff tree ... with the star player ... being the logo and the team
        // name being shown on the bracket"), scrolling sideways when it is wider than the panel.
        android.widget.HorizontalScrollView sideways = new android.widget.HorizontalScrollView(this);
        sideways.setHorizontalScrollBarEnabled(true);
        BracketView tree = new BracketView(this);
        tree.set(t, you);
        sideways.addView(tree);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(10);
        tourneyBody.addView(sideways, lp);
        bodyNote(size + "-player tournament", MUTED);
    }

    private TextView bodyNote(String text, int colour) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextColor(colour);
        t.setTextSize(17);
        t.setGravity(Gravity.CENTER);
        t.setPadding(0, dp(6), 0, dp(6));
        tourneyBody.addView(t, wrap());
        return t;
    }

    /** row(), but into `parent`; onClick null = shown, not pressable. */
    private Button rowIn(LinearLayout parent, String text, boolean highlight, View.OnClickListener onClick) {
        Button b = row(text, highlight, onClick);
        if (onClick == null) {
            b.setEnabled(false);
            b.setTextColor(highlight ? Color.WHITE : Color.rgb(200, 208, 220));
        }
        parent.addView(b);
        return b;
    }

    /** strikers-quest r44: the panel goes out of sight (headset off, Quest menu): stop searching. */
    @Override
    protected void onStop() {
        super.onStop();
        if (searching) {
            ++quickToken;
            searching = false;
            showHome();
        }
        // r45: out of sight on the Tournaments page: the hub's connection goes (a filling lobby lets
        // this player go after 90 s), and comes back with the page.
        if (tourney != null) {
            ++countdownToken;
            tourney.stop();
            tourney = null;
            wasOnTourneys = true;
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (wasOnTourneys) {
            wasOnTourneys = false;
            showTourneys();
        }
    }

    /**
     * strikers-quest r44, out of start(): the file access switch and a readable game file, before a
     * match (or a quick-match search) is started. False when it went to ask for one of them instead.
     */
    private boolean readyToStart(String code) {
        // strikers-quest r39b: the disc usually sits in Download, which takes "all files access". A
        // friend who only installed the APK has not given it yet: open that switch from here and
        // stay on this page, rather than start the game only for it to open the switch and close.
        // Not when the disc is in the app's own folder, which needs no permission.
        // r41: asked whenever the disc is not in the app's own folder (a picked file is elsewhere).
        if (!StrikersHome.hasAllFilesAccess()
                && !StrikersHome.hasGameDataInHome(StrikersHome.homeDir(this))
                && askForFileAccess(this)) {
            if (accessNote == null || accessNote.getParent() != page) accessNote = note("");
            accessNote.setText("Turn on \"Allow access to manage all files\" so the game can read your"
                    + " disc file, then come back here and press the same button again.");
            return false;
        }
        // r41: no game file, or one the game cannot read: say so and show the list, rather than
        // start a game that closes at once (alpha 1: "it just closes").
        String problem = gameProblem();
        if (problem != null) {
            showChooser(code, problem);
            return false;
        }
        return true;
    }

    private void start(String code) {
        start(code, "");
    }

    /** r45: `tourneyMid`, a tournament match's id ("" for none): the game reports how it ends. */
    private void start(String code, String tourneyMid) {
        if (!code.isEmpty() && tourneyMid.isEmpty()) prefs.edit().putString(LAST_CODE, code).apply();
        if (!readyToStart(code)) return;
        Intent game = new Intent(this, StrikersActivity.class);
        game.putExtra(EXTRA_ROOM, code);
        game.putExtra(EXTRA_EDITION, StrikersHome.edition(this));  // strikers-quest r43
        game.putExtra(EXTRA_TOURNEY, tourneyMid);  // r45
        leaveTourneyPage();
        startActivity(game);
        finish();
    }

    /**
     * strikers-quest: the "all files access" switch for this app; false when the system has no such
     * screen. r39b: moved here from StrikersActivity, which still uses it when started directly.
     */
    static boolean askForFileAccess(Activity activity) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return false;
        try {
            activity.startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + activity.getPackageName())));
            return true;
        } catch (Exception e) {
            try {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                return true;
            } catch (Exception e2) {
                return false;
            }
        }
    }

    private static String newCode() {
        SecureRandom r = new SecureRandom();
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 4; i++) sb.append(CODE_LETTERS.charAt(r.nextInt(CODE_LETTERS.length())));
        return sb.toString();
    }

    static String clean(String s) {
        StringBuilder sb = new StringBuilder();
        for (char c : s.toUpperCase().toCharArray()) {
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) sb.append(c);
            if (sb.length() == 8) break;
        }
        return sb.toString();
    }

    private static String spaced(String code) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < code.length(); i++) {
            if (i > 0) sb.append(' ');
            sb.append(code.charAt(i));
        }
        return sb.toString();
    }

    private void title(String text, int sp) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextColor(Color.WHITE);
        t.setTextSize(sp);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setGravity(Gravity.CENTER);
        page.addView(t, wrap());
    }

    private TextView note(String text) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextColor(Color.rgb(190, 200, 215));
        t.setTextSize(16);
        t.setGravity(Gravity.CENTER);
        t.setPadding(0, dp(6), 0, dp(14));
        page.addView(t, wrap());
        return t;
    }

    private Button button(String text, boolean primary, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(20);
        b.setTextColor(primary ? Color.BLACK : Color.WHITE);
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(14));
        bg.setColor(primary ? ACCENT : CARD);
        b.setBackground(bg);
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(360), dp(64));
        lp.topMargin = dp(10);
        b.setLayoutParams(lp);
        return b;
    }

    /** r41: one file in the game file list; onClick null = shown but not pickable. */
    private Button row(String text, boolean usable, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(15);
        b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        b.setTextColor(usable ? Color.WHITE : MUTED);
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(dp(12));
        bg.setColor(CARD);
        if (usable) bg.setStroke(dp(2), ACCENT);
        b.setBackground(bg);
        int p = dp(14);
        b.setPadding(p, p, p, p);
        b.setEnabled(onClick != null);
        b.setOnClickListener(onClick);
        // r43b: the panel's width, wrapping (Lance: the Version rows ran past the panel at 620 dp).
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    private String versionName() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "";
        }
    }

    private String versionText() {
        String v = versionName();
        return v.isEmpty() ? "" : "Version " + v;
    }

    private static String readText(File f, int max) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[max];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8).trim() : "";
        } catch (IOException e) {
            return "";
        }
    }

    private LinearLayout.LayoutParams wrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
