package io.github.newcoke.strikers.guests.audio;

import io.github.newcoke.strikers.guests.Job;
import io.github.newcoke.strikers.guests.Out;
import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * strikers-quest 1.14: every guest's sound, made on the headset from the player's own discs (what the PC tools
 * melee_voices.py / charged_voices.py / voicepack.py / colo_voices_melee.py / gvo_edit.py / hps2evm.py made):
 *
 * <ul>
 *   <li>guests/voices/&lt;pack&gt;.gvo (GVO1, xr_guest_voice.cpp bank_for): "GVO1", u32 count, then per clip
 *       char label[24], u32 rate, u32 samples, s16 mono. Which sound of which disc file each clip is comes from our
 *       list, guestdata/audio/voices.txt (numbers and names only, no sound): only those sounds are decoded.</li>
 *   <li>guests/evo/music_build.evm + music_fanfare.evm (EVM1, xr_guest_voice.cpp evo_music): Melee's own music,
 *       cut and faded as guestdata/audio/evo.txt says.</li>
 * </ul>
 *
 * A guest whose game the player doesn't have is skipped. A Colosseum Pokemon that speaks with a Melee guest's voice
 * gets an empty (silent) bank when Melee is missing: the game mutes the host's voice for every guest, so silent is
 * what a missing bank would do too, and an empty file also replaces any older voice file left there.
 */
public final class Audio {
    private Audio() {}

    public static final String VOICES = "audio/voices.txt", EVO = "audio/evo.txt";

    private static final byte[] MAGIC = {'G', 'V', 'O', '1'};

    /** One clip of our list. */
    static final class Clip {
        String label, file;
        int index, rate, samples;
        boolean silence;
    }

    /** One guests/voices/&lt;name&gt;.gvo. */
    static final class Pack {
        String name, game, same;
        boolean empty;
        final List<Clip> clips = new ArrayList<>();
    }

    static final class Music {
        String out, track;
        double start, length, fadeIn, fadeOut;
    }

    public static void run(Job job) {
        List<Pack> packs;
        List<Music> music;
        try {
            packs = packs(job.data.text(VOICES));
            music = music(job.data.text(EVO));
        } catch (IOException | RuntimeException e) {
            job.progress.warn("Sounds: our sound list is missing or broken (" + e.getMessage() + ")");
            return;
        }
        // what this player's games make
        List<Pack> todo = new ArrayList<>();
        Set<String> wanted = new HashSet<>();  // packs another one is the same as
        for (Pack p : packs) {
            if (source(job, p.game) == null) continue;
            todo.add(p);
            if (p.same != null) wanted.add(p.same);
        }
        final boolean melee = job.melee != null;
        final int total = todo.size() + (melee ? music.size() : 0);
        int done = 0;
        Files files = new Files();
        Map<String, byte[]> made = new HashMap<>();
        // the packs of their own first, then the ones that are another's (a Colosseum Pokemon with a Melee voice)
        for (int pass = 0; pass < 2; pass++) {
            for (Pack p : todo) {
                if ((p.same != null) != (pass == 1)) continue;
                job.progress.step(gameName(p.game) + ": " + p.name + "'s voice", done++, total);
                try {
                    byte[] gvo;
                    if (p.same != null) {
                        gvo = made.get(p.same);
                        if (gvo == null) {
                            if (melee && hasPack(packs, p.same))
                                job.progress.warn(p.name + ": no " + p.same + " voice to share; silent");
                            gvo = gvo(new ArrayList<Clip>(), new ArrayList<short[]>());
                        }
                    } else if (p.empty) {
                        gvo = gvo(new ArrayList<Clip>(), new ArrayList<short[]>());
                    } else {
                        gvo = pack(job, p, files);
                        if (gvo == null) continue;
                    }
                    Out.save(job.out("voices/" + p.name + ".gvo"), gvo);
                    if (wanted.contains(p.name)) made.put(p.name, gvo);
                } catch (IOException | RuntimeException e) {
                    job.progress.warn(p.name + "'s voice: " + e.getMessage());
                }
            }
        }
        files.clear();
        if (melee) {
            for (Music m : music) {
                job.progress.step("Melee: evolution music (" + m.out + ")", done++, total);
                try {
                    byte[] hps = job.melee.read(m.track);
                    Out.save(job.out("evo/" + m.out), Hps.evm(hps, m.start, m.length, m.fadeIn, m.fadeOut));
                } catch (IOException | RuntimeException e) {
                    job.progress.warn("evolution music " + m.out + ": " + e.getMessage());
                }
            }
        }
        job.progress.step("Sounds done", total, total);
    }

    private static boolean hasPack(List<Pack> packs, String name) {
        for (Pack p : packs)
            if (p.name.equals(name)) return true;
        return false;
    }

    static Source source(Job job, String game) {
        switch (game) {
            case "melee":
                return job.melee;
            case "charged":
                return job.charged;
            case "colo":
                return job.colo;
            default:
                return null;
        }
    }

    private static String gameName(String game) {
        switch (game) {
            case "melee":
                return "Melee";
            case "charged":
                return "Charged";
            case "colo":
                return "Colosseum";
            default:
                return game;
        }
    }

    /** A pack's clips from the disc; null when none could be made (nothing written, as the PC tool). */
    static byte[] pack(Job job, Pack p, Files files) throws IOException {
        final Source src = source(job, p.game);
        List<Clip> ok = new ArrayList<>();
        List<short[]> pcm = new ArrayList<>();
        for (Clip c : p.clips) {
            try {
                short[] s;
                int rate;
                if (p.game.equals("melee")) {
                    Ssm ssm = files.ssm(src, c.file);
                    rate = ssm.rate(c.index);
                    s = c.silence ? null : mono(ssm.decode(c.index));
                } else if (p.game.equals("charged")) {
                    Nlx bank = files.nlx(src, c.file + ".resbun");
                    rate = bank.rate(c.index);
                    s = c.silence ? null : bank.decode(c.index, files.raw(src, c.file + ".nlxwb"));
                } else {
                    throw new IOException("no clips from " + p.game);
                }
                if (rate != c.rate || (s != null && s.length != c.samples)) {
                    // not the disc our list was made from: a clip that may be another sound is left out
                    job.progress.warn(p.name + ": " + c.label + " (" + c.file + " #" + c.index + ") is " + rate + " Hz "
                            + (s != null ? s.length : c.samples) + " samples, not " + c.rate + " Hz " + c.samples
                            + "; left out");
                    continue;
                }
                if (s == null) s = new short[Math.max(1, rate / 100)];  // r88 gvo_edit silence: 10 ms
                ok.add(c);
                pcm.add(s);
            } catch (IOException | RuntimeException e) {
                job.progress.warn(p.name + ": " + c.label + " (" + c.file + " #" + c.index + "): " + e.getMessage());
            }
        }
        return ok.isEmpty() ? null : gvo(ok, pcm);
    }

    /** voicex write_wav + voicepack read_wav: channels cut to the shortest, stereo = (L + R) // 2. */
    static short[] mono(short[][] ch) throws IOException {
        if (ch.length == 1) return ch[0];
        if (ch.length != 2) throw new IOException(ch.length + " channels");
        final int n = Math.min(ch[0].length, ch[1].length);
        short[] m = new short[n];
        for (int i = 0; i < n; i++) m[i] = (short) ((ch[0][i] + ch[1][i]) >> 1);
        return m;
    }

    static byte[] gvo(List<Clip> clips, List<short[]> pcm) {
        Out o = new Out();
        o.bytes(MAGIC).le32(clips.size());
        for (int i = 0; i < clips.size(); i++) {
            final Clip c = clips.get(i);
            final short[] s = pcm.get(i);
            o.fixed(c.label.length() > 23 ? c.label.substring(0, 23) : c.label, 24).le32(c.rate).le32(s.length);
            for (short v : s) o.le16(v);
        }
        return o.toBytes();
    }

    // ------------------------------------------------------------------ our lists

    /** guestdata/audio/voices.txt (see its head). */
    static List<Pack> packs(String text) throws IOException {
        List<Pack> out = new ArrayList<>();
        Pack p = null;
        int ln = 0;
        for (String line : text.split("\n")) {
            ln++;
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] w = line.split("\\s+");
            if (w[0].equals("pack")) {
                if (w.length != 3) throw new IOException(VOICES + ":" + ln + ": pack <name> <game>");
                p = new Pack();
                p.name = w[1];
                p.game = w[2].toLowerCase(Locale.ROOT);
                out.add(p);
            } else if (p == null) {
                throw new IOException(VOICES + ":" + ln + ": before any pack");
            } else if (w[0].equals("empty") && w.length == 1) {
                p.empty = true;
            } else if (w[0].equals("same") && w.length == 2) {
                p.same = w[1];
            } else if (w.length == 5 || (w.length == 6 && w[5].equals("silence"))) {
                Clip c = new Clip();
                c.label = w[0];
                c.file = w[1];
                try {
                    c.index = Integer.parseInt(w[2]);
                    c.rate = Integer.parseInt(w[3]);
                    c.samples = Integer.parseInt(w[4]);
                } catch (NumberFormatException e) {
                    throw new IOException(VOICES + ":" + ln + ": " + e.getMessage());
                }
                c.silence = w.length == 6;
                p.clips.add(c);
            } else {
                throw new IOException(VOICES + ":" + ln + ": can't read \"" + line + "\"");
            }
        }
        return out;
    }

    /** guestdata/audio/evo.txt: &lt;out&gt; &lt;track&gt; &lt;start s&gt; &lt;length s | end&gt; &lt;fade in s&gt; &lt;fade out s&gt;. */
    static List<Music> music(String text) throws IOException {
        List<Music> out = new ArrayList<>();
        int ln = 0;
        for (String line : text.split("\n")) {
            ln++;
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] w = line.split("\\s+");
            if (w.length != 6) throw new IOException(EVO + ":" + ln + ": can't read \"" + line + "\"");
            Music m = new Music();
            m.out = w[0];
            m.track = w[1];
            try {
                m.start = Double.parseDouble(w[2]);
                m.length = w[3].equals("end") ? -1 : Double.parseDouble(w[3]);
                m.fadeIn = Double.parseDouble(w[4]);
                m.fadeOut = Double.parseDouble(w[5]);
            } catch (NumberFormatException e) {
                throw new IOException(EVO + ":" + ln + ": " + e.getMessage());
            }
            out.add(m);
        }
        return out;
    }

    // ------------------------------------------------------------------ disc files, kept while they're used

    /** The disc files read so far (most recent kept, up to ~32 MB), so a bank many guests share is read once. */
    static final class Files {
        private static final long BUDGET = 32L << 20;
        private final LinkedHashMap<String, Object[]> kept = new LinkedHashMap<>(16, 0.75f, true);
        private long bytes;

        Ssm ssm(Source s, String path) throws IOException {
            Object[] e = get(s, path);
            if (e[1] == null) e[1] = new Ssm((byte[]) e[0]);
            return (Ssm) e[1];
        }

        Nlx nlx(Source s, String path) throws IOException {
            Object[] e = get(s, path);
            if (e[1] == null) e[1] = new Nlx((byte[]) e[0]);
            return (Nlx) e[1];
        }

        byte[] raw(Source s, String path) throws IOException {
            return (byte[]) get(s, path)[0];
        }

        private Object[] get(Source s, String path) throws IOException {
            final String key = s.id() + "/" + path;
            Object[] e = kept.get(key);
            if (e != null) return e;
            byte[] b = s.read(path);
            e = new Object[] {b, null};
            kept.put(key, e);
            bytes += b.length;
            for (Iterator<Map.Entry<String, Object[]>> it = kept.entrySet().iterator(); bytes > BUDGET && it.hasNext(); ) {
                Map.Entry<String, Object[]> old = it.next();
                if (old.getValue() == e) continue;
                bytes -= ((byte[]) old.getValue()[0]).length;
                it.remove();
            }
            return e;
        }

        void clear() {
            kept.clear();
            bytes = 0;
        }
    }
}
