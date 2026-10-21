package io.github.newcoke.strikers;

import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import dev.encounter.aurora.AuroraSurface;

/** O jogo. Tudo que decide se ele pode começar está em {@link SetupActivity}. */
public class StrikersActivity extends SDLActivity {

    // strikers-quest r39b: how much longer than SDL's own 1 s a closing game thread gets before the
    // process ends anyway (onDestroy).
    private static final long GAME_THREAD_GRACE_MS = 5000;

    private OverlayView overlay;
    private HudView hud;
    private Preferencias prefs;
    private final Handler relogio = new Handler(Looper.getMainLooper());

    @Override
    protected String[] getLibraries() {
        // Só uma: o SDL é linkado dentro do libmain.so em vez de carregado ao
        // lado. Ver a nota em tools/configure-android.sh.
        return new String[] { "main" };
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        // A do Aurora, não a do SDL. Ela avisa o renderizador quando a surface
        // some, e no Android a surface some toda vez que o jogador troca de
        // aplicativo — uma swapchain do WebGPU ainda apontada para um
        // ANativeWindow destruído não é um quadro preto, é um travamento.
        return new AuroraSurface(context);
    }

    @Override
    protected void attachBaseContext(Context base) {
        // O idioma escolhido no launcher, antes de qualquer getString. Ver
        // Idiomas.envolver.
        super.attachBaseContext(Idiomas.envolver(base));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // strikers-quest r39b: SDL starts once per process; a second onCreate in the same one ends
        // the process on the spot (SDLActivity's recreate guard calls System.exit). This process
        // ends with the game (onDestroy), so the only way here is a start while the last game is
        // still shutting down, or Android recreating the activity for a configuration change. Send
        // the player back to the launcher panel to start again, and end this process once the last
        // game's thread is done, rather than vanish.
        if (mActivityCreated || mSDLMainFinished) {
            android.util.Log.w("strikers", "the game is still closing in this process, back to the panel");
            startActivity(new android.content.Intent(this, LobbyActivity.class));
            waitForGameThread(mSDLThread);
            android.os.Process.killProcess(android.os.Process.myPid());
            super.onCreate(savedInstanceState);  // not reached; if it were, SDL's guard ends the process
            return;
        }
        // strikers-quest: the disc sits in Download, which takes "all files access". r39b: the
        // launcher panel asks for it before it starts the game (LobbyActivity.start). This is for a
        // start that did not come through the panel (adb, a self-test): open that switch and close.
        // Android insists on super.onCreate even so, and SDL has then run in this process, which
        // onDestroy ends, so the next start gets a fresh one.
        if (!StrikersHome.hasAllFilesAccess() && !StrikersHome.hasGameData(this)
                && LobbyActivity.askForFileAccess(this)) {
            super.onCreate(savedInstanceState);
            finish();
            return;
        }
        // Antes do super, que é quem carrega o libmain.so: o jogo lê a
        // configuração do ambiente num construtor de biblioteca, então o que for
        // definido depois disto é tarde demais.
        StrikersHome.prepare(this);
        // strikers-quest r39: the online room picked in the launcher panel (LobbyActivity), "" for
        // offline. The network layer reads it when the game first polls the network.
        // r39b: only when the panel passed one. A start without it (adb, a self-test) keeps what
        // env.txt set in prepare just above, or no room at all.
        final android.content.Intent start = getIntent();
        if (start != null && start.hasExtra(LobbyActivity.EXTRA_ROOM)) {
            setRoomEnv(roomFrom(start));
            // r64: whether this headset made that room (co-op Cup: the maker's cup, the captain).
            try {
                android.system.Os.setenv("STRIKERS_ROOM_MAKER",
                        start.getBooleanExtra(LobbyActivity.EXTRA_ROOM_MAKER, false) ? "1" : "0", true);
            } catch (android.system.ErrnoException e) {
                android.util.Log.e("strikers", "setenv STRIKERS_ROOM_MAKER failed", e);
            }
        }
        // strikers-quest r45: a tournament match: its id, for the result the network layer writes.
        if (start != null && start.hasExtra(LobbyActivity.EXTRA_TOURNEY)) {
            try {
                String mid = start.getStringExtra(LobbyActivity.EXTRA_TOURNEY);
                android.system.Os.setenv("STRIKERS_TOURNEY", mid != null ? mid : "", true);
            } catch (android.system.ErrnoException e) {
                android.util.Log.e("strikers", "setenv STRIKERS_TOURNEY failed", e);
            }
        }
        super.onCreate(savedInstanceState);

        // Uma partida são minutos segurando o manche numa direção, o que nenhum
        // aparelho conta como atividade.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        telaCheia();

        prefs = new Preferencias(this);
        overlay = new OverlayView(this);
        overlay.recarregar(prefs);
        overlay.aoPedirMenu(() -> {
            overlay.soltarTudo();
            MenuConfig.abrir(this, prefs, () -> overlay.recarregar(prefs));
        });
        addContentView(overlay, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // O painel de desempenho, no topo ao centro: e a unica faixa da tela que
        // o overlay de toque nao usa. Os cantos sao todos dos botoes.
        hud = new HudView(this);
        FrameLayout.LayoutParams lpHud = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lpHud.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        lpHud.topMargin = Vitrine.dp(this, 8);
        addContentView(hud, lpHud);

        prepararVidro();
        seguirControleFisico();
    }

    /** strikers-quest r39: src/platform/xr_net.cpp; takes effect between matches. */
    private static native void nativeSetRoom(String room);

    /**
     * strikers-quest r43: src/platform/xr_net.cpp; true once after it refused an online match (the
     * friend is on the other Version, or another build) and wrote why for the launcher panel.
     */
    private static native boolean nativeTakeOnlineNote();

    private boolean onlineNoteHook = true;  // r43: false once this build turns out not to have it

    /** strikers-quest r45: src/platform/xr_net.cpp; the tournament match the next room is for. */
    private static native void nativeSetTourney(String mid);

    /** strikers-quest r64: src/platform/xr_net.cpp; this headset made the next room (Host). */
    private static native void nativeSetRoomMaker(boolean maker);

    /** strikers-quest r45: true once after a tournament match ended and tourney-result.txt was written. */
    private static native boolean nativeTakeTourneyResult();

    /**
     * strikers-quest r45: a tournament match is over: the launcher panel comes up on its Tournaments
     * page, which reports the result and shows the bracket. The game stays (paused behind it), so the
     * next match starts without loading it again.
     */
    private boolean backToPanelForTourney() {
        if (!onlineNoteHook) return false;
        boolean done;
        try {
            done = nativeTakeTourneyResult();
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
        if (!done) return false;
        android.util.Log.i("strikers", "tournament match over, to the Tournaments page");
        android.content.Intent panel = new android.content.Intent(this, LobbyActivity.class);
        panel.putExtra(LobbyActivity.EXTRA_SHOW, LobbyActivity.SHOW_TOURNEY);
        startActivity(panel);
        return true;
    }

    /**
     * strikers-quest r43: an online match refused: back to the launcher panel, which says why and has
     * the Version button (a Version is only taken when a game starts), and this game ends.
     */
    private boolean backToPanelForOnlineNote() {
        if (!onlineNoteHook) return false;
        boolean note;
        try {
            note = nativeTakeOnlineNote();
        } catch (UnsatisfiedLinkError e) {
            onlineNoteHook = false;
            return false;
        }
        if (!note) return false;
        android.util.Log.i("strikers", "online match refused, back to the panel (online-note.txt)");
        startActivity(new android.content.Intent(this, LobbyActivity.class));
        finish();
        return true;
    }

    private static String roomFrom(android.content.Intent intent) {
        String room = intent != null ? intent.getStringExtra(LobbyActivity.EXTRA_ROOM) : null;
        return room != null ? LobbyActivity.clean(room) : "";
    }

    private static void setRoomEnv(String room) {
        try {
            android.system.Os.setenv("STRIKERS_ROOM", room, true);
        } catch (android.system.ErrnoException e) {
            android.util.Log.e("strikers", "setenv STRIKERS_ROOM failed", e);
        }
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        // strikers-quest r43: the Version is taken when the game starts (StrikersHome.prepare). The
        // panel ends a game left running on the old one when the Version changes; should one still be
        // here, it goes back to the panel and ends, so the next start is a fresh game on the new one.
        if (intent != null && intent.hasExtra(LobbyActivity.EXTRA_EDITION)
                && StrikersHome.startedEdition != null
                && !StrikersHome.startedEdition.equals(intent.getStringExtra(LobbyActivity.EXTRA_EDITION))) {
            android.util.Log.w("strikers", "the panel's Version changed while this game ran: ending it");
            LobbyActivity.writeOnlineNote(this, LobbyActivity.NOTE_VERSION_CHANGED);
            startActivity(new android.content.Intent(this, LobbyActivity.class));
            finish();
            return;
        }
        // The game was already running and the panel picked a room (or offline) again.
        if (intent != null && intent.hasExtra(LobbyActivity.EXTRA_ROOM)) {
            setIntent(intent);
            final String room = roomFrom(intent);
            // strikers-quest r39b: not into the environment as well. The game thread may be reading
            // it right now, and setenv can move the whole table under it. nativeSetRoom alone is
            // enough: the network layer only reads STRIKERS_ROOM when nothing came through it.
            try {
                // r45: the tournament match this room is for ("" for none), before the room itself.
                final String mid = intent.getStringExtra(LobbyActivity.EXTRA_TOURNEY);
                nativeSetTourney(mid != null ? mid : "");
                nativeSetRoomMaker(intent.getBooleanExtra(LobbyActivity.EXTRA_ROOM_MAKER, false));  // r64
                nativeSetRoom(room);
            } catch (UnsatisfiedLinkError e) {
                android.util.Log.w("strikers", "no native room switch in this build", e);
            }
        }
    }

    /**
     * O vidro dos controles: a arte de fundo, desfocada uma vez.
     *
     * <p>Numa thread à parte porque o desfoque é uma média móvel sobre um
     * bitmap, e fazê-lo no onCreate atrasa o primeiro quadro do jogo por nada —
     * até ficar pronto o overlay desenha com a base escura, que é o mesmo
     * aspecto sem a textura por baixo.
     */
    private void prepararVidro() {
        new Thread(() -> {
            try {
                android.graphics.Bitmap arte = Vitrine.arte(this, 480);
                if (arte == null) return;
                final android.graphics.Bitmap fosco = Vidro.desfocar(arte, 240, 12);
                arte.recycle();
                runOnUiThread(() -> {
                    if (overlay != null) overlay.definirVidro(fosco);
                });
            } catch (Throwable t) {
                // Um vidro que não carrega é um detalhe de aparência, não um
                // motivo para o jogo não abrir.
                android.util.Log.w("strikers", "sem vidro para o overlay", t);
            }
        }, "vidro").start();
    }

    /**
     * Esconde as barras do sistema e deixa a janela ir até as bordas.
     *
     * <p>Não é só estética: sem isto a barra de navegação fica por cima do canto
     * inferior direito, que é exatamente onde o polegar direito vive, e o toque
     * vai para o sistema em vez de ir para o jogo.
     */
    private void telaCheia() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                  | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                  | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_FULLSCREEN
                  | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
    }

    /**
     * Some com o overlay enquanto houver um controle de verdade ligado.
     *
     * <p>Quem decide é o lado nativo, e não o Java: é ele que enumera os
     * gamepads do SDL e sabe qual deles é o nosso. Toque.ligada() responde
     * exatamente isso — existe um pad virtual? — e perguntar uma vez por segundo
     * é barato, porque do outro lado é uma leitura atômica. Um controle que
     * chega ou sai no meio de uma partida não é coisa que se perceba mais
     * rápido do que isso.
     */
    private void seguirControleFisico() {
        relogio.postDelayed(new Runnable() {
            @Override public void run() {
                if (backToPanelForOnlineNote()) return;  // strikers-quest r43
                backToPanelForTourney();                 // r45
                if (overlay != null) {
                    final boolean mostrar = Toque.ligada();
                    final int quer = mostrar ? View.VISIBLE : View.GONE;
                    if (overlay.getVisibility() != quer) {
                        if (!mostrar) overlay.soltarTudo();
                        overlay.setVisibility(quer);
                    }
                }
                relogio.postDelayed(this, 1000);
            }
        }, 1000);
    }

    @Override
    protected void onResume() {
        super.onResume();
        telaCheia();
        if (overlay != null && prefs != null) overlay.recarregar(prefs);
        if (hud != null && prefs != null) {
            hud.setVisibility(prefs.ligado(Preferencias.HUD) ? View.VISIBLE : View.GONE);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean temFoco) {
        super.onWindowFocusChanged(temFoco);
        if (temFoco) telaCheia();
        else if (overlay != null) overlay.soltarTudo();
    }

    @Override
    protected void onDestroy() {
        relogio.removeCallbacksAndMessages(null);
        // strikers-quest r39b: taken before SDL's onDestroy, which tells the game thread to quit and
        // waits for it, but for 1 s at most.
        final Thread game = mSDLThread;
        super.onDestroy();
        // strikers-quest r39b: SDL starts once per process (see onCreate), so this activity has a
        // process of its own (":game", AndroidManifest.xml) and the process ends here with it: the
        // next start from the panel gets a fresh one. Not before the game thread is done: it
        // finishes the frame it is in (memory card saves are written inside frames), then shuts
        // down Vulkan and OpenXR. That wait runs on a thread of its own, so this one is not held
        // up, and gives up after GAME_THREAD_GRACE_MS. Not at all for a configuration change:
        // Android creates the activity again at once, and onCreate sends that start to the panel.
        if (isChangingConfigurations()) return;
        final Runnable end = () -> android.os.Process.killProcess(android.os.Process.myPid());
        if (game == null || !game.isAlive()) {
            relogio.post(end);
            return;
        }
        new Thread(() -> {
            waitForGameThread(game);
            relogio.post(end);
        }, "strikers-end").start();
    }

    /** strikers-quest r39b: waits for a closing game thread, up to GAME_THREAD_GRACE_MS. */
    private static void waitForGameThread(Thread game) {
        if (game == null) return;
        try {
            game.join(GAME_THREAD_GRACE_MS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (game.isAlive()) {
            android.util.Log.w("strikers", "the game thread did not finish, ending the process anyway");
        }
    }
}
