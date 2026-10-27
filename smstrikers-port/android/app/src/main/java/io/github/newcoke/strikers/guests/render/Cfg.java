package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Data;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Our shipped numbers for the renderer (assets/guestdata/render): the guests, arm sizes, picture settings. */
final class Cfg {
    private Cfg() {}

    static final class Guest {
        String pack, game, pose, portrait;
        int cell;  // -1: none
    }

    static List<Guest> guests(Data data) throws IOException {
        final List<Guest> out = new ArrayList<>();
        for (String line : data.text("render/guests.txt").split("\n")) {
            if (line.startsWith("#") || line.trim().isEmpty()) continue;
            final String[] p = line.trim().split("\\s+");
            if (p.length < 5) continue;
            final Guest g = new Guest();
            g.pack = p[0];
            g.game = p[1];
            g.pose = p[2];
            g.cell = p[3].equals("-") ? -1 : Integer.parseInt(p[3]);
            g.portrait = p[4];
            out.add(g);
        }
        return out;
    }

    /** retargetsim.read_limbs: {pack: arm scale} from "<pack> arms <scale>" lines. */
    static Map<String, Double> limbs(String text) {
        final Map<String, Double> out = new HashMap<>();
        if (text == null) return out;
        for (String line : text.split("\n")) {
            final String[] p = line.trim().split("\\s+");
            if (p.length >= 3 && !p[0].startsWith("#") && p[1].equals("arms")) out.put(p[0], Double.parseDouble(p[2]));
        }
        return out;
    }

    /** "key value..." lines (# comments) -> key -> numbers. */
    static Map<String, double[]> numbers(String text) {
        final Map<String, double[]> out = new HashMap<>();
        for (String line : text.split("\n")) {
            final int hash = line.indexOf('#');
            final String s = (hash >= 0 ? line.substring(0, hash) : line).trim();
            if (s.isEmpty()) continue;
            final String[] p = s.split("\\s+");
            final double[] v = new double[p.length - 1];
            for (int i = 1; i < p.length; i++) v[i - 1] = Double.parseDouble(p[i]);
            out.put(p[0], v);
        }
        return out;
    }
}
