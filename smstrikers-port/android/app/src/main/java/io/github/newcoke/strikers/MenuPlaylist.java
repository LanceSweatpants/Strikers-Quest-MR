package io.github.newcoke.strikers;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Random;

/**
 * strikers-quest r147: what the menu music plays next ({@link MenuMusic} plays it). Plain Java, no Android, so its
 * rules are tested on a PC too (logs/music147: PlaylistTest).
 *
 * <ul>
 *   <li>A shuffle bag: every track once a round, in a new order each round, and never the one that just played (not
 *       even across two rounds) unless there is only one.
 *   <li>Franchise mode entered: its theme first ({@link #franchiseTheme}); it leaves this round's bag so the rest come
 *       before it does again, then the shuffle carries on.
 *   <li>The setting (Lance: LOFI / MY MUSIC / BOTH / OFF): LOFI = the lofi playlist + its theme; MY MUSIC = only his
 *       songs (STRIKERS_HOME/music; the lofi when that folder is empty); BOTH = both; OFF = nothing (the game's own).
 *       The theme: a song of his named theme.* in BOTH / MY MUSIC, else the lofi's ("lofi hip hop loop"); none in MY
 *       MUSIC without his own theme.*. The lofi theme is in the outside-franchise shuffle as well.
 * </ul>
 */
final class MenuPlaylist {
    static final int LOFI = 0, MINE = 1, BOTH = 2, OFF = 3, MODES = 4;
    static final String[] MODE_NAMES = {"Lofi", "My music", "Both", "Off"};

    static final class Track {
        final String source;   // "asset:music/lofi/<name>.ogg" or a file's absolute path
        final String title, artist;
        final boolean mine;    // from STRIKERS_HOME/music

        Track(String source, String title, String artist, boolean mine) {
            this.source = source;
            this.title = title;
            this.artist = artist;
            this.mine = mine;
        }

        @Override public boolean equals(Object o) {
            return o instanceof Track && ((Track) o).source.equals(source);
        }

        @Override public int hashCode() {
            return source.hashCode();
        }

        @Override public String toString() {
            return title;
        }
    }

    private final Random rng;
    private final List<Track> pool = new ArrayList<>();
    private final List<Track> bag = new ArrayList<>();  // this round's rest, drawn from the end
    private Track theme;
    private Track last;
    private boolean fellBack;

    MenuPlaylist(Random rng) {
        this.rng = rng;
    }

    /** The setting and the tracks there are. The round carries on when the tracks to shuffle are the same. */
    void set(int mode, List<Track> lofi, Track lofiTheme, List<Track> mine, Track myTheme) {
        List<Track> p = new ArrayList<>();
        Track th = null;
        fellBack = false;
        if (mode == OFF) {
            // nothing: the game's own menu music
        } else if (mode == MINE && !mine.isEmpty()) {
            p.addAll(mine);
            th = myTheme;
        } else if (mode == BOTH) {
            p.addAll(lofi);
            if (lofiTheme != null) p.add(lofiTheme);
            p.addAll(mine);
            th = myTheme != null ? myTheme : lofiTheme;
        } else {
            fellBack = mode == MINE;  // MY MUSIC with an empty folder: the lofi
            p.addAll(lofi);
            if (lofiTheme != null) p.add(lofiTheme);
            th = lofiTheme;
        }
        theme = th;
        if (!p.equals(pool)) {
            pool.clear();
            pool.addAll(p);
            bag.clear();
        }
    }

    boolean isEmpty() {
        return pool.isEmpty();
    }

    int size() {
        return pool.size();
    }

    boolean contains(Track t) {
        return t != null && pool.contains(t);
    }

    /** MY MUSIC picked with no songs of his: the lofi plays. */
    boolean fellBack() {
        return fellBack;
    }

    Track theme() {
        return theme;
    }

    /** The next track from the bag (a new shuffled round when it is empty); null when there is nothing to play. */
    Track next() {
        if (pool.isEmpty()) return null;
        if (bag.isEmpty()) {
            bag.addAll(pool);
            Collections.shuffle(bag, rng);
            // never the one that just played first in the new round (it is drawn from the end)
            if (last != null && bag.size() > 1 && bag.get(bag.size() - 1).equals(last)) {
                Collections.swap(bag, rng.nextInt(bag.size() - 1), bag.size() - 1);
            }
        }
        last = bag.remove(bag.size() - 1);
        return last;
    }

    /** Franchise mode entered: its theme (null when there is none), out of this round so the rest come first. */
    Track franchiseTheme() {
        if (theme == null) return null;
        bag.remove(theme);
        last = theme;
        return theme;
    }
}
