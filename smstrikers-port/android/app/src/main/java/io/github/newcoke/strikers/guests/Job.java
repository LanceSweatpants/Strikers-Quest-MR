package io.github.newcoke.strikers.guests;

import java.io.File;

/**
 * One run of the converter: the player's games (null when they don't have it), our shipped data, where the
 * guest files go (STRIKERS_HOME/guests), and the progress line the launcher shows.
 */
public final class Job {
    public interface Progress {
        /** what: a short line for the player ("Melee: Fox"); done / total: the whole run's steps. */
        void step(String what, int done, int total);

        /** Something one guest couldn't do (it's left out; the rest carry on). */
        void warn(String what);
    }

    public final Source strikers, melee, colo, charged;
    public final Data data;
    public final File guests;
    public final Progress progress;

    public Job(Source strikers, Source melee, Source colo, Source charged, Data data, File guests, Progress progress) {
        this.strikers = strikers;
        this.melee = melee;
        this.colo = colo;
        this.charged = charged;
        this.data = data;
        this.guests = guests;
        this.progress = progress;
    }

    public File out(String rel) {
        return new File(guests, rel);
    }
}
