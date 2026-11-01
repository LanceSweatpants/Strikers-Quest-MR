package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Source;

import java.io.IOException;

/**
 * 1.16: the score bar's banners (Icons, iconres.py's numbers) for a part that brings its own emblem picture (the
 * Marvel heroes: tools/outside/mua/hero_icons.py, their home figure's silhouette in their colour).
 */
public final class Banners {
    private final Icons icons;

    /** From the Strikers disc's CaptainIconsUI.Res. */
    public Banners(Source strikers) throws IOException {
        icons = new Icons(Res.records(strikers.read(Pics.ICONS_RES)));
    }

    /** {<pack>.gtx, <pack>_r.gtx} for an emblem (RGBA, ew x eh: its alpha is the shape), fill / outline colours. */
    public byte[][] make(byte[] emblem, int ew, int eh, int[] fill, int[] line) throws IOException {
        final int[] img = icons.compose(emblem, ew, eh, fill, line);
        return new byte[][] {icons.record(img, false), icons.record(img, true)};
    }
}
