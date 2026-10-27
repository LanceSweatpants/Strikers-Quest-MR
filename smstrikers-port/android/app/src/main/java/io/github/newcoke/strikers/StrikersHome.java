package io.github.newcoke.strikers;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Environment;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * Everything the native side expects to find on disk, put there before the native side is loaded.
 *
 * <p>The port has one idea of where its files are: the directory holding the executable. Android has
 * no executable to hold them, so {@code port_executable_dir()} reads STRIKERS_HOME instead and this
 * class is what sets it. Past that point nothing about the game is Android-specific -- strikers.ini,
 * the log, the memory card saves, the button prompt artwork and the search for game data all work
 * off that one directory exactly as they do beside strikers.exe.
 *
 * <p>A pasta é {@code /storage/emulated/0/Strikers}, na raiz do armazenamento: ver {@link #homeDir}
 * para por que ali e não em {@code Android/data}, e o que isso custa.
 */
final class StrikersHome {
    private static final String TAG = "strikers";
    private static final String PREFS = "strikers-home";
    private static final String KEY_STAGED_VERSION = "staged-assets-version";

    /**
     * Public folders a player might reasonably have put a disc in, in the order searched. The
     * empty string is the root of shared storage itself -- what a file manager opens on, and where
     * a file dropped over USB lands unless it is dragged somewhere else.
     */
    private static final String[] PUBLIC_FOLDERS = {
        "", "strikers", "Games/Strikers", "ROMs/GameCube", "Download",
    };

    private StrikersHome() {}

    /** O nome da pasta na raiz do armazenamento. */
    static final String PASTA = "Strikers";

    /** {@code /storage/emulated/0/Strikers}, exista ela ou não. */
    static File raiz() {
        return new File(Environment.getExternalStorageDirectory(), PASTA);
    }

    /**
     * A pasta de dados do jogo.
     *
     * <p>É a da RAIZ do armazenamento, {@code /storage/emulated/0/Strikers}, e
     * não a de {@code Android/data}. A diferença é para quem usa: desde o Android
     * 11 o gerenciador de arquivos do próprio aparelho não abre
     * {@code Android/data}, então uma pasta lá só é alcançável por um PC. Uma na
     * raiz aparece junto de Download e DCIM, que é onde alguém procuraria.
     *
     * <p>O PREÇO É UMA PERMISSÃO. Escrever fora da própria pasta exige acesso a
     * todos os arquivos, e sem ele o único lugar que resta é justamente
     * {@code Android/data} — que é o que isto devolve enquanto a permissão não
     * estiver dada. A tela inicial explica e oferece o botão.
     */
    static File homeDir(Context context) {
        if (hasAllFilesAccess()) {
            File raiz = raiz();
            if (raiz.isDirectory() || raiz.mkdirs()) {
                migrar(context, raiz);
                return raiz;
            }
        }
        File reserva = context.getExternalFilesDir(null);
        if (reserva == null) {
            // Sem armazenamento externo montado. A interna ainda funciona; só pôr
            // um disco nela é que fica difícil.
            reserva = context.getFilesDir();
        }
        //noinspection ResultOfMethodCallIgnored
        reserva.mkdirs();
        return reserva;
    }

    /** Se a pasta em uso já é a da raiz. A tela inicial pergunta isto. */
    static boolean naRaiz(Context context) {
        return hasAllFilesAccess() && raiz().isDirectory();
    }

    /**
     * Leva o que estava em {@code Android/data} para a pasta nova, uma vez.
     *
     * <p>Sem isto, conceder a permissão faria o jogo perder o disco, o
     * strikers.ini e os saves de uma vez: tudo continuaria no lugar antigo, que o
     * jogo deixou de olhar. Renomeia em vez de copiar — as duas pastas estão no
     * mesmo volume, e copiar uma imagem de 1,4 GB seria um minuto de espera para
     * chegar ao mesmo lugar.
     */
    private static void migrar(Context context, File nova) {
        File velha = context.getExternalFilesDir(null);
        if (velha == null || !velha.isDirectory() || velha.equals(nova)) return;
        File[] itens = velha.listFiles();
        if (itens == null) return;
        for (File item : itens) {
            mover(item, new File(nova, item.getName()));
        }
    }

    /**
     * Move um arquivo ou uma árvore inteira.
     *
     * <p>O renomear resolve um arquivo e falha numa PASTA atravessando estes dois
     * caminhos — foi medido: a imagem de disco, o ini e o log passaram, e
     * input-prompts/ ficou para trás. Então a pasta é descida a pé, arquivo por
     * arquivo, e só é apagada depois que esvazia.
     */
    private static void mover(File origem, File destino) {
        if (destino.exists()) return;
        if (origem.isDirectory()) {
            if (!destino.isDirectory() && !destino.mkdirs()) {
                Log.w(TAG, "não deu para criar " + destino);
                return;
            }
            File[] filhos = origem.listFiles();
            if (filhos != null) {
                for (File filho : filhos) {
                    mover(filho, new File(destino, filho.getName()));
                }
            }
            //noinspection ResultOfMethodCallIgnored
            origem.delete();   // só funciona se esvaziou, que é o que se quer
            return;
        }
        if (origem.renameTo(destino)) return;
        // Volumes diferentes, ou uma regra de armazenamento no caminho: copiar é
        // mais lento mas sempre funciona, e o original só sai depois.
        try (InputStream in = new java.io.FileInputStream(origem);
             OutputStream out = new FileOutputStream(destino)) {
            byte[] buffer = new byte[64 * 1024];
            int lido;
            while ((lido = in.read(buffer)) > 0) out.write(buffer, 0, lido);
        } catch (IOException e) {
            Log.w(TAG, "não deu para mover " + origem + " para " + destino, e);
            //noinspection ResultOfMethodCallIgnored
            destino.delete();
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        origem.delete();
    }

    /**
     * Must be called before the native library is loaded: the game reads its configuration out of
     * the environment from a library constructor, which runs inside System.loadLibrary.
     */
    static void prepare(Context context) {
        File home = homeDir(context);
        setenv("STRIKERS_HOME", home.getAbsolutePath());

        stageAssets(context, home);
        writeDefaultConfig(context, home);

        // Only when it is somewhere the native search cannot reach on its own. Left unset, DVDInit
        // finds a files/ folder or a disc image in the home directory itself and reports what it
        // tried, which is a better message than anything passed in from here.
        // strikers-quest r41: the file picked in the launcher panel comes first.
        String chosen = GameFile.chosen(context);
        String elsewhere = chosen != null ? null : findInPublicFolders();
        File inHome = chosen != null ? null : firstDiscImage(home);
        if (chosen != null) {
            setenv("STRIKERS_DATA", chosen);
        } else if (!new File(home, "files/common.ini").exists() && inHome != null) {
            // strikers-quest 1.14: named, not left to DVDInit's lowest-name pick in the home directory, which
            // could take a guest game's image (Melee, Colosseum, Charged) put there beside this one
            setenv("STRIKERS_DATA", inHome.getAbsolutePath());
        } else if (!hasGameDataInHome(home) && elsewhere != null) {
            setenv("STRIKERS_DATA", elsewhere);
        }

        // The game's stderr is a file descriptor nobody reads on Android. Point the port's own log
        // at a file in the home directory instead, where `adb pull` can reach it.
        setenv("STRIKERS_LOG", new File(home, "strikers-log.txt").getAbsolutePath());

        // UM DIAGNÓSTICO, LIGADO DE PROPÓSITO, e não esquecido ligado. Ele faz o
        // feRender escrever uma linha por elemento de menu que a regra de
        // alargamento julga -- o tamanho do quadro, o tamanho da textura e para
        // que lado a decisão foi. São no máximo 128 linhas por execução, uma por
        // elemento distinto, e nada é impresso por quadro.
        //
        // Está aqui porque a escolha entre manter a proporção e esticar a arte
        // dos menus foi feita a partir de uma captura de tela, que mostra o
        // resultado e não quem o causou. Sai quando o alinhamento estiver
        // confirmado no aparelho.
        setenv("STRIKERS_PROBE_WIDEN", "1");

        prepararDriver(context);
        // strikers-quest r43: Classic or Plus, the panel's Version (src/platform/edition.cpp reads it
        // once). Before env.txt, which can still set it for a headset test.
        startedEdition = edition(context);
        setenv("STRIKERS_EDITION", startedEdition);
        // strikers-quest r46: RetroAchievements (src/platform/xr_ra.cpp): the panel's saved login,
        // in the app's private folder, and the name the game's calls carry.
        setenv("STRIKERS_RA_LOGIN", RetroAch.loginFile(context).getAbsolutePath());
        setenv("STRIKERS_RA_UA", RetroAch.userAgent(context));
        // strikers-quest r61: the home ground (src/platform/xr_home.cpp): who this headset is to an
        // online friend (the panel's player id, made here the first time, and team name).
        SharedPreferences lobby = context.getSharedPreferences(LOBBY_PREFS, Context.MODE_PRIVATE);
        String playerId = lobby.getString("playerId", "");
        if (playerId.length() != 16) {
            StringBuilder sb = new StringBuilder();
            java.security.SecureRandom r = new java.security.SecureRandom();
            for (int i = 0; i < 16; i++) sb.append("0123456789abcdef".charAt(r.nextInt(16)));
            playerId = sb.toString();
            lobby.edit().putString("playerId", playerId).commit();
        }
        setenv("STRIKERS_PLAYER_ID", playerId);
        setenv("STRIKERS_TEAM_NAME", lobby.getString("teamName", ""));
        loadEnvFile(home);
    }

    // strikers-quest r43: the Version picked in the launcher panel (LobbyActivity), kept in the panel's
    // preferences beside the game file (GameFile). Plus unless Classic was picked, so a fresh install
    // and an update from alpha 1.2 both start on Plus.
    static final String CLASSIC = "classic";
    static final String PLUS = "plus";
    private static final String LOBBY_PREFS = "lobby";
    private static final String KEY_EDITION = "edition";

    /** strikers-quest r43: the Version this game's process started with (prepare); null before. */
    static String startedEdition;

    /** strikers-quest r43: {@link #CLASSIC} or {@link #PLUS}. */
    static String edition(Context context) {
        String e = context.getSharedPreferences(LOBBY_PREFS, Context.MODE_PRIVATE).getString(KEY_EDITION, PLUS);
        return CLASSIC.equals(e) ? CLASSIC : PLUS;
    }

    /** strikers-quest r43: with commit(), not apply(): the game reads it in its own process. */
    static void chooseEdition(Context context, String edition) {
        context.getSharedPreferences(LOBBY_PREFS, Context.MODE_PRIVATE).edit()
                .putString(KEY_EDITION, CLASSIC.equals(edition) ? CLASSIC : PLUS)
                .commit();
    }

    /**
     * strikers-quest: a developer hook. Each KEY=VALUE line of env.txt in the home folder becomes an
     * environment variable before the game starts (# lines and blanks skipped), so a headset run can
     * be configured over adb -- STRIKERS_CONTROL for the control channel, STRIKERS_XR_* and the
     * like -- without a rebuild. No file, nothing happens.
     */
    private static void loadEnvFile(File home) {
        File env = new File(home, "env.txt");
        if (!env.isFile()) {
            return;
        }
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(env))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                int eq = line.indexOf('=');
                if (eq > 0) {
                    setenv(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
                }
            }
        } catch (java.io.IOException e) {
            Log.w(TAG, "env.txt unreadable", e);
        }
    }

    /**
     * O driver de vídeo que o jogador escolheu, para o src/platform/vulkan_shim.c.
     *
     * <p>Três caminhos, e nenhum deles é adivinhável do lado nativo:
     *
     * <ul>
     *   <li>o diretório de bibliotecas do aplicativo, que é onde o libadrenotools
     *       procura os ganchos com que foi compilado. Tem de ser exatamente esse,
     *       e não uma cópia;
     *   <li>a pasta do driver, que tem de estar no armazenamento PRIVADO — o
     *       {@code dlopen} recusa uma biblioteca de um lugar que outro aplicativo
     *       poderia ter alterado;
     *   <li>o SONAME do .so, que varia de pacote para pacote e vem do meta.json.
     * </ul>
     *
     * <p>Sem escolha, nenhuma das variáveis é definida e o shim cai no driver do
     * sistema — que é o comportamento certo, e não uma falha.
     */
    private static void prepararDriver(Context context) {
        setenv("STRIKERS_VK_HOOK_DIR", context.getApplicationInfo().nativeLibraryDir);

        Drivers.Driver d = Drivers.porId(context, new Preferencias(context)
                .texto(Preferencias.DRIVER));
        if (d == null) {
            Log.i(TAG, "driver de vídeo: o do sistema");
            return;
        }
        setenv("STRIKERS_VK_DRIVER_DIR", d.pasta.getAbsolutePath() + "/");
        setenv("STRIKERS_VK_DRIVER", d.soname);
    }

    /** Whether the game would find its data, without doing anything about it. */
    static boolean hasGameData(Context context) {
        return GameFile.chosen(context) != null || hasGameDataInHome(homeDir(context))
                || findInPublicFolders() != null;
    }

    /** What the game will read, for the setup screen to show. Null when there is nothing. */
    static String describeGameData(Context context) {
        String chosen = GameFile.chosen(context);  // strikers-quest r41
        if (chosen != null) {
            return chosen;
        }
        File home = homeDir(context);
        File extracted = new File(home, "files");
        if (new File(extracted, "common.ini").exists()) {
            return extracted.getAbsolutePath();
        }
        File image = firstDiscImage(home);
        if (image != null) {
            return image.getAbsolutePath();
        }
        return findInPublicFolders();
    }

    /** strikers-quest 1.14: the Super Mario Strikers disc image the game reads, null when it reads an extracted folder or none. */
    static String strikersImage(Context context) {
        String d = describeGameData(context);
        return d != null && new File(d).isFile() ? d : null;
    }

    static boolean hasGameDataInHome(File home) {
        return new File(home, "files/common.ini").exists() || firstDiscImage(home) != null;
    }

    private static void setenv(String name, String value) {
        try {
            Os.setenv(name, value, true);
            Log.i(TAG, name + "=" + value);
        } catch (ErrnoException e) {
            Log.e(TAG, "setenv " + name + " failed", e);
        }
    }

    /**
     * The button prompt artwork, out of the APK and into the home directory, because the game opens
     * it by path and an asset inside an APK has none. Re-staged when the app's version changes, so
     * an upgrade that adds artwork is not left with the old set.
     */
    private static void stageAssets(Context context, File home) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long installed = versionCode(context);
        if (prefs.getLong(KEY_STAGED_VERSION, -1) == installed
                && new File(home, "input-prompts/LICENSE-Kenney.txt").exists()
                && new File(home, "suites").isDirectory()) {
            return;
        }

        try {
            copyAssetTree(context.getAssets(), "input-prompts", new File(home, "input-prompts"));
            // strikers-quest r61: the cup suites' baked rooms, a mesh and two pictures each
            // (src/platform/xr_quest.cpp request_suite_room reads them).
            copyAssetTree(context.getAssets(), "suites", new File(home, "suites"));
            // strikers-quest 1.14: our own franchise data (src/platform/franchise.cpp) and the club / playoff /
            // league logos beside the guests' portraits; the guests themselves are made on the headset from the
            // player's own game files (guests/GuestSetup).
            copyAssetTree(context.getAssets(), "franchise", new File(home, "franchise"));
            copyAssetTree(context.getAssets(), "guestart/portraits", new File(home, "guests/portraits"));
            prefs.edit().putLong(KEY_STAGED_VERSION, installed).apply();
        } catch (IOException e) {
            // Not fatal: the game says so on stderr and keeps the GameCube prompts.
            Log.w(TAG, "could not stage input-prompts", e);
        }
    }

    private static long versionCode(Context context) {
        try {
            android.content.pm.PackageInfo info =
                    context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                return info.getLongVersionCode();
            }
            return info.versionCode;
        } catch (Exception e) {
            return -1;
        }
    }

    private static void copyAssetTree(AssetManager assets, String path, File target)
            throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            copyAssetFile(assets, path, target);
            return;
        }
        if (!target.isDirectory() && !target.mkdirs()) {
            throw new IOException("could not create " + target);
        }
        for (String child : children) {
            copyAssetTree(assets, path + "/" + child, new File(target, child));
        }
    }

    private static void copyAssetFile(AssetManager assets, String path, File target)
            throws IOException {
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("could not create " + parent);
        }
        byte[] buffer = new byte[64 * 1024];
        try (InputStream in = assets.open(path);
             OutputStream out = new FileOutputStream(target)) {
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
    }

    /**
     * A strikers.ini in the home directory if there is not one already, so the player has a file to
     * edit and can see the keys that matter on a handheld with the defaults this port picked.
     */
    private static void writeDefaultConfig(Context context, File home) {
        File ini = new File(home, "strikers.ini");
        if (ini.exists()) {
            return;
        }
        String contents =
                "# Super Mario Strikers, native port: Android settings.\n"
              + "#\n"
              + "# Every key here is also an environment variable with a STRIKERS_ prefix, and the\n"
              + "# desktop port's strikers.ini.example documents the full set. This file is written\n"
              + "# once, on first run, and never overwritten: it is yours to edit.\n"
              + "\n"
              + "# Where the game data is. Left unset, the game looks for a `files` folder or a\n"
              + "# disc image (.iso, .gcm, .ciso, .gcz) in this same directory:\n"
              + "#   " + home.getAbsolutePath() + "\n"
              + "#data =\n"
              + "\n"
              + "# Only three keys are set here, and each is set because Android differs, not\n"
              + "# because the port's own default is wrong. Everything else is left at it.\n"
              + "\n"
              + "# Vulkan is the only backend Dawn has on Android. `auto` would land here anyway;\n"
              + "# it is written out so the answer is in the file rather than in someone's head.\n"
              + "backend = vulkan\n"
              + "\n"
              + "# The game's own pace is 60. A phone panel that refreshes at 120 would otherwise\n"
              + "# have the limiter follow it, which is twice the GPU work, twice the heat and\n"
              + "# twice the battery for a simulation that still steps 60 times a second.\n"
              + "fps_limit = 60\n"
              + "\n"
              + "# Fifo blocks and lets the GPU idle between frames. On a battery that is the point.\n"
              + "vsync = 1\n"
              + "\n"
              + "# Left at their defaults, and here only so they are easy to find. Internal render\n"
              + "# resolution follows the window, which on a phone is the panel itself; multisampling\n"
              + "# is off; anisotropic filtering is 16x. On a device that runs warm, res_scale is the\n"
              + "# one worth turning down first.\n"
              + "#res_scale = 1.5\n"
              + "#msaa = 4\n"
              + "#aniso = 4\n"
              + "\n"
              + "# The on-screen controls, which exist only on Android. They hide themselves while a\n"
              + "# real controller is connected; 0 here turns them off for good.\n"
              + "#touch = 0\n";
        try (OutputStream out = new FileOutputStream(ini)) {
            out.write(contents.getBytes("UTF-8"));
        } catch (IOException e) {
            Log.w(TAG, "could not write " + ini, e);
        }
    }

    /**
     * A disc image or an extracted disc in one of the public folders. Needs All files access on
     * Android 11 and later, and returns null without it rather than guessing.
     */
    private static String findInPublicFolders() {
        if (!hasAllFilesAccess()) {
            return null;
        }
        File shared = Environment.getExternalStorageDirectory();
        for (String folder : PUBLIC_FOLDERS) {
            File dir = folder.isEmpty() ? shared : new File(shared, folder);
            if (new File(dir, "files/common.ini").exists()) {
                return new File(dir, "files").getAbsolutePath();
            }
            File image = firstDiscImage(dir);
            if (image != null) {
                return image.getAbsolutePath();
            }
        }
        return null;
    }

    private static File firstDiscImage(File dir) {
        File[] entries = dir.listFiles();
        if (entries == null) {
            return null;
        }
        File best = null;
        for (File entry : entries) {
            if (!entry.isFile()) {
                continue;
            }
            String name = entry.getName().toLowerCase(Locale.ROOT);
            if (!name.endsWith(".iso") && !name.endsWith(".gcm")
                    && !name.endsWith(".ciso") && !name.endsWith(".gcz")
                    && !name.endsWith(".rvz")       // strikers-quest: Dolphin's .rvz too
                    && !name.endsWith(".wia")) {    // and (r41) its .wia
                continue;
            }
            // strikers-quest 1.14: only Super Mario Strikers itself (disc code G4Q): the guests' Melee /
            // Colosseum / Charged files may sit beside it, and "Mario Strikers Charged" sorts first.
            if (!GameFile.check(entry).usable) {
                continue;
            }
            // Lowest name wins, the same rule src/platform/dvd.c uses, so the two agree on which of
            // two images in one folder is "the" one.
            if (best == null || entry.getName().compareToIgnoreCase(best.getName()) < 0) {
                best = entry;
            }
        }
        return best;
    }

    static boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            // Before Android 11 the manifest's READ_EXTERNAL_STORAGE is the whole story, and it is
            // granted at install time on the API levels this app supports.
            return true;
        }
        return Environment.isExternalStorageManager();
    }
}
