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
import android.text.TextUtils;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.text.style.TypefaceSpan;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
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
 *
 * <p>r157 (menu overhaul, MENUS-PLAN.md; Lance: "easy to navigate, right now its just easy to get lost"): HOME is
 * five tiles (PLAY with the game file + version on it, ONLINE, GUESTS, SETTINGS, UPDATE with a NEW badge), every
 * other page has a breadcrumb + a BACK pill in the same spot, SETTINGS has a rail (GAME, CONTROLS, SOUND, HOME
 * GROUND, ACHIEVEMENTS), the notes that used to stack over the buttons are one notice strip with GOT IT, a green
 * check marks what's chosen, and a hint bar sits at the bottom. B / BACK goes back one level (hold it: HOME). Each
 * page's own logic is as before; MENUS-INVENTORY.md lists where every option went.
 */
public class LobbyActivity extends Activity {

    // strikers-quest r44: the room server (src/platform/xr_net.cpp kServerHost) and the online protocol
    // (kProtocol there): quick match only pairs headsets that can play each other.
    private static final String SERVER_HOST = "strikers-rooms.strikers-rooms.workers.dev";
    // r76: 10 = xr_net.cpp kProtocol (franchise co-op). It had stayed at 7 since r63 while the game went to 8, 9:
    // quick match could pair builds that then refused each other.
    private static final int ONLINE_PROTOCOL = 14;  // r78: the co-captains in the franchise start; r79: in the choice; r80: guest sidekicks' own stats; r86: 4-minute franchise matches
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
    // strikers-quest r64: true when this headset made the room (Host an online match): in co-op Cup
    // the maker's cup is played and the maker plays the captain. hostedCode = the code shown by Host.
    static final String EXTRA_ROOM_MAKER = "roomMaker";
    private String hostedCode = "";
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
    // r157 (menu overhaul): the hub's navy + gold (LobbyKit, from tools/outside/menus_kit.py) for the old grey-blue
    private static final int CARD = LobbyKit.TILE_HI;
    private static final int ACCENT = LobbyKit.GOLD;
    private static final int WARN = LobbyKit.WARN;
    private static final int MUTED = LobbyKit.INK_DIM;
    private static final int OK_GREEN = Color.rgb(140, 220, 140);

    private LinearLayout page;
    // r157 (menu overhaul): the chrome round every page. HOME is the tile grid (homeView); every other page is `page`
    // (in pageScroll, with the SETTINGS rail on its left), under the breadcrumb (crumbs: BACK pill + path) and the
    // notice strip, over the hint bar.
    private Typeface lucky;
    private LinearLayout crumbs, rail, pageRow, noticeStrip, homeView, crumbRow, noticeRow;
    private ScrollView pageScroll;
    private TextView noticeText, versionPill;
    private View hintBack, hintHold;
    private Button noticeGotIt;
    private Runnable backAction;       // where B / BACK goes from the page up now; null on HOME
    private String flashNote;          // a note after an action (update permission, file access, voice chat ...)
    private int flashColour;
    private boolean onUpdate;          // the UPDATE page is up (the update check redraws it)
    private boolean bHeld;             // B held past HOLD_B_MS: it went HOME, its release does nothing more
    private static final long HOLD_B_MS = 600;
    // r157 round 2 (MENUS-PLAN.md rule 9: every word at least 40 px at panel size): the mockups are the 1024 x 640 dp
    // panel at 1920 x 1200 px, so 40 px = 21.3 dp; Luckiest Guy's line is its em (ascent + descent = 1.001 em), so no
    // text on the launcher is under 22 sp (tools/outside/launcher_preview.py checks it)
    static final int MIN_SP = 22;
    // the SETTINGS rail (MENUS-PLAN.md section 2)
    private static final int RAIL_GAME = 0, RAIL_CONTROLS = 1, RAIL_SOUND = 2, RAIL_HOME_GROUND = 3, RAIL_ACH = 4;
    private static final String[] RAIL_NAMES = {"GAME", "CONTROLS", "SOUND", "HOME GROUND", "ACHIEVEMENTS"};
    private SharedPreferences prefs;
    private int chooserToken;     // r41: the game file list being filled (a newer one wins)
    private Updater.Release update;  // r41: a newer release on GitHub, null when there is none
    private boolean onHome;          // r41: the home page is showing (the update check redraws it)
    // r147: the menu music on this panel (MenuMusic: the lofi + his songs while the panel is in sight) and its NOW
    // PLAYING card in the top right corner
    private MenuMusic music;
    private TextView nowPlaying;
    private int nowPlayingToken;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        lucky = LobbyKit.font(this);
        buildChrome();
        music = MenuMusic.forPanel(this, this::showNowPlaying);
        if (SHOW_TOURNEY.equals(getIntent().getStringExtra(EXTRA_SHOW))) {
            showTourneys();  // r45: back from a tournament match
        } else {
            showHome();
        }
        checkForUpdate();
        fetchBadgesQuietly();
        // r197: the room scan's permission, once, with its reason (a moment after the panel is up)
        getWindow().getDecorView().postDelayed(() -> {
            if (!isFinishing()) maybeAskScene();
        }, 1500);
    }

    /**
     * strikers-quest r57: with a RetroAchievements login, the set (when not yet saved) and its badge
     * pictures, for the trophy shelf's board in the game; once per panel start, off the UI thread,
     * silently (only pictures not yet saved are downloaded).
     */
    private void fetchBadgesQuietly() {
        final RetroAch.Login login = RetroAch.saved(this);
        if (login == null) return;
        new Thread(() -> {
            try {
                if (!RetroAch.file(this, RetroAch.SET_FILE).isFile()) RetroAch.fetchSet(this, login);
                RetroAch.fetchBadges(this);
            } catch (Exception e) {
                // no network now: the next panel start tries again
            }
        }, "strikers-ra-badges").start();
    }

    // ------------------------------------------------------------------ r157 (menu overhaul): the chrome

    /**
     * r157 (menu overhaul): the frame every page sits in (MENUS-PLAN.md section 1): the title bar (QUEST 3 STRIKERS +
     * the release pill, NOW PLAYING over its right end), the breadcrumb row (BACK pill + where you are, the notice
     * strip beside it), the body (HOME's tiles, or a page with the SETTINGS rail on its left), and the hint bar.
     */
    private void buildChrome() {
        final int match = ViewGroup.LayoutParams.MATCH_PARENT, wrapped = ViewGroup.LayoutParams.WRAP_CONTENT;
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(LobbyKit.NAVY_BOT);
        LinearLayout shell = new LinearLayout(this);
        shell.setOrientation(LinearLayout.VERTICAL);
        shell.setBackground(LobbyKit.frame(getResources().getDisplayMetrics().density));
        shell.setPadding(dp(18), dp(8), dp(18), dp(10));
        root.addView(shell, new FrameLayout.LayoutParams(match, match));

        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        TextView name = luckyText("", 32, LobbyKit.CREAM);
        SpannableString n = new SpannableString("QUEST 3  STRIKERS");
        n.setSpan(new ForegroundColorSpan(ACCENT), 0, 7, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        n.setSpan(new RelativeSizeSpan(0.82f), 0, 7, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        name.setAllCaps(false);  // already capitals; the caps transform would drop the colour spans
        name.setText(n);
        name.setShadowLayer(dp(3), 0, dp(2), 0xC0000000);
        head.addView(name, wrap());
        versionPill = luckyText(releaseName(), MIN_SP, LobbyKit.INK_SOFT);
        versionPill.setBackground(LobbyKit.round(0xFF0E1428, dp(14)));
        versionPill.setPadding(dp(12), dp(5), dp(12), dp(3));
        LinearLayout.LayoutParams vlp = wrap();
        vlp.leftMargin = dp(14);
        head.addView(versionPill, vlp);
        shell.addView(head, new LinearLayout.LayoutParams(match, dp(52)));

        crumbRow = new LinearLayout(this);
        crumbRow.setOrientation(LinearLayout.HORIZONTAL);
        crumbRow.setGravity(Gravity.CENTER_VERTICAL);
        crumbRow.setMinimumHeight(dp(46));
        crumbs = new LinearLayout(this);
        crumbs.setOrientation(LinearLayout.HORIZONTAL);
        crumbs.setGravity(Gravity.CENTER_VERTICAL);
        crumbRow.addView(crumbs, wrap());
        noticeStrip = new LinearLayout(this);
        noticeStrip.setOrientation(LinearLayout.HORIZONTAL);
        noticeStrip.setGravity(Gravity.CENTER_VERTICAL);
        noticeStrip.setPadding(dp(12), dp(3), dp(5), dp(3));
        ImageView bang = new ImageView(this);
        bang.setImageDrawable(new LobbyKit.Icon(LobbyKit.ICON_WARN, LobbyKit.NOTICE_EDGE));
        noticeStrip.addView(bang, new LinearLayout.LayoutParams(dp(22), dp(28)));
        noticeText = luckyText("", MIN_SP, Color.WHITE);
        noticeText.setMaxLines(2);
        noticeText.setEllipsize(TextUtils.TruncateAt.END);
        noticeText.setPadding(dp(8), dp(2), dp(8), 0);
        noticeStrip.addView(noticeText, new LinearLayout.LayoutParams(0, wrapped, 1f));
        noticeGotIt = pill("GOT IT", ACCENT, 0xFF14080C, v -> dismissNotice());
        noticeStrip.addView(noticeGotIt, wrap());
        noticeStrip.setVisibility(View.GONE);
        crumbRow.addView(noticeStrip, noticeBeside());
        shell.addView(crumbRow, new LinearLayout.LayoutParams(match, wrapped));
        // round 2: on a page the breadcrumb can take most of the row (BACK + 3 parts at 22 sp), so there the strip
        // gets the row under it instead (refreshNotice moves it); on HOME it stays beside the HOME chip
        noticeRow = new LinearLayout(this);
        noticeRow.setOrientation(LinearLayout.HORIZONTAL);
        shell.addView(noticeRow, new LinearLayout.LayoutParams(match, wrapped));

        FrameLayout body = new FrameLayout(this);
        homeView = new LinearLayout(this);
        homeView.setOrientation(LinearLayout.HORIZONTAL);
        body.addView(homeView, new FrameLayout.LayoutParams(match, match));
        pageRow = new LinearLayout(this);
        pageRow.setOrientation(LinearLayout.HORIZONTAL);
        rail = new LinearLayout(this);
        rail.setOrientation(LinearLayout.VERTICAL);
        rail.setPadding(0, 0, dp(14), 0);
        pageRow.addView(rail, new LinearLayout.LayoutParams(dp(236), match));
        page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setGravity(Gravity.CENTER_HORIZONTAL);
        page.setPadding(dp(6), dp(2), dp(6), dp(14));
        // r41: scrolls, for the game file list and a last error on a short panel.
        pageScroll = new ScrollView(this);
        pageScroll.setFillViewport(true);
        pageScroll.addView(page, new ScrollView.LayoutParams(match, wrapped));
        pageRow.addView(pageScroll, new LinearLayout.LayoutParams(0, match, 1f));
        body.addView(pageRow, new FrameLayout.LayoutParams(match, match));
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(match, 0, 1f);
        blp.topMargin = dp(6);
        shell.addView(body, blp);

        // the hint bar (MENUS-PLAN.md rule 3): the pointer, then B, then the extra
        LinearLayout hints = new LinearLayout(this);
        hints.setOrientation(LinearLayout.HORIZONTAL);
        hints.setGravity(Gravity.CENTER);
        hints.setBackground(LobbyKit.round(0xE005070E, dp(22)));
        hints.addView(hint("RT", LobbyKit.SLATE, "POINT + PULL TO PICK"));
        hintBack = hint("B", LobbyKit.B_RED, "BACK");
        hints.addView(hintBack);
        hintHold = hint("B", LobbyKit.B_RED, "HOLD: HOME");
        hints.addView(hintHold);
        LinearLayout.LayoutParams hlp = new LinearLayout.LayoutParams(match, dp(50));
        hlp.topMargin = dp(8);
        shell.addView(hints, hlp);

        // r147: the NOW PLAYING card, over the title bar's right end
        nowPlaying = new TextView(this);
        nowPlaying.setTextColor(Color.WHITE);
        nowPlaying.setTypeface(lucky);
        nowPlaying.setTextSize(MIN_SP);
        nowPlaying.setMaxWidth(dp(440));
        nowPlaying.setSingleLine(false);
        nowPlaying.setMaxLines(2);
        nowPlaying.setEllipsize(TextUtils.TruncateAt.END);
        nowPlaying.setPadding(dp(16), dp(4), dp(18), dp(4));  // round 2: 52 dp, clear of the breadcrumb row
        nowPlaying.setCompoundDrawablePadding(dp(10));
        LobbyKit.Icon noteIcon = new LobbyKit.Icon(LobbyKit.ICON_NOTE, ACCENT);
        noteIcon.setBounds(0, 0, dp(28), dp(28));
        nowPlaying.setCompoundDrawables(noteIcon, null, null, null);
        GradientDrawable card = new GradientDrawable();
        card.setCornerRadius(dp(16));
        card.setColor(Color.argb(240, 14, 20, 40));
        card.setStroke(dp(3), ACCENT);
        nowPlaying.setBackground(card);
        nowPlaying.setAlpha(0f);
        nowPlaying.setVisibility(View.GONE);
        FrameLayout.LayoutParams npLp = new FrameLayout.LayoutParams(wrapped, wrapped, Gravity.TOP | Gravity.END);
        npLp.topMargin = dp(4);
        npLp.rightMargin = dp(18);
        root.addView(nowPlaying, npLp);
        setContentView(root);
    }

    /** r157: a line in Luckiest Guy (the game's font), capitals, as every heading, tile and button. */
    private TextView luckyText(String text, int sp, int colour) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTypeface(lucky);
        t.setAllCaps(true);
        t.setTextSize(Math.max(sp, MIN_SP));  // round 2: never under the 40 px rule
        t.setTextColor(colour);
        t.setIncludeFontPadding(false);
        return t;
    }

    /**
     * r157 round 2: a title that must stay on one line (the tiles): `sp` at most, shrinking to MIN_SP to fit the
     * tile's width (Android's own uniform autosize); launcher_preview.py flags any that wouldn't fit even at MIN_SP.
     */
    private TextView fitText(String text, int sp, int colour) {
        TextView t = luckyText(text, sp, colour);
        t.setMaxLines(1);  // not setSingleLine: its horizontal scrolling turns autosize off
        t.setGravity(Gravity.CENTER);
        t.setAutoSizeTextTypeUniformWithConfiguration(MIN_SP, Math.max(sp, MIN_SP), 1,
                android.util.TypedValue.COMPLEX_UNIT_SP);
        return t;
    }

    /** r157: one hint in the hint bar: the button's glyph (its colour, as the game's prompts) + what it does. */
    private View hint(String glyph, int colour, String words) {
        LinearLayout h = new LinearLayout(this);
        h.setOrientation(LinearLayout.HORIZONTAL);
        h.setGravity(Gravity.CENTER_VERTICAL);
        h.setPadding(dp(14), 0, dp(14), 0);
        TextView g = luckyText(glyph, 14, Color.WHITE);
        g.setGravity(Gravity.CENTER);
        GradientDrawable bg = LobbyKit.round(colour, dp(20));
        bg.setStroke(dp(2), 0x60FFFFFF);
        g.setBackground(bg);
        g.setPadding(dp(glyph.length() > 1 ? 10 : 0), dp(3), dp(glyph.length() > 1 ? 10 : 0), 0);
        g.setMinWidth(dp(40));
        h.addView(g, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)));
        TextView w = luckyText(words, MIN_SP, Color.WHITE);
        w.setPadding(dp(10), dp(3), 0, 0);
        h.addView(w, wrap());
        return h;
    }

    /** r157: a small pressable pill (BACK, GOT IT, CHANGE ...). */
    private Button pill(String text, int ink, int fill, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(text);
        b.setTypeface(lucky);
        b.setAllCaps(true);
        b.setTextSize(MIN_SP);
        b.setTextColor(ink);
        b.setIncludeFontPadding(false);
        b.setMinHeight(dp(40));
        b.setMinimumHeight(dp(40));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setPadding(dp(16), dp(4), dp(16), dp(2));
        GradientDrawable bg = LobbyKit.round(fill, dp(20));
        bg.setStroke(dp(2), 0x50FFFFFF);
        b.setBackground(LobbyKit.pressable(bg, 0, dp(20), getResources().getDisplayMetrics().density));
        b.setStateListAnimator(null);
        b.setOnClickListener(onClick);
        b.setEnabled(onClick != null);  // a breadcrumb part with nowhere to go: shown, no ring
        return b;
    }

    /**
     * r157: every page but HOME starts here. `colour` lights the last part of the breadcrumb (the section's colour),
     * `back` is where B / the BACK pill goes, `railAt` the SETTINGS rail's lit row (-1: no rail), `path` the
     * breadcrumb after HOME. Whatever the last page had running (a tournament link, a quick match search, its
     * countdown, a RetroAchievements load) ends, as each page's own Back did before.
     */
    private void openPage(int colour, Runnable back, int railAt, String... path) {
        leaveTourneyPage();  // r45
        ++quickToken;        // r44: a search, or a "player found" countdown, stops with its page
        searching = false;
        ++raToken;           // r46
        page.removeAllViews();
        onHome = false;
        onGuests = false;
        onUpdate = false;
        flashNote = null;
        backAction = back;
        homeView.removeAllViews();
        homeView.setVisibility(View.GONE);
        pageRow.setVisibility(View.VISIBLE);
        rail.setVisibility(railAt >= 0 ? View.VISIBLE : View.GONE);
        if (railAt >= 0) fillRail(railAt);
        pageScroll.scrollTo(0, 0);
        setCrumbs(colour, path);
        hintBack.setAlpha(1f);
        hintHold.setVisibility(path.length >= 2 ? View.VISIBLE : View.GONE);
        refreshNotice();
    }

    /** r157: BACK pill (not on HOME), then HOME > ... > the page, the last part lit; the others jump there. */
    private void setCrumbs(int colour, String[] path) {
        crumbs.removeAllViews();
        if (backAction != null) {
            Button back = pill("BACK", Color.WHITE, 0xFF0E1428, v -> goBack());
            LobbyKit.Icon arrow = new LobbyKit.Icon(LobbyKit.ICON_BACK, Color.WHITE);
            arrow.setBounds(0, 0, dp(20), dp(20));
            back.setCompoundDrawables(arrow, null, null, null);
            back.setCompoundDrawablePadding(dp(6));
            LinearLayout.LayoutParams lp = wrap();
            lp.rightMargin = dp(12);
            crumbs.addView(back, lp);
        }
        String[] all = new String[path.length + 1];
        all[0] = "HOME";
        System.arraycopy(path, 0, all, 1, path.length);
        for (int i = 0; i < all.length; i++) {
            final boolean last = i == all.length - 1;
            if (i > 0) {
                // round 2: a drawn gold arrow (the font has none, and the system's fallback isn't sure on Horizon)
                ImageView sep = new ImageView(this);
                sep.setImageDrawable(new LobbyKit.Icon(LobbyKit.ICON_CRUMB, ACCENT));
                LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(dp(14), dp(18));
                slp.leftMargin = dp(6);
                slp.rightMargin = dp(6);
                crumbs.addView(sep, slp);
            }
            if (last) {
                TextView lit = luckyText(all[i], 19, colour == ACCENT ? LobbyKit.NAVY_BOT : Color.WHITE);
                lit.setBackground(LobbyKit.round(colour, dp(16)));
                lit.setPadding(dp(12), dp(6), dp(12), dp(3));
                lit.setShadowLayer(colour == ACCENT ? 0 : dp(2), 0, dp(1), 0x90000000);
                crumbs.addView(lit, wrap());
            } else {
                final Runnable to = crumbTarget(all[i]);
                Button part = pill(all[i], LobbyKit.INK_SOFT, 0x00000000, to != null ? v -> to.run() : null);
                part.setTextSize(MIN_SP);
                part.setPadding(dp(8), dp(4), dp(8), dp(2));
                crumbs.addView(part, wrap());
            }
        }
    }

    /** r157: where a breadcrumb part goes (HOME is "the top" from anywhere). */
    private Runnable crumbTarget(String part) {
        switch (part) {
            case "HOME": return this::showHome;
            case "ONLINE": return this::showOnline;
            case "TOURNAMENTS": return this::showTourneys;
            case "GUESTS": return this::showGuestsHub;
            case "MAKE GUESTS": return this::showGuests;
            case "WHO PLAYS WHO": return this::showCharacters;
            case "SETTINGS":
            case "GAME": return this::showGameSettings;
            case "CONTROLS": return this::showControls;
            case "SOUND": return this::showSound;
            case "HOME GROUND": return this::showHomeGround;
            case "ACHIEVEMENTS": return this::showRetroAch;
            case "UPDATE": return this::showUpdate;
            default: return null;
        }
    }

    /** r157: the SETTINGS rail, `at` lit in the section's teal. */
    private void fillRail(int at) {
        rail.removeAllViews();
        final float density = getResources().getDisplayMetrics().density;
        for (int i = 0; i < RAIL_NAMES.length; i++) {
            final int item = i;
            Button b = new Button(this);
            b.setText(RAIL_NAMES[i]);
            b.setTypeface(lucky);
            b.setAllCaps(true);
            b.setTextSize(24);
            b.setMaxLines(1);  // round 2: one line, shrinking to MIN_SP before it would wrap
            b.setAutoSizeTextTypeUniformWithConfiguration(MIN_SP, 24, 1, android.util.TypedValue.COMPLEX_UNIT_SP);
            b.setIncludeFontPadding(false);
            b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            b.setPadding(dp(16), dp(4), dp(8), 0);
            b.setTextColor(i == at ? Color.WHITE : LobbyKit.INK_SOFT);
            if (i == at) b.setShadowLayer(dp(2), 0, dp(1), 0x90000000);
            b.setBackground(LobbyKit.pressable(LobbyKit.round(i == at ? LobbyKit.SETTINGS : LobbyKit.TILE, dp(16)), 0,
                    dp(16), density));
            b.setStateListAnimator(null);
            b.setOnClickListener(v -> railOpen(item));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(60));
            lp.bottomMargin = dp(8);
            rail.addView(b, lp);
        }
    }

    private void railOpen(int item) {
        switch (item) {
            case RAIL_CONTROLS: showControls(); break;
            case RAIL_SOUND: showSound(); break;
            case RAIL_HOME_GROUND: showHomeGround(); break;
            case RAIL_ACH: showRetroAch(); break;
            default: showGameSettings(); break;
        }
    }

    /** r157: B / BACK, one level up (on HOME: nothing, so a stray B doesn't close the panel). */
    private void goBack() {
        if (backAction != null) backAction.run();
    }

    /**
     * r157 (MENUS-PLAN.md rule 2): B, or the system's BACK (the Quest's menu button on a panel, a pad's B), goes
     * back one level; held, it goes HOME. A text box's keyboard takes its own BACK first (the system hands it to the
     * keyboard before the panel sees it).
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        final int k = e.getKeyCode();
        if (k == KeyEvent.KEYCODE_BACK || k == KeyEvent.KEYCODE_BUTTON_B || k == KeyEvent.KEYCODE_ESCAPE) {
            if (e.getAction() == KeyEvent.ACTION_DOWN) {
                if (e.getRepeatCount() == 0) {
                    bHeld = false;
                } else if (!bHeld && e.getEventTime() - e.getDownTime() >= HOLD_B_MS) {
                    bHeld = true;
                    if (!onHome) showHome();
                }
                return true;
            }
            if (e.getAction() == KeyEvent.ACTION_UP) {
                final boolean held = bHeld || e.getEventTime() - e.getDownTime() >= HOLD_B_MS;
                if (!e.isCanceled()) {
                    if (held && !bHeld) {
                        if (!onHome) showHome();
                    } else if (!held) {
                        goBack();
                    }
                }
                bHeld = false;
                return true;
            }
        }
        return super.dispatchKeyEvent(e);
    }

    // ------------------------------------------------------------------ r157: the notice strip

    /** r157: a note after an action (it used to go over the buttons), in the strip until GOT IT or the next page. */
    private void notice(String text, int colour) {
        flashNote = text;
        flashColour = colour;
        refreshNotice();
    }

    /**
     * r157 (MENUS-PLAN.md launcher fix 4): one strip beside the breadcrumb, one note at a time: a note after an
     * action first, then why online didn't start (r43), then the crash note (r41; its full text is on UPDATE).
     */
    private void refreshNotice() {
        String text = null;
        int colour = WARN;
        View.OnClickListener tap = null;
        if (flashNote != null) {
            text = flashNote;
            colour = flashColour;
        } else {
            text = onlineNoteText();
            if (text == null) {
                text = crashLine();
                if (text != null) tap = v -> showUpdate();
            }
        }
        // round 2: beside the HOME chip on HOME, its own row under the breadcrumb on a page
        final ViewGroup want = onHome ? crumbRow : noticeRow;
        if (noticeStrip.getParent() != want) {
            ((ViewGroup) noticeStrip.getParent()).removeView(noticeStrip);
            if (onHome) {
                crumbRow.addView(noticeStrip, noticeBeside());
            } else {
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.topMargin = dp(4);
                noticeRow.addView(noticeStrip, lp);
            }
        }
        if (text == null) {
            noticeStrip.setVisibility(View.GONE);
            return;
        }
        final boolean bad = colour == WARN;
        GradientDrawable bg = LobbyKit.round(bad ? LobbyKit.NOTICE_BG : 0xFF14203E, dp(20));
        bg.setStroke(dp(3), bad ? LobbyKit.NOTICE_EDGE : ACCENT);
        noticeStrip.setBackground(bg);
        noticeText.setText(text);
        noticeText.setTextColor(bad ? Color.WHITE : LobbyKit.CREAM);
        noticeText.setOnClickListener(tap);
        noticeText.setClickable(tap != null);
        noticeStrip.setVisibility(View.VISIBLE);
    }

    private LinearLayout.LayoutParams noticeBeside() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        lp.leftMargin = dp(14);
        return lp;
    }

    /** r157: GOT IT clears the note shown (the online note's and the crash note's files go, as their OK did). */
    private void dismissNotice() {
        if (flashNote != null) {
            flashNote = null;
        } else if (onlineNoteText() != null) {
            //noinspection ResultOfMethodCallIgnored
            new File(StrikersHome.homeDir(this), ONLINE_NOTE).delete();
        } else {
            //noinspection ResultOfMethodCallIgnored
            crashFile().delete();
            if (onUpdate) {
                showUpdate();
                return;
            }
        }
        refreshNotice();
    }

    // ------------------------------------------------------------------ r157: HOME and the section pages

    /** r157 (menu overhaul): HOME, five tiles, nothing scrolls: PLAY big, then ONLINE, GUESTS, SETTINGS, UPDATE. */
    private void showHome() {
        leaveTourneyPage();
        ++quickToken;
        searching = false;
        ++raToken;
        page.removeAllViews();
        onHome = true;
        onGuests = false;
        onUpdate = false;
        flashNote = null;
        backAction = null;
        rail.setVisibility(View.GONE);
        pageRow.setVisibility(View.GONE);
        homeView.setVisibility(View.VISIBLE);
        homeView.removeAllViews();
        crumbs.removeAllViews();  // HOME alone: a gold chip, no BACK pill, no path
        TextView here = luckyText("HOME", 19, LobbyKit.NAVY_BOT);
        here.setBackground(LobbyKit.round(ACCENT, dp(16)));
        here.setPadding(dp(12), dp(6), dp(12), dp(3));
        crumbs.addView(here, wrap());
        hintBack.setAlpha(0.35f);  // nothing to go back to: faded, in its place (MENUS-PLAN.md rule 3)
        hintHold.setVisibility(View.GONE);

        // round 2: our own drawn ball + goal burst (LobbyKit.Icon ICON_PLAY_ART), not the app icon; no game art ships
        View play = tile(LobbyKit.PLAY, new LobbyKit.Icon(LobbyKit.ICON_PLAY_ART, Color.WHITE), "PLAY", playLine(),
                null, true, v -> start(""));
        LinearLayout.LayoutParams plp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.47f);
        plp.rightMargin = dp(12);
        homeView.addView(play, plp);
        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        homeView.addView(right, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 0.53f));
        final boolean guestsMade = io.github.newcoke.strikers.guests.GuestSetup.made(StrikersHome.homeDir(this));
        LinearLayout top = tileRow(right, true);
        top.addView(tile(LobbyKit.ONLINE, new LobbyKit.Icon(LobbyKit.ICON_GLOBE, Color.WHITE), "ONLINE",
                "PLAY A FRIEND, TOURNAMENTS", null, false, v -> showOnline()), tileLp(true));
        top.addView(tile(LobbyKit.GUESTS, new LobbyKit.Icon(LobbyKit.ICON_PEOPLE, Color.WHITE), "GUESTS",
                guestsMade ? "MAKE + SWAP THEM" : "MAKE YOURS FIRST", guestsMade ? null : "NEW", false,
                v -> showGuestsHub()), tileLp(false));
        LinearLayout bottom = tileRow(right, false);
        bottom.addView(tile(LobbyKit.SETTINGS, new LobbyKit.Icon(LobbyKit.ICON_GEAR, Color.WHITE), "SETTINGS",
                "CONTROLS, SOUND, MORE", null, false, v -> showGameSettings()), tileLp(true));
        bottom.addView(tile(LobbyKit.UPDATE, new LobbyKit.Icon(LobbyKit.ICON_DOWNLOAD, Color.WHITE), "UPDATE",
                update != null ? shortName(update) + " is out" : "THIS VERSION, CRASH NOTE",
                update != null ? "NEW" : null, false, v -> showUpdate()), tileLp(false));
        refreshNotice();
    }

    private LinearLayout tileRow(LinearLayout parent, boolean first) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
        if (!first) lp.topMargin = dp(12);
        parent.addView(r, lp);
        return r;
    }

    private LinearLayout.LayoutParams tileLp(boolean first) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f);
        if (!first) lp.leftMargin = dp(12);
        return lp;
    }

    /**
     * r157: one big tile: the section's colour fading into the navy, a band of it along the top, the icon, the name,
     * one line under it, and a red badge (NEW) in the corner. `big`: HOME's PLAY.
     */
    private View tile(int colour, android.graphics.drawable.Drawable art, String name, String sub, String badge,
                      boolean big, View.OnClickListener onClick) {
        return tile(colour, art, name, sub, badge, big, big ? 72 : 30, onClick);
    }

    /** tile() with the title's largest size (round 2: it shrinks to fit one line, never under MIN_SP). */
    private View tile(int colour, android.graphics.drawable.Drawable art, String name, String sub, String badge,
                      boolean big, int titleSp, View.OnClickListener onClick) {
        final float density = getResources().getDisplayMetrics().density;
        FrameLayout t = new FrameLayout(this);
        GradientDrawable bg = LobbyKit.fade(LobbyKit.mix(colour, LobbyKit.TILE, big ? 0.1f : 0.6f),
                LobbyKit.mix(colour, LobbyKit.NAVY_BOT, big ? 0.62f : 0.72f), dp(22));
        t.setBackground(LobbyKit.pressable(bg, LobbyKit.mix(colour, Color.WHITE, 0.15f), dp(22), density));
        t.setClickable(true);
        t.setFocusable(true);
        t.setOnClickListener(onClick);
        t.setContentDescription(name);
        LinearLayout in = new LinearLayout(this);
        in.setOrientation(LinearLayout.VERTICAL);
        in.setGravity(Gravity.CENTER);
        in.setPadding(dp(10), dp(14), dp(10), dp(10));
        ImageView icon = new ImageView(this);
        icon.setImageDrawable(art);
        icon.setScaleType(ImageView.ScaleType.FIT_CENTER);
        in.addView(icon, new LinearLayout.LayoutParams(dp(big ? 200 : 66), dp(big ? 200 : 66)));
        TextView title = fitText(name, titleSp, big ? LobbyKit.CREAM : Color.WHITE);  // round 2: one line, always
        title.setShadowLayer(dp(4), 0, dp(3), 0xD0000000);
        title.setPadding(0, dp(big ? 6 : 8), 0, dp(4));
        in.addView(title, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(titleSp + 14)));
        if (sub != null) {
            TextView line = luckyText(sub, MIN_SP, big ? Color.WHITE : LobbyKit.INK_SOFT);
            line.setGravity(Gravity.CENTER);
            line.setMaxLines(2);
            if (big) {
                line.setBackground(LobbyKit.round(0x90000000, dp(20)));
                line.setPadding(dp(20), dp(8), dp(20), dp(5));
            }
            LinearLayout.LayoutParams llp = wrap();
            llp.topMargin = dp(big ? 12 : 2);
            in.addView(line, llp);
        }
        t.addView(in, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        if (badge != null) {
            TextView b = luckyText(badge, MIN_SP, Color.WHITE);
            GradientDrawable bb = LobbyKit.round(0xFFE0503C, dp(14));
            bb.setStroke(dp(2), Color.WHITE);
            b.setBackground(bb);
            b.setPadding(dp(10), dp(5), dp(10), dp(2));
            FrameLayout.LayoutParams blp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.END);
            blp.topMargin = dp(16);
            blp.rightMargin = dp(12);
            t.addView(b, blp);
        }
        return t;
    }

    /** r157: a row of tiles inside a page (ONLINE's four, GUESTS' two). */
    private LinearLayout pageTiles(int height) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(height));
        lp.topMargin = dp(4);
        lp.bottomMargin = dp(10);
        page.addView(r, lp);
        return r;
    }

    /** r157: what PLAY starts, on its tile: "SUPER MARIO STRIKERS (USA) - PLUS 1.18", or what's in the way. */
    private String playLine() {
        final String edition = editionName(StrikersHome.edition(this));
        if (!StrikersHome.hasAllFilesAccess() && !StrikersHome.hasGameDataInHome(StrikersHome.homeDir(this))) {
            return "PULL: THE APP ASKS FOR FILE ACCESS FIRST";
        }
        String path = StrikersHome.describeGameData(this);
        if (path == null) return "NO GAME FILE YET: PULL TO PICK IT";
        File f = new File(path);
        if (f.isDirectory()) return "THE EXTRACTED DISC\n" + edition;
        GameFile.Found found = GameFile.check(f);
        // round 2: the version on its own line (at MIN_SP the two don't fit one line on the tile)
        return found.usable ? found.label + "\n" + edition : "GAME FILE PROBLEM: PULL TO PICK ANOTHER";
    }

    /**
     * r157 (menu overhaul): HOME > ONLINE: QUICK MATCH, HOST A ROOM, JOIN WITH A CODE, TOURNAMENTS, the voice chat
     * row, and the online / co-op how-to in one line (the full text stays in the README).
     */
    private void showOnline() {
        openPage(LobbyKit.ONLINE, this::showHome, -1, "ONLINE");
        LinearLayout four = pageTiles(212);  // round 2: the row, the how-to and the notice strip fit without scrolling
        four.addView(tile(LobbyKit.ONLINE, new LobbyKit.Icon(LobbyKit.ICON_GLOBE, Color.WHITE), "QUICK MATCH",
                "ANYONE, RIGHT NOW", null, false, 28, v -> showQuick()), tileLp(true));  // r44
        // r44d (Lance: "make sure everyones codes randomize"): a fresh code every time, not the last one.
        four.addView(tile(LobbyKit.ONLINE, new LobbyKit.Icon(LobbyKit.ICON_HOUSE, Color.WHITE), "HOST A ROOM",
                "GET A CODE", null, false, 28, v -> showHost(newCode())), tileLp(false));
        four.addView(tile(LobbyKit.ONLINE, new LobbyKit.Icon(LobbyKit.ICON_CHAT, Color.WHITE), "JOIN WITH A CODE",
                "TYPE THEIR CODE", null, false, 28, v -> showJoin()), tileLp(false));
        four.addView(tile(LobbyKit.UPDATE, new LobbyKit.Icon(LobbyKit.ICON_TROPHY, Color.WHITE), "TOURNAMENTS",
                "4 OR 8, KNOCKOUT", null, false, 28, v -> showTourneys()), tileLp(false));  // r45
        valueRow(new LobbyKit.Icon(LobbyKit.ICON_CHAT, LobbyKit.INK_SOFT), "VOICE CHAT IN ONLINE MATCHES",
                voiceOn() ? "ON" : "OFF", voiceOn(), v -> toggleVoice());  // r44
        line("Grudge Match: same room. Co-op Cup: the host picks a cup, the friend any cup, both start its next"
                + " match.");
        line("Same release + version for both  (yours: " + editionName(StrikersHome.edition(this))
                + ")").setTextColor(LobbyKit.INK_DIM);
    }

    /** r157 (menu overhaul): HOME > GUESTS: MAKE GUESTS (the converter) and WHO PLAYS WHO (the swaps). */
    private void showGuestsHub() {
        openPage(LobbyKit.GUESTS, this::showHome, -1, "GUESTS");
        final boolean made = io.github.newcoke.strikers.guests.GuestSetup.made(StrikersHome.homeDir(this));
        final int swapped = readGuests().size();
        LinearLayout two = pageTiles(270);
        two.addView(tile(LobbyKit.GUESTS, new LobbyKit.Icon(LobbyKit.ICON_PEOPLE, Color.WHITE), "MAKE GUESTS",
                made ? "MADE ON THIS HEADSET" : "FROM YOUR OWN GAME FILES", made ? null : "NEW", false,
                v -> showGuests()), tileLp(true));
        two.addView(tile(LobbyKit.GUESTS, new LobbyKit.Icon(LobbyKit.ICON_SWAP, Color.WHITE), "WHO PLAYS WHO",
                swapped == 0 ? "EVERYONE AS THEMSELVES" : swapped + " SWAPPED IN", null, false, v -> showCharacters()),
                tileLp(false));
        line("Who plays who closes a game left running: the new line-up starts with the next game.")
                .setTextColor(LobbyKit.INK_DIM);
    }

    /**
     * r157 (menu overhaul): HOME > UPDATE: update now (when a release is out), this version, and the last crash note
     * in full (the strip shows its first line).
     */
    private void showUpdate() {
        openPage(LobbyKit.UPDATE, this::showHome, -1, "UPDATE");
        onUpdate = true;
        title("Update now", 18);
        if (update != null) {
            line(shortName(update) + " is out.").setTextColor(ACCENT);
            page.addView(button("Update now", true, v -> startUpdate()));
        } else {
            line("No newer release. The panel looks on GitHub each time it starts.");
        }
        title("This version", 18);
        line(releaseName() + "  -  " + editionName(StrikersHome.edition(this)));
        TextView version = note(versionText());
        version.setTextSize(MIN_SP);
        version.setTextColor(MUTED);
        title("Last crash note", 18);
        final File f = crashFile();
        if (f.isFile()) {
            TextView t = note("The game closed last time with:\n" + crashText());
            t.setTextColor(WARN);
            t.setTextSize(MIN_SP);
            page.addView(button("Clear it", false, v -> {
                //noinspection ResultOfMethodCallIgnored
                f.delete();
                showUpdate();
            }));
        } else {
            line("None: no message from the game's last run.");
        }
    }

    /** strikers-quest r41: GitHub's latest release, once per panel start, off the UI thread. */
    private void checkForUpdate() {
        final String own = versionName();
        new Thread(() -> {
            final Updater.Release r = Updater.newer(own);
            if (r == null) return;
            runOnUiThread(() -> {
                update = r;
                // r157: HOME's UPDATE tile gets its NEW badge (and the UPDATE page its button), keeping a note up
                final String keep = flashNote;
                final int keepColour = flashColour;
                if (onHome) showHome();
                else if (onUpdate) showUpdate();
                else return;
                if (keep != null) notice(keep, keepColour);
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
                showUpdate();  // r157: the note in the strip
                notice("Allow this app to install its update, then come back and press Update now again.", WARN);
                return;
            } catch (Exception e) {
                // fall through to the installer
            }
        }
        openPage(LobbyKit.UPDATE, this::showUpdate, -1, "UPDATE", "UPDATING");  // r157: BACK pill for Back
        title("Updating to " + shortName(r), 24);
        final TextView status = note("Downloading... 0%");
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
            showUpdate();  // r157: the note in the strip
            notice("The update wasn't installed" + (msg != null ? ": " + msg : "."), WARN);
        }
    }

    /** "alpha 1.2" out of "Quest 3 Strikers - alpha 1.2". */
    private static String shortName(Updater.Release r) {
        String name = r.name.replaceFirst("(?i)^quest 3 strikers\\s*[-–—:]\\s*", "");
        return name.isEmpty() ? r.tag : name.substring(0, 1).toUpperCase(java.util.Locale.ROOT) + name.substring(1);
    }

    /**
     * strikers-quest r41: the message the game closed with last time (fatal.c writes it; under the
     * immersive view its own message box is never seen). r157: its first line is in the notice strip (GOT IT
     * clears it, as OK did), the first 12 lines on UPDATE > LAST CRASH NOTE.
     */
    private File crashFile() {
        return new File(StrikersHome.homeDir(this), "last-error.txt");
    }

    private String crashText() {
        String text = readText(crashFile(), 1500);
        String[] lines = text.split("\n");
        StringBuilder shown = new StringBuilder();
        for (int i = 0; i < lines.length && i < 12; i++) shown.append(i > 0 ? "\n" : "").append(lines[i]);
        return shown.toString();
    }

    /** r157: the crash note's line for the strip, null when there is none. */
    private String crashLine() {
        if (!crashFile().isFile()) return null;
        String first = "";
        for (String l : crashText().split("\n")) {
            if (!l.trim().isEmpty()) {
                first = l.trim();
                break;
            }
        }
        if (first.length() > 70) first = first.substring(0, 70) + "...";
        return "The game closed last time: " + first + "  (UPDATE > LAST CRASH NOTE)";
    }

    /**
     * strikers-quest r43: why the last online match didn't start, in words. The game comes back here when it
     * refuses one (StrikersActivity), so this is what the player sees first. Before r43 the reason was only in the
     * log. r157: in the notice strip (GOT IT clears it, as OK did), null when there is none.
     */
    private String onlineNoteText() {
        final File f = new File(StrikersHome.homeDir(this), ONLINE_NOTE);
        if (!f.isFile()) return null;
        String why = readText(f, 200);
        String mine = StrikersHome.edition(this);
        String text;
        if (why.startsWith("edition=")) {
            String theirs = StrikersHome.CLASSIC.equals(why.substring(8).trim()) ? StrikersHome.CLASSIC
                    : StrikersHome.PLUS;
            if (theirs.equals(mine)) {  // already switched to theirs
                //noinspection ResultOfMethodCallIgnored
                f.delete();
                return null;
            }
            // r157: where the Version is now, in the words of the page
            text = "Online didn't start: your friend is on " + editionName(theirs) + ", you're on "
                    + editionName(mine) + ". One of you: SETTINGS > GAME > VERSION, then start again.";
        } else if (why.startsWith(NOTE_VERSION_CHANGED)) {
            text = "The game was still open on the other Version, so it closed. Press the same button"
                    + " again to play " + editionName(mine) + ".";
        } else {
            text = "Online didn't start: your friend has a different release. Both of you need the same one"
                    + " (UPDATE, or the GitHub page).";
        }
        return text;
    }

    /** strikers-quest r43: one line for {@link #onlineNoteText} (StrikersActivity, in the game's process). */
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
    // r157 (menu overhaul): the Version picker and "Choose game file" are one page now, SETTINGS > GAME (the
    // mockup's 3_launcher_settings.png): the game file card with CHANGE, then the two Versions side by side.
    private void showGameSettings() {
        openPage(LobbyKit.SETTINGS, this::showHome, RAIL_GAME, "SETTINGS", "GAME");
        final float density = getResources().getDisplayMetrics().density;
        // the game file (r41's gameStatus, as a card)
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.HORIZONTAL);
        card.setGravity(Gravity.CENTER_VERTICAL);
        card.setBackground(LobbyKit.round(LobbyKit.TILE, dp(20)));
        card.setPadding(dp(18), dp(14), dp(16), dp(14));
        ImageView disk = new ImageView(this);
        disk.setImageDrawable(new LobbyKit.Icon(LobbyKit.ICON_FLOPPY, Color.WHITE));
        card.addView(disk, new LinearLayout.LayoutParams(dp(56), dp(56)));
        LinearLayout words = new LinearLayout(this);
        words.setOrientation(LinearLayout.VERTICAL);
        words.setPadding(dp(16), 0, dp(10), 0);
        words.addView(luckyText("GAME FILE", 15, LobbyKit.INK_DIM), wrap());
        final String[] file = gameFileLines();
        TextView name = luckyText(file[0], 22, Color.WHITE);
        name.setPadding(0, dp(4), 0, dp(2));
        words.addView(name, wrap());
        TextView where = new TextView(this);
        where.setText(file[1]);
        where.setTextSize(MIN_SP);
        where.setTypeface(lucky);  // round 2
        where.setAllCaps(true);
        where.setTextColor(file[2] != null ? OK_GREEN : WARN);
        words.addView(where, wrap());
        card.addView(words, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Button change = pill("CHANGE", ACCENT, LobbyKit.PILL, v -> showChooser(null, null));
        change.setTextSize(24);
        change.setPadding(dp(24), dp(10), dp(24), dp(6));
        card.addView(change, wrap());
        page.addView(card, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // strikers-quest r43: the Version picker (Lance: "we draw a line after 1.2 to notify versions after
        // that are no longer strikers as they know it"). A tap saves it at once; the game takes it when it
        // starts (StrikersHome.prepare -> STRIKERS_EDITION, src/platform/edition.cpp). r157: side by side, the
        // gold line between them, a green check on the one chosen.
        title("Version", 18);
        final String current = StrikersHome.edition(this);
        LinearLayout two = new LinearLayout(this);
        two.setOrientation(LinearLayout.HORIZONTAL);
        two.addView(versionCard(StrikersHome.CLASSIC, "Strikers as you know it", current, density),
                new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        // The line: everything past it adds to the game.
        View gold = new View(this);
        gold.setBackgroundColor(ACCENT);
        LinearLayout.LayoutParams glp = new LinearLayout.LayoutParams(dp(4), ViewGroup.LayoutParams.MATCH_PARENT);
        glp.leftMargin = dp(10);
        glp.rightMargin = dp(10);
        two.addView(gold, glp);
        two.addView(versionCard(StrikersHome.PLUS, "Life-size scorer, scorer banner, longer replay, living crowd", current, density), new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.MATCH_PARENT, 1f));
        page.addView(two, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        line("Right of the line: not Strikers as you know it").setTextColor(ACCENT);
        line("Online: you both need the same one. Changing it closes a running game.");
    }

    /** r157: the game file card's lines: the game's name, where it is (or what's wrong), non-null [2] = ready. */
    private String[] gameFileLines() {
        if (!StrikersHome.hasAllFilesAccess()) {
            return new String[] {"NOT FOUND YET", "The app needs file access to look for it (CHANGE asks).", null};
        }
        String path = StrikersHome.describeGameData(this);
        if (path == null) return new String[] {"NO GAME FILE YET", "Press CHANGE to pick yours.", null};
        File f = new File(path);
        if (f.isDirectory()) return new String[] {"THE EXTRACTED DISC", "Ready - " + GameFile.shortPath(f), ""};
        GameFile.Found found = GameFile.check(f);
        return found.usable
                ? new String[] {found.label, "Ready - " + GameFile.shortPath(f), ""}
                : new String[] {f.getName(), "Problem: " + found.problem, null};
    }

    /** strikers-quest r43: one Version in the picker, its name large over what it is. r157: a card, lit + a green
     *  check when chosen. */
    private View versionCard(String edition, String about, String current, float density) {
        final boolean chosen = edition.equals(current);
        FrameLayout c = new FrameLayout(this);
        c.setBackground(LobbyKit.pressable(LobbyKit.round(chosen ? LobbyKit.TILE_ON : LobbyKit.TILE, dp(20)), 0,
                dp(20), density));
        c.setClickable(true);
        c.setFocusable(true);
        c.setOnClickListener(v -> {
            if (!edition.equals(StrikersHome.edition(this))) {
                StrikersHome.chooseEdition(this, edition);
                endRunningGame();
                // A note that said to switch has been acted on.
                //noinspection ResultOfMethodCallIgnored
                new File(StrikersHome.homeDir(this), ONLINE_NOTE).delete();
            }
            showGameSettings();
        });
        LinearLayout in = new LinearLayout(this);
        in.setOrientation(LinearLayout.VERTICAL);
        in.setPadding(dp(20), dp(18), dp(56), dp(18));
        TextView n = luckyText(editionName(edition), 30, chosen ? ACCENT : Color.WHITE);
        n.setShadowLayer(dp(3), 0, dp(2), 0xA0000000);
        in.addView(n, wrap());
        TextView a = luckyText(about, 15, LobbyKit.INK_SOFT);
        a.setPadding(0, dp(10), 0, 0);
        in.addView(a, wrap());
        c.addView(in);
        if (chosen) c.addView(checkMark(), checkLp());
        return c;
    }

    /** r157 (MENUS-PLAN.md rule 8): "chosen" is a green check, nothing else. */
    private ImageView checkMark() {
        ImageView v = new ImageView(this);
        v.setImageDrawable(new LobbyKit.Icon(LobbyKit.ICON_CHECK, Color.WHITE));
        return v;
    }

    private FrameLayout.LayoutParams checkLp() {
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(dp(38), dp(38), Gravity.TOP | Gravity.END);
        lp.topMargin = dp(12);
        lp.rightMargin = dp(12);
        return lp;
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
                android.util.Log.i("strikers", "Version or characters changed: ending the game left running (pid " + p.pid + ")");
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
            // r157: the page stays; the note goes in the strip
            notice("Turn on \"Allow access to manage all files\" so the game can find your game file, then press"
                    + " the same button again.", WARN);
            return;
        }
        // r157: SETTINGS > GAME > GAME FILE from CHANGE; from PLAY / a match (code != null) BACK goes HOME
        if (code == null) {
            openPage(LobbyKit.SETTINGS, this::showGameSettings, RAIL_GAME, "SETTINGS", "GAME", "GAME FILE");
        } else {
            openPage(LobbyKit.PLAY, this::showHome, -1, "GAME FILE");
        }
        if (reason != null) note(reason).setTextColor(WARN);
        final TextView status = note("Looking for game files on this Quest...");
        final LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setGravity(Gravity.CENTER_HORIZONTAL);
        page.addView(list, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        page.addView(button("Look again", false, v -> showChooser(code, null)));
        final int token = ++chooserToken;
        new Thread(() -> {
            final List<GameFile.Found> found = GameFile.scan();
            runOnUiThread(() -> {
                if (token != chooserToken || list.getParent() != page) return;  // left this page
                if (found.isEmpty()) {
                    status.setText("No .iso, .rvz or .wia file on this Quest.\nOn your PC: SideQuest, the folder"
                            + " icon, Download, drag your game file in. Then LOOK AGAIN.");
                    return;
                }
                status.setText("Tap your game file:");
                String chosen = GameFile.chosen(this);
                for (GameFile.Found f : found) {
                    String where = GameFile.shortPath(f.file) + " · " + GameFile.size(f.file.length());
                    boolean isChosen = f.file.getAbsolutePath().equals(chosen);
                    String text = f.file.getName() + "\n"
                            + (f.usable ? f.label : f.label + ": " + f.problem) + "\n" + where;
                    list.addView(checkRow(text, isChosen, f.usable, f.usable ? v -> {
                        GameFile.choose(this, f.file.getAbsolutePath());
                        if (code != null) start(code); else showGameSettings();  // r157: back to GAME
                    } : null));
                }
            });
        }, "strikers-scan").start();
    }

    private void showHost(String code) {
        openPage(LobbyKit.ONLINE, this::showOnline, -1, "ONLINE", "HOST A ROOM");  // r157
        title("Your room code", 24);
        TextView big = new TextView(this);
        big.setText(spaced(code));
        big.setTextColor(ACCENT);
        big.setTypeface(lucky);  // r157: the game's font (no I, O, 0 or 1 to mix up in it either)
        big.setShadowLayer(dp(4), 0, dp(3), 0xC0000000);
        big.setTextSize(72);
        big.setGravity(Gravity.CENTER);
        big.setPadding(0, dp(8), 0, dp(8));
        page.addView(big, wrap());
        hostedCode = code;  // r64
        note("Tell your friend this code. Both: Grudge Match, pick your team, start.\n"
                + "Co-op Cup: you start your cup's next match, your friend any cup's. Your cup is played: you're the"
                + " captain + 1 sidekick, your friend the other 2.");
        page.addView(button("Start", true, v -> start(code)));
        page.addView(button("New code", false, v -> showHost(newCode())));
    }

    private void showJoin() {
        openPage(LobbyKit.ONLINE, this::showOnline, -1, "ONLINE", "JOIN WITH A CODE");  // r157
        note("Type the code your friend sees.");
        EditText field = new EditText(this);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        field.setFilters(new InputFilter[] {new InputFilter.AllCaps(), new InputFilter.LengthFilter(8)});
        field.setTextSize(44);
        field.setTypeface(lucky);  // r157
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
        // r157: BACK (the pill, or B) is Cancel: openPage on any other page ends the search (++quickToken)
        openPage(LobbyKit.ONLINE, this::showOnline, -1, "ONLINE", "QUICK MATCH");
        searching = true;
        final TextView status = luckyText("Looking for another player...", 26, ACCENT);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(20), 0, dp(14));
        page.addView(status, wrap());
        String edition = StrikersHome.edition(this);
        note("Matching you with someone on " + editionName(edition) + ". Then both: Grudge Match, pick your team,"
                + " start." + (voiceOn() ? "" : "\nVoice chat is off (ONLINE > VOICE CHAT).") + "\nBACK stops looking.");
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
        // r157: BACK on this page stops the start (openPage's ++quickToken), as leaving it never could before
        openPage(LobbyKit.ONLINE, this::showOnline, -1, "ONLINE", "QUICK MATCH", "PLAYER FOUND");
        searching = false;
        title("Player found!", 28);
        TextView t = note("In the game: Grudge Match, pick your team, start. It begins when you both have.\n(Your"
                + " room: " + spaced(code) + ")");
        t.setTextSize(MIN_SP);
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

    // r47: LONG, the long pass (the game's L + A from one button; players: "L2 + A ... is really
    // awkward"), on nothing by default. A Bluetooth controller follows the same map: its A/B/X/Y,
    // LT/RT, RB for the right grip and Start for the menu button.
    private static final String[] ACTIONS = {"A", "B", "X", "Y", "Z", "L", "R", "START", "LONG"};
    private static final String[] ACTION_LABELS = {"Pass (GameCube A)", "Shoot (GameCube B)",
            "Use your item (GameCube X)", "Hit (GameCube Y)", "Swap items (GameCube Z)", "GameCube L",
            "GameCube R", "Pause (Start)", "Long pass (GameCube L + A)"};
    private static final String[] SOURCES = {"a", "b", "x", "y", "lt", "rt", "rg", "menu", "none"};
    private static final String[] SOURCE_LABELS = {"A button", "B button", "X button", "Y button",
            "Left trigger (pad: LT)", "Right trigger (pad: RT)", "Right grip (pad: RB)", "Menu button (pad: Start)",
            "Nothing"};
    private static final String[] DEFAULT_CONTROLS = {"a", "b", "x", "y", "rg", "lt", "rt", "menu", "none"};

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
        openPage(LobbyKit.SETTINGS, this::showHome, RAIL_CONTROLS, "SETTINGS", "CONTROLS");  // r157
        note("Pick a game button, then its Quest button. It changes at once, even in a match. A Bluetooth pad"
                + " follows the same map (A/B/X/Y, LT/RT, RB, Start).");
        final String[] map = readControls();
        for (int i = 0; i < ACTIONS.length; i++) {
            final int action = i;
            final String head = ACTION_LABELS[i];
            final boolean changed = !map[i].equals(DEFAULT_CONTROLS[i]);
            // r157: a changed button's Quest button in a gold pill (no "(changed)"); round 2: one row each, the game
            // button left, its Quest button in the pill (half the height of two-line rows, less to scroll)
            valueRow(null, head, sourceLabel(map[i]), changed ? ACCENT : 0xFF0E1428,
                    changed ? LobbyKit.NAVY_BOT : Color.WHITE, v -> showControlPick(action));
        }
        TextView fixed = note("Always: left stick moves, right stick = C-stick, a stick clicked twice switches the"
                + " view, left grip moves the table (or the big screen at its lit edge).");
        fixed.setTextSize(MIN_SP);
        fixed.setTextColor(MUTED);
        // r157: "Back to the default buttons" read like BACK; the same button, its own words
        page.addView(button("Default buttons", false, v -> {
            writeControls(DEFAULT_CONTROLS.clone());
            showControls();
        }));
    }

    // --- strikers-quest r61: the home ground (Lance + GPT: "give people their own Strikers home ground
    // that grows with them"; src/platform/xr_home.cpp). The home captain is picked here (or follows the
    // captain played most); the game reads home-captain.txt, even while it is open, and keeps the
    // records in xr_home.txt, summed up here.

    private static final int[] HOME_CAPTAINS = {6, 5, 7, 1, 11, 10, 9, 2};  // eCharacterClass
    private static final String[] CLASS_NAMES = {"Birdo", "Daisy", "Donkey Kong", "Hammer Bro", "Koopa", "Luigi",
            "Mario", "Peach", "Toad", "Waluigi", "Wario", "Yoshi"};

    private static String className(int c) {
        return c >= 0 && c < CLASS_NAMES.length ? CLASS_NAMES[c] : "?";
    }

    /** home-captain.txt: a Strikers captain's class number, r90 a guest's pack ("pk_geodude"), "-1" automatic. */
    private String readHomePick() {
        String t = readText(new File(StrikersHome.homeDir(this), "home-captain.txt"), 64).trim();
        return t.isEmpty() ? "-1" : t;
    }

    private void writeHomePick(String pick) {
        try (java.io.OutputStream out = new java.io.FileOutputStream(
                new File(StrikersHome.homeDir(this), "home-captain.txt"))) {
            out.write((pick + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            // the pick is lost; the page shows the old one
        }
    }

    /** r90: a guest captain on this headset (guests/roster.txt). */
    private static final class HomeGuest {
        final String pack, name, game;

        HomeGuest(String pack, String name, String game) {
            this.pack = pack;
            this.name = name;
            this.game = game;
        }
    }

    /** r90: every guest captain this headset has, in roster order; an evolved Pokemon only once it's unlocked
     *  (franchise/unlocked.txt), as on the character select menu. */
    private List<HomeGuest> homeGuests() {
        File home = StrikersHome.homeDir(this);
        java.util.Set<String> unlocked = new java.util.HashSet<>();
        for (String l : readText(new File(home, "franchise/unlocked.txt"), 64000).split("\n")) {
            if (!l.trim().isEmpty()) unlocked.add(l.trim());
        }
        List<HomeGuest> out = new ArrayList<>();
        // r183: and the ORIGINALS shipped in the APK (guests/originals.txt)
        String rosters = readText(new File(home, "guests/roster.txt"), 200000) + "\n"
                + readText(new File(home, "guests/originals.txt"), 64000);
        for (String line : rosters.split("\n")) {
            if (line.startsWith("#")) continue;
            String[] f = line.split("\t");
            if (f.length < 5 || !f[1].trim().equals("captain")) continue;
            String pack = f[0].trim();
            if (f.length >= 6 && f[5].trim().equals("franchise") && !unlocked.contains(pack)) continue;
            out.add(new HomeGuest(pack, f[3].trim(), f[4].trim()));
        }
        return out;
    }

    /** r90: a home-ground name: a class number's captain, or a guest pack's own name. */
    private static String homeName(String classOrPack, java.util.Map<String, String> guestNames) {
        try {
            return className(Integer.parseInt(classOrPack));
        } catch (NumberFormatException e) {
            String n = guestNames.get(classOrPack);
            return n != null ? n : classOrPack;
        }
    }

    /** The words from `from` on, as one string (a name saved last on a line). */
    private static String joinFrom(String[] f, int from) {
        StringBuilder sb = new StringBuilder();
        for (int i = from; i < f.length; i++) {
            if (f[i].isEmpty()) continue;
            if (sb.length() > 0) sb.append(' ');
            sb.append(f[i]);
        }
        return sb.toString();
    }

    /** What xr_home.txt says, in a few lines (r90: imported captains by their own names). */
    private String homeSummary(java.util.Map<String, String> guestNames) {
        String text = readText(new File(StrikersHome.homeDir(this), "xr_home.txt"), 8000);
        StringBuilder out = new StringBuilder();
        String cpuRival = null;
        int cpuGames = 1;  // two at least
        String onlineRival = null;
        int onlineGames = 0;
        // r90: the imported captains of the last match / the first win ("lastguests <mine|-> <theirs|->")
        String[] lastGuests = null, ticketGuests = null;
        for (String line : text.split("\n")) {
            String[] f = line.trim().split(" ");
            if (f.length == 3 && f[0].equals("lastguests")) lastGuests = f;
            if (f.length == 3 && f[0].equals("ticketguests")) ticketGuests = f;
        }
        for (String line : text.split("\n")) {
            String[] f = line.trim().split(" ");
            if (f.length < 2) continue;
            try {
                if (f[0].equals("last") && f.length >= 10) {
                    int mine = Integer.parseInt(f[3]), theirs = Integer.parseInt(f[4]);
                    boolean online = f[6].equals("1");
                    String me = lastGuests != null && !lastGuests[1].equals("-") ? homeName(lastGuests[1], guestNames)
                                                                                 : className(Integer.parseInt(f[1]));
                    String them = online ? joinFrom(f, 10)
                            : (lastGuests != null && !lastGuests[2].equals("-") ? homeName(lastGuests[2], guestNames)
                                                                                  : className(Integer.parseInt(f[2])));
                    out.append("Last match: ").append(me).append(" ").append(mine)
                            .append(" - ").append(theirs).append(" ").append(them.isEmpty() ? "online friend" : them)
                            .append(f[5].equals("1") ? " (overtime)" : "").append("\n");
                } else if (f[0].equals("cpu") && f.length >= 5) {
                    int w = Integer.parseInt(f[2]), l = Integer.parseInt(f[3]), d = Integer.parseInt(f[4]);
                    if (w + l + d > cpuGames) {
                        cpuGames = w + l + d;
                        cpuRival = className(Integer.parseInt(f[1])) + " (you " + w + " - " + l + " them"
                                + (d > 0 ? ", " + d + " draws" : "") + ")";
                    }
                } else if (f[0].equals("gcpu") && f.length >= 6) {  // r90: an imported CPU captain's record
                    int w = Integer.parseInt(f[3]), l = Integer.parseInt(f[4]), d = Integer.parseInt(f[5]);
                    if (w + l + d > cpuGames) {
                        cpuGames = w + l + d;
                        cpuRival = homeName(f[1], guestNames) + " (you " + w + " - " + l + " them"
                                + (d > 0 ? ", " + d + " draws" : "") + ")";
                    }
                } else if (f[0].equals("online") && f.length >= 5) {
                    int w = Integer.parseInt(f[2]), l = Integer.parseInt(f[3]), d = Integer.parseInt(f[4]);
                    if (w + l + d > onlineGames) {
                        onlineGames = w + l + d;
                        String name = joinFrom(f, 6);
                        onlineRival = (name.isEmpty() ? "a friend" : name) + " (you " + w + " - " + l + " them)";
                    }
                } else if (f[0].equals("honors") && f.length >= 5) {
                    out.append("Comebacks: ").append(f[1]).append("   Super Strike goals: ").append(f[3])
                            .append("   Matches: ").append(f[4]).append("\n");
                } else if (f[0].equals("ticket") && f.length >= 5) {
                    String me = ticketGuests != null && !ticketGuests[1].equals("-") ? homeName(ticketGuests[1], guestNames)
                                                                                     : className(Integer.parseInt(f[1]));
                    String them = ticketGuests != null && !ticketGuests[2].equals("-") ? homeName(ticketGuests[2], guestNames)
                                                                                       : className(Integer.parseInt(f[2]));
                    out.append("First win: ").append(me).append(" ").append(f[3])
                            .append(" - ").append(f[4]).append(" ").append(them).append("\n");
                }
            } catch (RuntimeException e) {
                // a line from another version: skipped
            }
        }
        if (cpuRival != null) out.append("CPU rival: ").append(cpuRival).append("\n");
        if (onlineRival != null) out.append("Online rival: ").append(onlineRival).append("\n");
        return out.length() == 0 ? "Play a match and your home ground starts filling up." : out.toString().trim();
    }

    private void showHomeGround() {
        openPage(LobbyKit.SETTINGS, this::showHome, RAIL_HOME_GROUND, "SETTINGS", "HOME GROUND");  // r157
        note("Your home team's colours, trophies and rivals show up around you: the shelf, by the big screen,"
                + " on the Tabletop, in your cup suite.");
        // r194: WEATHER IN MY ROOM (also PAUSED > SETTINGS > ROOM, the same line in xr_room_options.txt)
        final boolean roomWx = readRoomWeather();
        valueRow(new LobbyKit.Icon(LobbyKit.ICON_HOUSE, LobbyKit.INK_SOFT), "WEATHER IN MY ROOM", roomWx ? "ON" : "OFF",
                roomWx, v -> {
                    writeRoomWeather(!roomWx);
                    showHomeGround();
                });
        line("A rain or snow match rains / snows in your own room too (MR). This headset only.").setTextColor(MUTED);
        // r197: USE MY ROOM SCAN (also PAUSED > SETTINGS > ROOM, the same "roomscan" line in xr_room_options.txt)
        final boolean scanOn = readRoomScan();
        valueRow(new LobbyKit.Icon(LobbyKit.ICON_HOUSE, LobbyKit.INK_SOFT), "USE MY ROOM SCAN", scanOn ? "ON" : "OFF",
                scanOn, v -> {
                    writeRoomScan(!scanOn);
                    if (!scanOn) askScenePermission();
                    showHomeGround();
                });
        line(!scanOn ? "Off: weather and effects use your floor, as before."
                : sceneGranted() ? "Rain, snow and goal effects land on your real furniture and pile against your walls."
                        + " Your scan: Quest Settings > Physical space > Space setup."
                : "Needs spatial data: press it, choose Allow (so effects land on your real furniture).")
                .setTextColor(MUTED);
        final String pick = readHomePick();
        // r90 (Lance: "its still only available for the base players"): every character on this headset's
        // roster too, by game; the game takes a guest's pack as well as a captain's number.
        final List<HomeGuest> guests = homeGuests();
        final java.util.Map<String, String> guestNames = new java.util.HashMap<>();
        for (HomeGuest g : guests) guestNames.put(g.pack, g.name);
        String current = pick.equals("-1") ? "Automatic" : (pick.equals("none") ? "None" : homeName(pick, guestNames));
        note("Your home captain now: " + current);
        // r134 (Lance: "when they select home ground at the beginning they can choose (NONE) to have none"): no home
        // ground at all - no board, no banners by the screen, no corner, no trophy shelf (the game reads "none")
        // r157: the one chosen lit with a green check (no "(current)")
        page.addView(checkRow("None: no home ground (no board, banners, corner or trophy shelf)",
                pick.equals("none"), true, v -> {
            writeHomePick("none");
            showHomeGround();
        }));
        page.addView(checkRow("Automatic: the captain you play most", pick.equals("-1"), true, v -> {
            writeHomePick("-1");
            showHomeGround();
        }));
        title("Super Mario Strikers", 18);
        for (final int c : HOME_CAPTAINS) {
            final String key = Integer.toString(c);
            page.addView(checkRow(className(c), pick.equals(key), true, v -> {
                writeHomePick(key);
                showHomeGround();
            }));
        }
        String lastGame = null;
        for (final HomeGuest g : guests) {
            if (!g.game.equals(lastGame)) {
                lastGame = g.game;
                title(g.game.equals("Colosseum") ? "Pokemon Colosseum" : g.game, 18);
            }
            page.addView(checkRow(g.name, pick.equals(g.pack), true, v -> {
                writeHomePick(g.pack);
                showHomeGround();
            }));
        }
        TextView summary = note(homeSummary(guestNames));
        summary.setTextSize(MIN_SP);
    }

    // --- strikers-quest r194: WEATHER IN MY ROOM - xr_room_options.txt "roomweather N" (the game's pause row keeps it
    // there too: xr_quest.cpp load_room_options), else strikers.ini room_weather, else on. The file's other lines kept.

    private File roomOptionsFile() {
        return new File(StrikersHome.homeDir(this), "xr_room_options.txt");
    }

    private static List<String> readTextLines(File f) {
        final List<String> out = new ArrayList<>();
        if (!f.isFile()) return out;
        try (java.io.BufferedReader r = new java.io.BufferedReader(
                new java.io.InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
            String s;
            while ((s = r.readLine()) != null) out.add(s);
        } catch (IOException e) {
            android.util.Log.w("StrikersLobby", "could not read " + f, e);
        }
        return out;
    }

    private boolean readRoomWeather() {
        for (String line : readTextLines(roomOptionsFile())) {
            final String[] f = line.trim().split("\\s+");
            if (f.length == 2 && f[0].equals("roomweather")) return !f[1].equals("0");
        }
        for (String line : readTextLines(new File(StrikersHome.homeDir(this), "strikers.ini"))) {
            final String l = line.trim();
            if (l.startsWith("#") || l.startsWith(";")) continue;
            final int eq = l.indexOf('=');
            if (eq <= 0) continue;
            final String key = l.substring(0, eq).trim().toLowerCase(java.util.Locale.ROOT);
            if (!key.equals("room_weather") && !key.equals("strikers_room_weather")) continue;
            final String v = l.substring(eq + 1).trim().toLowerCase(java.util.Locale.ROOT);
            return !(v.startsWith("0") || v.startsWith("of") || v.startsWith("n") || v.startsWith("f"));
        }
        return true;
    }

    private void writeRoomWeather(boolean on) {
        final File f = roomOptionsFile();
        final StringBuilder sb = new StringBuilder();
        boolean done = false;
        for (String line : readTextLines(f)) {
            if (line.trim().isEmpty()) continue;
            if (line.trim().startsWith("roomweather ")) {
                if (!done) sb.append("roomweather ").append(on ? 1 : 0).append('\n');
                done = true;
            } else {
                sb.append(line.trim()).append('\n');
            }
        }
        if (!done) sb.append("roomweather ").append(on ? 1 : 0).append('\n');
        try (java.io.OutputStream o = new java.io.FileOutputStream(f)) {
            o.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            android.util.Log.w("StrikersLobby", "could not write " + f, e);
        }
    }

    // --- strikers-quest r197: USE MY ROOM SCAN - xr_room_options.txt "roomscan N" (the game's pause row writes it too:
    // xr_quest.cpp save_room_options), else on. Its permission (spatial data) asked once here, with the reason.

    private static final int REQUEST_SCENE = 45;
    private static final String SCENE_PERMISSION = "com.oculus.permission.USE_SCENE";
    private static final String SCENE_ASKED = "scene_asked_r197";

    private boolean readRoomScan() {
        for (String line : readTextLines(roomOptionsFile())) {
            final String[] f = line.trim().split("\\s+");
            if (f.length == 2 && f[0].equals("roomscan")) return !f[1].equals("0");
        }
        return true;
    }

    private void writeRoomScan(boolean on) {
        final File f = roomOptionsFile();
        final StringBuilder sb = new StringBuilder();
        boolean done = false;
        for (String line : readTextLines(f)) {
            if (line.trim().isEmpty()) continue;
            if (line.trim().startsWith("roomscan ")) {
                if (!done) sb.append("roomscan ").append(on ? 1 : 0).append('\n');
                done = true;
            } else {
                sb.append(line.trim()).append('\n');
            }
        }
        if (!done) sb.append("roomscan ").append(on ? 1 : 0).append('\n');
        try (java.io.OutputStream o = new java.io.FileOutputStream(f)) {
            o.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            android.util.Log.w("StrikersLobby", "could not write " + f, e);
        }
    }

    private boolean sceneGranted() {
        return checkSelfPermission(SCENE_PERMISSION) == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    /** Once (the first panel start with r197), with the reason, while USE MY ROOM SCAN is on. */
    private void maybeAskScene() {
        if (sceneGranted() || prefs.getBoolean(SCENE_ASKED, false) || !readRoomScan()) return;
        prefs.edit().putBoolean(SCENE_ASKED, true).apply();
        askScenePermission();
    }

    private void askScenePermission() {
        if (sceneGranted()) return;
        notice("Strikers can use your Quest room scan, so rain, snow and goal effects land on your real furniture."
                + " Choose Allow. (SETTINGS > HOME GROUND > USE MY ROOM SCAN changes it.)", ACCENT);
        try {
            requestPermissions(new String[] {SCENE_PERMISSION}, REQUEST_SCENE);
        } catch (RuntimeException e) {
            android.util.Log.w("StrikersLobby", "could not ask for the room scan permission", e);
        }
    }

    // --- strikers-quest r66: guest characters (src/platform/xr_guest.cpp) ---
    // Characters from the player's own other games (packs in <home>/guests/*.gpk), each playing in
    // place of one Strikers character by Strikers' rules. guests.txt in the game's home folder holds
    // the picks, "<class> <pack>" a line, read when the game starts.

    private static final String[][] GUEST_SLOTS = {
        {"mario", "Mario"}, {"luigi", "Luigi"}, {"peach", "Peach"}, {"daisy", "Daisy"},
        {"wario", "Wario"}, {"waluigi", "Waluigi"}, {"yoshi", "Yoshi"}, {"donkeykong", "Donkey Kong"},
        {"toad", "Toad"}, {"koopa", "Koopa"}, {"hammerbros", "Hammer Bro"}, {"birdo", "Birdo"},
    };

    private static final java.util.Set<String> POKEMON = new java.util.HashSet<>(java.util.Arrays.asList(
            "venusaur", "charizard", "blastoise", "clefairy", "chansey", "electrode", "weezing", "goldeen",
            "staryu", "snorlax", "articuno", "zapdos", "moltres", "mew", "chikorita", "cyndaquil", "togepi",
            "marill", "bellossom", "unown", "wobbuffet", "porygon2", "scizor", "entei", "raikou", "suicune",
            "lugia", "hooh", "celebi"));

    private static String guestLabel(String pack) {
        switch (pack) {
            case "bowserjr": return "Bowser Jr. (Charged)";
            case "bowser": return "Bowser (Charged)";
            case "diddykong": return "Diddy Kong (Charged)";
            case "petey": return "Petey Piranha (Charged)";
            case "boo": return "Boo (Charged)";
            case "drybones": return "Dry Bones (Charged)";
            case "montymole": return "Monty Mole (Charged)";
            case "shyguy": return "Shy Guy (Charged)";
            case "falcon": return "Captain Falcon (Melee)";
            case "younglink": return "Young Link (Melee)";
            case "drmario": return "Dr. Mario (Melee)";
            case "gamewatch": return "Mr. Game & Watch (Melee)";
            case "popo": return "Popo (Melee)";
            case "nana": return "Nana (Melee)";
            case "hooh": return "Ho-Oh (Melee Pokemon)";
            default: {
                String n = pack.isEmpty() ? pack : Character.toUpperCase(pack.charAt(0)) + pack.substring(1);
                return POKEMON.contains(pack) ? n + " (Melee Pokemon)" : n + " (Melee)";
            }
        }
    }

    private File guestsFile() {
        return new File(StrikersHome.homeDir(this), "guests.txt");
    }

    private java.util.Map<String, String> readGuests() {
        java.util.Map<String, String> picks = new java.util.LinkedHashMap<>();
        for (String line : readText(guestsFile(), 8000).split("\n")) {
            String[] f = line.trim().split("\\s+");
            if (f.length >= 2 && !f[0].startsWith("#")) picks.put(f[0], f[1]);
        }
        return picks;
    }

    private void writeGuests(java.util.Map<String, String> picks) {
        StringBuilder sb = new StringBuilder("# Quest 3 Strikers guest characters: <Strikers character> <guest>\n");
        for (java.util.Map.Entry<String, String> e : picks.entrySet()) {
            sb.append(e.getKey()).append(' ').append(e.getValue()).append('\n');
        }
        try (java.io.OutputStream out = new java.io.FileOutputStream(guestsFile())) {
            out.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            // the pick is lost; the page shows the old one
        }
        // A fresh pick gets a fresh start (xr_guest.cpp turns guests off after a run that died drawing one).
        //noinspection ResultOfMethodCallIgnored
        new File(StrikersHome.homeDir(this), "guests.lock").delete();
        // r66e: the game reads guests.txt once, at its first guest draw; one left running in the
        // background would keep the old line-up, so it ends, as for a Version change.
        endRunningGame();
    }

    private java.util.List<String> guestPacks() {
        java.util.List<String> packs = new java.util.ArrayList<>();
        File[] files = new File(StrikersHome.homeDir(this), "guests").listFiles();
        if (files != null) {
            for (File f : files) {
                String n = f.getName();
                if (n.endsWith(".gpk")) packs.add(n.substring(0, n.length() - 4));
            }
        }
        java.util.Collections.sort(packs, (a, b) -> guestLabel(a).compareToIgnoreCase(guestLabel(b)));
        return packs;
    }

    // ------------------------------------------------------------------ strikers-quest 1.14: guest characters
    // (Lance: "make the converter"): made on this headset from the player's own Melee / Pokemon Colosseum / Mario
    // Strikers Charged files (guests/GuestSetup, guests/Convert). Nothing of those games is in the app.
    private io.github.newcoke.strikers.guests.GuestSetup.Found guestFound;  // the last search, null before one
    private boolean guestScanning, onGuests;
    private TextView guestProgress;

    private void showGuests() {
        final io.github.newcoke.strikers.guests.GuestSetup.Found found = guestFound;
        openPage(LobbyKit.GUESTS, this::showGuestsHub, -1, "GUESTS", "MAKE GUESTS");  // r157 (R: MAKE GUESTS)
        onGuests = true;
        guestProgress = null;
        // round 2: cut to the 40 px rule; the full text stays in the README
        TextView rule = note("You need your own game files for the guests: .iso or .rvz, anywhere on the Quest (the Wii"
                + " games: .rvz only, Dolphin's Convert File Format). Nothing from them comes with this app: your"
                + " headset makes the guests.");
        rule.setTextColor(ACCENT);
        if (found == null && !guestScanning) scanGuests();
        if (found == null) {
            note("Looking for your game files...");
        } else {
            for (int i = 0; i < io.github.newcoke.strikers.guests.GuestSetup.IDS.length; i++) {
                File f = found.files.get(io.github.newcoke.strikers.guests.GuestSetup.IDS[i]);
                TextView t = note(io.github.newcoke.strikers.guests.GuestSetup.NAMES[i] + ": "
                        + (f != null ? "found  (" + GameFile.shortPath(f) + ")" : "not found") + "\n"
                        + io.github.newcoke.strikers.guests.GuestSetup.GIVES[i]);
                t.setTextSize(MIN_SP);
                t.setTextColor(f != null ? Color.WHITE : MUTED);
            }
            for (String p : found.problems) note(p).setTextColor(WARN);
            if (StrikersHome.strikersImage(this) == null) {
                note("Pick your Super Mario Strikers file first (SETTINGS > GAME > CHANGE): the guests' kits and moves"
                        + " come from it.").setTextColor(WARN);
            }
        }
        final File home = StrikersHome.homeDir(this);
        if (io.github.newcoke.strikers.guests.GuestSetup.running) {
            guestProgress = note(guestLine());
            guestProgress.setTextColor(ACCENT);
            note("Keep the app open (a few minutes). The game can't start until it's done.");
        } else if (found != null && StrikersHome.strikersImage(this) != null) {
            // (r178: with no guest game too - the Strikers sidekicks' 3D figures come from the Strikers disc alone)
            boolean needed = io.github.newcoke.strikers.guests.GuestSetup.needed(home, found.files);
            page.addView(button(needed ? "Make guest characters" : "Make them again", needed, v -> startGuests()));
        }
        String result = io.github.newcoke.strikers.guests.GuestSetup.result;
        if (!io.github.newcoke.strikers.guests.GuestSetup.running && result != null) {
            note(result).setTextColor(io.github.newcoke.strikers.guests.GuestSetup.warnings.isEmpty() ? ACCENT : WARN);
            java.util.List<String> w = new java.util.ArrayList<>(io.github.newcoke.strikers.guests.GuestSetup.warnings);
            for (int i = 0; i < w.size() && i < 12; i++) note(w.get(i)).setTextSize(MIN_SP);
        }
        if (!io.github.newcoke.strikers.guests.GuestSetup.running) {
            page.addView(button("Search again", false, v -> {
                guestFound = null;
                showGuests();
            }));
        }
    }

    private static String guestLine() {
        int d = io.github.newcoke.strikers.guests.GuestSetup.done, t = io.github.newcoke.strikers.guests.GuestSetup.total;
        return "Making guests: " + (t > 0 ? (100 * d / t) : 0) + "%\n" + io.github.newcoke.strikers.guests.GuestSetup.line;
    }

    private void scanGuests() {
        if (!StrikersHome.hasAllFilesAccess()) {
            guestFound = new io.github.newcoke.strikers.guests.GuestSetup.Found();
            guestFound.problems.add("Turn on file access for this app first (SETTINGS > GAME > CHANGE asks for it).");
            return;
        }
        guestScanning = true;
        new Thread(() -> {
            io.github.newcoke.strikers.guests.GuestSetup.Found f =
                    io.github.newcoke.strikers.guests.GuestSetup.scan(android.os.Environment.getExternalStorageDirectory());
            runOnUiThread(() -> {
                guestScanning = false;
                guestFound = f;
                if (onGuests) showGuests();
            });
        }, "strikers-guest-scan").start();
    }

    private void startGuests() {
        if (guestFound == null) return;
        final File home = StrikersHome.homeDir(this);
        io.github.newcoke.strikers.guests.GuestSetup.start(getAssets(), home, StrikersHome.strikersImage(this),
                guestFound.files, () -> runOnUiThread(() -> {
                    if (!onGuests) return;
                    if (!io.github.newcoke.strikers.guests.GuestSetup.running || guestProgress == null) showGuests();
                    else guestProgress.setText(guestLine());
                }));
        showGuests();
    }

    private void showCharacters() {
        openPage(LobbyKit.GUESTS, this::showGuestsHub, -1, "GUESTS", "WHO PLAYS WHO");  // r157 (R: WHO PLAYS WHO)
        final java.util.List<String> packs = guestPacks();
        note(packs.isEmpty()
                ? "No guest characters on this headset yet."
                : "Pick who plays instead of each Strikers character. Guests play by Strikers' rules; Charged's"
                        + " move their own way. It applies the next time the game starts.");
        // r157 (MENUS-PLAN.md launcher fix 7): the page says what a change does to a running game
        line("A change here closes a game left running in the background.").setTextColor(WARN);
        final java.util.Map<String, String> picks = readGuests();
        for (final String[] slot : GUEST_SLOTS) {
            String g = picks.get(slot[0]);
            // round 2: one row each, the Strikers character left, who plays him in the pill (gold when swapped)
            valueRow(null, slot[1], g == null ? "themselves" : guestLabel(g), g != null ? ACCENT : 0xFF0E1428,
                    g != null ? LobbyKit.NAVY_BOT : Color.WHITE, v -> showCharacterPick(slot));
        }
        if (!picks.isEmpty()) {
            page.addView(button("Everyone back to normal", false, v -> {
                writeGuests(new java.util.LinkedHashMap<>());
                showCharacters();
            }));
        }
    }

    private void showCharacterPick(final String[] slot) {
        // r157: a level under WHO PLAYS WHO; BACK goes back up to it
        openPage(LobbyKit.GUESTS, this::showCharacters, -1, "GUESTS", "WHO PLAYS WHO", "INSTEAD OF " + slot[1]);
        final java.util.Map<String, String> picks = readGuests();
        final String current = picks.get(slot[0]);
        page.addView(checkRow(slot[1] + " themselves", current == null, true, v -> {
            picks.remove(slot[0]);
            writeGuests(picks);
            showCharacters();
        }));
        for (final String pack : guestPacks()) {
            page.addView(checkRow(guestLabel(pack), pack.equals(current), true, v -> {
                picks.put(slot[0], pack);
                writeGuests(picks);
                showCharacters();
            }));
        }
    }

    /** One game button: the Quest buttons to pick from. One already taken swaps with it. */
    private void showControlPick(final int action) {
        // r157: a level under CONTROLS; BACK goes back up to it
        openPage(LobbyKit.SETTINGS, this::showControls, RAIL_CONTROLS, "SETTINGS", "CONTROLS",
                ACTION_LABELS[action].replaceFirst("\\s*\\(.*$", ""));
        title(ACTION_LABELS[action], 24);
        final String[] map = readControls();
        note("Pick the Quest button for it. Now: " + sourceLabel(map[action]) + ".");
        for (int s = 0; s < SOURCES.length; s++) {
            final String source = SOURCES[s];
            String owner = "";
            for (int i = 0; i < ACTIONS.length; i++) {
                if (i != action && map[i].equals(source) && !source.equals("none")) owner = "   (now " + ACTION_LABELS[i] + ": they swap)";
            }
            final boolean now = source.equals(map[action]);
            page.addView(checkRow(SOURCE_LABELS[s] + (now ? "" : owner), now, true, v -> {
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
    }

    // --- strikers-quest r46: RetroAchievements (Lance: "its allowed to use THEIR strikers achievement
    // set"). Log in once; the page lists Super Mario Strikers' set and what this account has (RetroAch).
    // Off the UI thread throughout (Android refuses network calls there).

    private int raToken;  // the page's background work (a newer page, or Back, ends it)

    private void showRetroAch() {
        openPage(LobbyKit.SETTINGS, this::showHome, RAIL_ACH, "SETTINGS", "ACHIEVEMENTS");  // r157
        final int token = ++raToken;  // after openPage, which ends the last page's work (++raToken)
        title("RetroAchievements", 24);
        final RetroAch.Login login = RetroAch.saved(this);
        if (login == null) {
            note("Log in with your RetroAchievements account (retroachievements.org). Only a login token is kept,"
                    + " never your password. Casual mode.");
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
        new Thread(() -> {
            java.util.List<RetroAch.Achievement> set = null;
            String error = null;
            try {
                set = RetroAch.fetchSet(this, login);
                // r57: the trophy shelf's badge board, without holding up this list.
                new Thread(() -> RetroAch.fetchBadges(this), "strikers-ra-badges").start();
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
                        + " points.\nThey unlock as you play. Logged in with the game open? Start it again.");
                status.setTextColor(Color.rgb(190, 200, 215));
                for (RetroAch.Achievement a : got) {
                    // r157: unlocked = lit with the green check (no text tick)
                    Button b = headRow(a.title + "   (" + a.points + " points)", a.description, a.unlocked, null);
                    if (a.unlocked) markChosen(b);
                    b.setEnabled(false);
                    b.setTextColor(a.unlocked ? Color.WHITE : MUTED);
                    list.addView(b);
                }
            });
        }, "strikers-ra-set").start();
    }

    // --- strikers-quest r147: the menu music (MenuMusic). The button steps round Lofi, My music, Both, Off (the game's
    // own), written where the game's pause row keeps it (xr_room_options.txt "music N"); his own songs go in
    // Strikers/music (its README says so).

    /** r147: "Lofi", "My music (3)", "Both (+3)", "Off" (r157: the value pill on SETTINGS > SOUND). */
    private String menuMusicValue() {
        final File home = StrikersHome.homeDir(this);
        final int mode = MenuMusic.readMode(home);
        final int mine = MenuMusic.mySongCount(home);
        String s = MenuPlaylist.MODE_NAMES[mode];
        if (mode == MenuPlaylist.MINE) s += mine == 0 ? " (folder empty)" : " (" + mine + ")";
        else if (mode == MenuPlaylist.BOTH && mine > 0) s += " (+" + mine + ")";
        return s;
    }

    private void cycleMenuMusic() {
        final File home = StrikersHome.homeDir(this);
        final int next = (MenuMusic.readMode(home) + 1) % MenuPlaylist.MODES;
        MenuMusic.writeMode(home, next);
        if (music != null) music.reloadSetting();
        showSound();  // r157: the row is on SETTINGS > SOUND now
    }

    /** r157 (menu overhaul): SETTINGS > SOUND: the menu music (also in the pause menu, the same file). */
    private void showSound() {
        openPage(LobbyKit.SETTINGS, this::showHome, RAIL_SOUND, "SETTINGS", "SOUND");
        final File home = StrikersHome.homeDir(this);
        final int mode = MenuMusic.readMode(home);
        valueRow(new LobbyKit.Icon(LobbyKit.ICON_NOTE, LobbyKit.INK_SOFT), "MENU MUSIC", menuMusicValue(),
                mode != MenuPlaylist.OFF, v -> cycleMenuMusic());  // r147
        line("Pull to step round: " + TextUtils.join(", ", MenuPlaylist.MODE_NAMES)
                + " (off = the game's own). Also in the pause menu.");
        TextView mine = note("Your own songs: put .mp3 / .ogg / .m4a files in " + MenuMusic.myFolder(home).getAbsolutePath()
                + " (a song named theme.mp3 is your franchise theme).");
        mine.setTextSize(MIN_SP);
        mine.setTextColor(mode == MenuPlaylist.MINE || mode == MenuPlaylist.BOTH ? LobbyKit.INK_SOFT : MUTED);
        line("Crowd, music, effects + voice volumes: PAUSED > SETTINGS > SOUND, in a match.").setTextColor(MUTED);
    }

    /** r147: the NOW PLAYING card: in over a quarter second, 4 s up, out over 0.6 s. */
    private void showNowPlaying(String title, String artist) {
        if (nowPlaying == null || isFinishing()) return;
        final String line = artist == null || artist.isEmpty() ? title : title + "  -  " + artist;
        SpannableString s = new SpannableString("NOW PLAYING\n" + line);
        s.setSpan(new RelativeSizeSpan(0.8f), 0, 11, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        s.setSpan(new StyleSpan(Typeface.BOLD), 0, 11, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        s.setSpan(new android.text.style.ForegroundColorSpan(ACCENT), 0, 11, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        nowPlaying.setText(s);
        final int token = ++nowPlayingToken;
        nowPlaying.animate().cancel();
        nowPlaying.setVisibility(View.VISIBLE);
        nowPlaying.animate().alpha(1f).setDuration(250).start();
        nowPlaying.postDelayed(() -> {
            if (token != nowPlayingToken) return;
            nowPlaying.animate().alpha(0f).setDuration(600).withEndAction(() -> {
                if (token == nowPlayingToken) nowPlaying.setVisibility(View.GONE);
            }).start();
        }, 4000);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (music != null) music.setForeground(true);  // r147
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (music != null) music.setForeground(false);  // r147
    }

    @Override
    protected void onDestroy() {
        if (music != null) music.release();  // r147
        music = null;
        super.onDestroy();
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
            showOnline();  // r157: the row is on ONLINE now
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
        showOnline();  // r157: the note in the strip; the pause menu's row is under SETTINGS > SOUND now
        notice("Voice chat is on: in online matches you hear each other. Off: here, or in the pause menu"
                + " (PAUSED > SETTINGS > SOUND).", ACCENT);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == REQUEST_SCENE) {  // r197
            final boolean ok = results.length > 0 && results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED;
            notice(ok ? "Room scan on: weather and effects land on your real furniture (set it up in Quest Settings >"
                            + " Physical space > Space setup)."
                      : "No room scan: weather and effects use your floor, as before. SETTINGS > HOME GROUND >"
                            + " USE MY ROOM SCAN asks again.", ok ? ACCENT : WARN);
            return;
        }
        if (requestCode != REQUEST_MIC) return;
        if (results.length > 0 && results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            voiceGranted();
        } else {
            writeVoice(false);
            showOnline();  // r157
            notice("Voice chat needs the microphone. Press Voice chat again and choose Allow.", WARN);
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
            n.setTextSize(MIN_SP);
            n.setTypeface(lucky);  // round 2
            n.setPadding(dp(5), 0, 0, 0);
            chip.addView(n, wrap());
            line.addView(chip, wrap());
        }
    }

    private void showTourneys() {
        // r157: openPage ends the last page's link, search and countdown, as this page did itself
        openPage(LobbyKit.ONLINE, this::showOnline, -1, "ONLINE", "TOURNAMENTS");
        note("4 or 8 players, knockout. It starts when full; each match is a Grudge Match, back here between"
                + " rounds.");
        // The team name, kept as typed; the hub tidies it.
        TextView label = line("Your team name");
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
        tourneyLink.setTextSize(MIN_SP);
        tourneyLink.setTextColor(MUTED);
        tourneyBody = new LinearLayout(this);
        tourneyBody.setOrientation(LinearLayout.VERTICAL);
        tourneyBody.setGravity(Gravity.CENTER_HORIZONTAL);
        page.addView(tourneyBody, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
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
            c.setTextSize(30);
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
        TextView t = luckyText(text, MIN_SP, colour);  // r157 round 2: the game's font, 40 px rule
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
            // r157: in the notice strip, on whatever page the button was
            notice("Turn on \"Allow access to manage all files\" so the game can read your game file, then press"
                    + " the same button again.", WARN);
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
        if (io.github.newcoke.strikers.guests.GuestSetup.running) {  // 1.14: half-made guests aren't loadable
            showGuests();
            return;
        }
        if (!readyToStart(code)) return;
        Intent game = new Intent(this, StrikersActivity.class);
        game.putExtra(EXTRA_ROOM, code);
        // r64: made here = the code Host showed (not a tournament's or Quick match's room).
        game.putExtra(EXTRA_ROOM_MAKER, !code.isEmpty() && tourneyMid.isEmpty() && code.equals(hostedCode));
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

    /**
     * A heading on the page. r157: Luckiest Guy, capitals; 24 sp and up = the page's own headline (cream, centred),
     * smaller = a section label (dim, left, as VERSION in the mockups).
     */
    private void title(String text, int sp) {
        final boolean headline = sp >= 24;
        TextView t = luckyText(text, headline ? 30 : MIN_SP, headline ? LobbyKit.CREAM : LobbyKit.INK_DIM);
        t.setGravity(headline ? Gravity.CENTER : Gravity.START);
        t.setPadding(dp(4), dp(headline ? 8 : 16), dp(4), dp(headline ? 8 : 6));
        if (headline) t.setShadowLayer(dp(3), 0, dp(2), 0xC0000000);
        page.addView(t, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    /** Body text. r157 round 2: the game's font too, at MIN_SP (the 40 px rule), the words cut short to suit. */
    private TextView note(String text) {
        TextView t = luckyText(text, MIN_SP, LobbyKit.INK_SOFT);
        t.setGravity(Gravity.CENTER);
        t.setLineSpacing(dp(3), 1f);
        t.setPadding(dp(6), dp(6), dp(6), dp(12));
        page.addView(t, wrap());
        return t;
    }

    /** r157: one short line in the game's font (a how-to cut to one line, a page's warning). */
    private TextView line(String text) {
        TextView t = luckyText(text, MIN_SP, LobbyKit.INK_SOFT);
        t.setGravity(Gravity.CENTER);
        t.setLineSpacing(dp(3), 1f);
        t.setPadding(dp(8), dp(8), dp(8), dp(6));
        page.addView(t, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return t;
    }

    /** r157: gold (the page's main action) or a navy pill, in the game's font, with the gold ring on hover. */
    private Button button(String text, boolean primary, View.OnClickListener onClick) {
        Button b = new Button(this);
        b.setText(text);
        b.setTypeface(lucky);
        b.setAllCaps(true);
        b.setTextSize(24);
        b.setIncludeFontPadding(false);
        b.setPadding(dp(16), dp(4), dp(16), 0);
        b.setTextColor(primary ? LobbyKit.NAVY_BOT : Color.WHITE);
        b.setBackground(LobbyKit.pressable(LobbyKit.round(primary ? ACCENT : LobbyKit.PILL, dp(20)), 0, dp(20),
                getResources().getDisplayMetrics().density, primary ? Color.WHITE : ACCENT));
        b.setStateListAnimator(null);
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(380), dp(60));
        lp.topMargin = dp(10);
        b.setLayoutParams(lp);
        return b;
    }

    /** r41: one file in the game file list; onClick null = shown but not pickable. */
    private Button row(String text, boolean usable, View.OnClickListener onClick) {
        Button b = new Button(this);
        // r157 round 2: the game's font at MIN_SP; capitals by hand (setAllCaps would drop headRow's spans)
        b.setText(text.toUpperCase(java.util.Locale.ROOT));
        b.setAllCaps(false);
        b.setTypeface(lucky);
        b.setTextSize(MIN_SP);
        b.setLineSpacing(dp(3), 1f);
        b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        b.setTextColor(usable ? Color.WHITE : MUTED);
        // r157: one focus style (the gold ring on hover / focus), so the gold edge that marked "usable" is gone
        b.setBackground(LobbyKit.pressable(LobbyKit.round(usable ? CARD : LobbyKit.TILE, dp(16)), 0, dp(16),
                getResources().getDisplayMetrics().density));
        b.setStateListAnimator(null);
        int p = dp(16);
        b.setPadding(p, dp(12), p, dp(12));
        b.setEnabled(onClick != null);
        b.setOnClickListener(onClick);
        // r43b: the panel's width, wrapping (Lance: the Version rows ran past the panel at 620 dp).
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    /** r157: row() with its first line in the game's font, larger: "PASS (GAMECUBE A)" over "A button". */
    private Button headRow(String head, String rest, boolean usable, View.OnClickListener onClick) {
        final String h = head.toUpperCase(java.util.Locale.ROOT);
        Button b = row(h + "\n" + rest, usable, onClick);
        SpannableString text = new SpannableString(b.getText());
        text.setSpan(new RelativeSizeSpan(1.15f), 0, h.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        text.setSpan(new ForegroundColorSpan(LobbyKit.CREAM), 0, h.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        b.setText(text);
        return b;
    }

    /** r157 (MENUS-PLAN.md rule 8): a pick in a list; the chosen one lit with a green check. */
    private Button checkRow(String text, boolean chosen, boolean usable, View.OnClickListener onClick) {
        Button b = row(text, usable, onClick);
        if (chosen) markChosen(b);
        return b;
    }

    /** r157: lit (the hub's TILE_ON blue) + the green check at the row's end. */
    private void markChosen(Button b) {
        b.setBackground(LobbyKit.pressable(LobbyKit.round(LobbyKit.TILE_ON, dp(16)), 0, dp(16),
                getResources().getDisplayMetrics().density));
        LobbyKit.Icon check = new LobbyKit.Icon(LobbyKit.ICON_CHECK, Color.WHITE);
        check.setBounds(0, 0, dp(32), dp(32));
        b.setCompoundDrawables(null, null, check, null);
        b.setCompoundDrawablePadding(dp(12));
    }

    /** r157: a setting as a row: icon, its name, and its value in a pill (green when on); a pull changes it. */
    private View valueRow(android.graphics.drawable.Drawable icon, String label, String value, boolean on,
                          View.OnClickListener onClick) {
        return valueRow(icon, label, value, on ? LobbyKit.CHECK : 0xFF0E1428, Color.WHITE, onClick);
    }

    /** valueRow() with the pill's own colours (round 2: CONTROLS / WHO PLAYS WHO, gold = changed); icon may be null. */
    private View valueRow(android.graphics.drawable.Drawable icon, String label, String value, int fill, int ink,
                          View.OnClickListener onClick) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setPadding(dp(18), dp(10), dp(12), dp(10));
        GradientDrawable rowBg = LobbyKit.round(LobbyKit.TILE, dp(18));
        rowBg.setStroke(dp(2), 0x30FFFFFF);  // round 2: a faint edge, so the row reads on the lighter navy at the top
        r.setBackground(LobbyKit.pressable(rowBg, 0, dp(18), getResources().getDisplayMetrics().density));
        r.setClickable(true);
        r.setFocusable(true);
        r.setOnClickListener(onClick);
        r.setContentDescription(label + ": " + value);
        if (icon != null) {
            ImageView i = new ImageView(this);
            i.setImageDrawable(icon);
            r.addView(i, new LinearLayout.LayoutParams(dp(30), dp(30)));
        }
        TextView name = luckyText(label, 24, Color.WHITE);
        name.setPadding(dp(icon != null ? 14 : 0), dp(3), dp(10), 0);
        r.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView v = luckyText(value, MIN_SP, ink);
        v.setGravity(Gravity.CENTER);
        v.setMinWidth(dp(150));
        v.setMaxWidth(dp(360));
        v.setMaxLines(2);
        v.setBackground(LobbyKit.round(fill, dp(18)));
        v.setPadding(dp(18), dp(8), dp(18), dp(5));
        r.addView(v, wrap());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        page.addView(r, lp);
        return r;
    }

    /** r157: the title bar's release pill: "ALPHA 1.18" out of versionName "0.1.18-dev (r157)". */
    private String releaseName() {
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("^\\d+\\.(\\d+)\\.(\\d+)").matcher(versionName());
        return m.find() ? "ALPHA " + m.group(1) + "." + m.group(2) : "ALPHA";
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
