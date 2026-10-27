package io.github.newcoke.strikers.guests.render;

import io.github.newcoke.strikers.guests.Out;

/**
 * evo_pose.py: a Pokemon's standing idle as joint matrices for the 3D evolution (xr_evo.cpp): its Strikers slot's
 * own "idle" clip (retargetsim.guest_pose, hostref, limbs.txt arms), every key -> guests/evo/<pack>.gev.
 *
 * GEV1, little-endian: "GEV1", u32 nodes, u32 frames, f32 frames a second (30), then frames x nodes x 12 f32: each
 * guest node's world matrix (row vectors; Strikers model space, Z up, facing +X, feet at z 0) as rows 0-2 and the
 * position row. The shiny packs share the skeleton, so one file serves both.
 */
public final class Evo {
    private Evo() {}

    public static byte[] gev(Nl.Host host, Gpk pack, HostRef.Ref ref, double arms) {
        final Retarget rt = new Retarget(host, "idle", pack, ref, arms);
        final int nkeys = rt.keys();
        final int nn = pack.nodes.length;
        final Out o = new Out();
        o.bytes(new byte[] {'G', 'E', 'V', '1'}).le32(nn).le32(nkeys).lef32(30.0f);
        for (int k = 0; k < nkeys; k++) {
            final double t = k == 0 ? 0.0 : k / (nkeys - 1.0);
            final double[][][] w = rt.pose(t);
            for (double[][] m : w)
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 3; c++) o.lef32((float) m[r][c]);
        }
        return o.toBytes();
    }
}
