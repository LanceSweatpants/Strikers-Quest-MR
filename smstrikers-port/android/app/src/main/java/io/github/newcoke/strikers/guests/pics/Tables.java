package io.github.newcoke.strikers.guests.pics;

import io.github.newcoke.strikers.guests.Data;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Our own tables for the pictures (assets/guestdata/pics/pics.txt): which game picture is whose, series, colours. */
final class Tables {
    /** A Melee picture: file, TexAnim offset (-1: none) and frame, or (no TexAnim) an image descriptor offset. */
    static final class MeleePic {
        final String file;
        final int texAnim, frame;

        MeleePic(String file, int texAnim, int frame) {
            this.file = file;
            this.texAnim = texAnim;
            this.frame = frame;
        }
    }

    /** A Strikers picture: bundle path and the entry's name hash. */
    static final class StrikersPic {
        final String bundle;
        final int hash;

        StrikersPic(String bundle, int hash) {
            this.bundle = bundle;
            this.hash = hash;
        }
    }

    final Map<String, MeleePic> melee = new LinkedHashMap<>();       // pack -> its portrait
    final Map<String, StrikersPic> strikers = new LinkedHashMap<>(); // name -> strikers_<name>.png
    final Map<String, Integer> emblem = new HashMap<>();             // series -> IfAll image descriptor
    final Map<String, int[]> colour = new HashMap<>();               // series -> fill rgb, outline rgb
    final Map<String, String> series = new HashMap<>();              // pack -> series

    static Tables load(Data data) throws IOException {
        Tables t = new Tables();
        String cspFile = null;
        int cspTa = -1;
        List<String> order = new ArrayList<>();
        Map<String, String> alias = new LinkedHashMap<>();
        for (String line : data.text("pics/pics.txt").split("\n")) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\\s+");
            switch (p[0]) {
                case "csp":
                    cspFile = p[1];
                    cspTa = Integer.parseInt(p[2], 16);
                    break;
                case "order":
                    for (int i = 1; i < p.length; i++) order.add(p[i]);
                    break;
                case "alias":
                    alias.put(p[1], p[2]);
                    break;
                case "poke":
                    t.melee.put(p[1], p[3].equals("-") ? new MeleePic(p[2], -1, Integer.parseInt(p[4], 16))
                            : new MeleePic(p[2], Integer.parseInt(p[3], 16), Integer.parseInt(p[4])));
                    break;
                case "strikers":
                    t.strikers.put(p[1], new StrikersPic(p[2],
                            p[3].startsWith("#") ? (int) Long.parseLong(p[3].substring(1), 16) : Res.nameHash(p[3])));
                    break;
                case "emblem":
                    t.emblem.put(p[1], Integer.parseInt(p[2], 16));
                    break;
                case "colour": {
                    int[] c = new int[6];
                    for (int i = 0; i < 6; i++) c[i] = Integer.parseInt(p[2 + i]);
                    t.colour.put(p[1], c);
                    break;
                }
                case "series":
                    t.series.put(p[1], p[2]);
                    break;
                default:
                    throw new IOException("pics.txt: unknown line " + line);
            }
        }
        if (cspFile == null) throw new IOException("pics.txt: no csp line");
        for (int k = 0; k < order.size(); k++) t.melee.put(order.get(k), new MeleePic(cspFile, cspTa, k));
        for (Map.Entry<String, String> e : alias.entrySet()) t.melee.put(e.getKey(), t.melee.get(e.getValue()));
        return t;
    }
}
