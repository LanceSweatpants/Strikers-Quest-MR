package io.github.newcoke.strikers.guests;

import java.io.IOException;

/**
 * strikers-quest 1.14 (Lance: guests made on the headset from the player's own game files): one game's files, by
 * their path on its disc ("PlCaNr.dat", "audio/us/mario.ssm", "Art/characters/boo/boo.rlg"). On the headset the
 * game's own disc reader serves them (DiscSource); on the PC a folder of files pulled from the same disc
 * (DirSource), so the converter can be checked against the files the PC tools made.
 *
 * Everything in this package except DiscSource and GuestConvert's launcher half is plain Java (no Android), so the
 * PC runs the very same code (tools/guestconv).
 */
public interface Source {
    /** The game's ID ("GALE01" Melee, "GC6E01" Colosseum, "R4QE01" Charged). */
    String id();

    /** True when the disc has this file. */
    boolean has(String path);

    /** The whole file. */
    byte[] read(String path) throws IOException;

    /**
     * len bytes of a file from off (MUA's 600 MB z/assetsfb.wad is read a piece at a time). This default reads the
     * whole file; DiscSource and DirSource read just the piece.
     */
    default byte[] read(String path, long off, int len) throws IOException {
        final byte[] all = read(path);
        if (off < 0 || off + len > all.length) throw new IOException(id() + ": " + path + ": read past its end");
        return java.util.Arrays.copyOfRange(all, (int) off, (int) off + len);
    }

    /** The file's size in bytes. */
    default long size(String path) throws IOException {
        return read(path).length;
    }
}
