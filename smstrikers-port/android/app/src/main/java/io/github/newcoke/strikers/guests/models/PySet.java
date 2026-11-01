package io.github.newcoke.strikers.guests.models;

/**
 * CPython's set of small non-negative ints (hash = the int), only added to: set_add_entry with its resize rule and
 * set_merge (s |= other, one resize up front), so a set's iteration order (its slot order) is Python's - the PC tools
 * sum rows of a mesh's neighbour sets in that order (vaprepose.smooth_weights / relax: Wd[list(nb[i])].sum(0)).
 */
final class PySet {
    private static final int MINSIZE = 8, LINEAR_PROBES = 9, PERTURB_SHIFT = 5;
    private int[] keys = new int[MINSIZE];
    private boolean[] full = new boolean[MINSIZE];
    private int mask = MINSIZE - 1, used;

    int size() {
        return used;
    }

    boolean isEmpty() {
        return used == 0;
    }

    /** {a, b} (BUILD_SET: added in order). */
    static PySet of(int a, int b) {
        final PySet s = new PySet();
        s.add(a);
        s.add(b);
        return s;
    }

    boolean contains(int key) {
        int i = key & mask;
        long perturb = key;
        while (true) {
            final int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
            int e = i;
            for (int k = 0; k <= probes; k++, e++) {
                if (!full[e]) return false;
                if (keys[e] == key) return true;
            }
            perturb >>>= PERTURB_SHIFT;
            i = (int) ((i * 5L + 1 + perturb) & mask);
        }
    }

    /** set_add_entry */
    void add(int key) {
        int i = key & mask;
        long perturb = key;
        while (true) {
            final int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
            int e = i;
            for (int k = 0; k <= probes; k++, e++) {
                if (!full[e]) {
                    full[e] = true;
                    keys[e] = key;
                    used++;
                    if ((long) used * 5 >= (long) mask * 3) resize(used > 50000 ? used * 2 : used * 4);
                    return;
                }
                if (keys[e] == key) return;
            }
            perturb >>>= PERTURB_SHIFT;
            i = (int) ((i * 5L + 1 + perturb) & mask);
        }
    }

    /** set_merge: this |= other. */
    void merge(PySet other) {
        if (other.used == 0) return;
        if ((long) (used + other.used) * 5 >= (long) mask * 3) resize((used + other.used) * 2);
        if (used == 0 && mask == other.mask) {  // (no dummies: fill == used) copy the table as it is
            System.arraycopy(other.keys, 0, keys, 0, keys.length);
            System.arraycopy(other.full, 0, full, 0, full.length);
            used = other.used;
            return;
        }
        if (used == 0) {
            for (int s = 0; s <= other.mask; s++) if (other.full[s]) insertClean(keys, full, mask, other.keys[s]);
            used = other.used;
            return;
        }
        for (int s = 0; s <= other.mask; s++) if (other.full[s]) add(other.keys[s]);
    }

    private static void insertClean(int[] keys, boolean[] full, int mask, int key) {
        int i = key & mask;
        long perturb = key;
        while (true) {
            if (!full[i]) {
                full[i] = true;
                keys[i] = key;
                return;
            }
            if (i + LINEAR_PROBES <= mask) {
                for (int j = 1; j <= LINEAR_PROBES; j++) {
                    if (!full[i + j]) {
                        full[i + j] = true;
                        keys[i + j] = key;
                        return;
                    }
                }
            }
            perturb >>>= PERTURB_SHIFT;
            i = (int) ((i * 5L + 1 + perturb) & mask);
        }
    }

    /** set_table_resize */
    private void resize(int minused) {
        int size = MINSIZE;
        while (size <= minused) size <<= 1;
        final int[] ok = keys;
        final boolean[] of = full;
        keys = new int[size];
        full = new boolean[size];
        mask = size - 1;
        for (int s = 0; s < ok.length; s++) if (of[s]) insertClean(keys, full, mask, ok[s]);
    }

    /** The members in Python's iteration order. */
    int[] toArray() {
        final int[] out = new int[used];
        int n = 0;
        for (int s = 0; s <= mask; s++) if (full[s]) out[n++] = keys[s];
        return out;
    }
}
