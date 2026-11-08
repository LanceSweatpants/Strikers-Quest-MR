package io.github.newcoke.strikers;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;

/**
 * strikers-quest r157 (menu overhaul, MENUS-PLAN.md): the launcher panel's look, the same numbers as the mockups
 * (tools/outside/menus_kit.py): the franchise hub's navy + gold, a colour per launcher section, Luckiest Guy (the
 * game's font, an APK asset: assets/fonts, Apache-2.0), one focus style (a thick gold ring, on hover from the
 * controller's ray, keyboard / d-pad focus and press), and the tiles' icons drawn here rather than shipped as art.
 */
final class LobbyKit {
    private LobbyKit() {}

    // menus_kit.py's design tokens
    static final int GOLD = 0xFFFFD23F, CREAM = 0xFFFFF7B0, NAVY_TOP = 0xFF1E2A4E, NAVY_BOT = 0xFF070A14;
    static final int TILE = 0xFF151C36, TILE_HI = 0xFF22305C, TILE_ON = 0xFF3B5AA8, PILL = 0xFF26335E;
    static final int INK_SOFT = 0xFFD2DAEB, INK_DIM = 0xFF8A94AA, SLATE = 0xFF3A4A70;
    static final int CHECK = 0xFF2EA043, B_RED = 0xFFC0392B, WARN = 0xFFFF968C;
    static final int NOTICE_BG = 0xFF4A1A22, NOTICE_EDGE = 0xFFE0605A;
    // a colour per launcher section (menus_kit.py SECTION), lit in the breadcrumb
    static final int PLAY = 0xFF2EA043, ONLINE = 0xFF2F6FD0, GUESTS = 0xFFD9542B, SETTINGS = 0xFF16938C,
            UPDATE = 0xFFC9A227;

    private static Typeface lucky;

    /** Luckiest Guy from the APK's assets; the system bold if it isn't there (an old APK layout). */
    static Typeface font(Context c) {
        if (lucky == null) {
            try {
                lucky = Typeface.createFromAsset(c.getAssets(), "fonts/LuckiestGuy-Regular.ttf");
            } catch (RuntimeException e) {
                lucky = Typeface.DEFAULT_BOLD;
            }
        }
        return lucky;
    }

    /** a toward b by t (0..1), alpha from a. */
    static int mix(int a, int b, float t) {
        t = Math.max(0f, Math.min(1f, t));
        int out = a & 0xFF000000;
        for (int s = 16; s >= 0; s -= 8) {
            int x = (a >> s) & 255, y = (b >> s) & 255;
            out |= Math.round(x + (y - x) * t) << s;
        }
        return out;
    }

    static GradientDrawable round(int colour, float radius) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(colour);
        g.setCornerRadius(radius);
        return g;
    }

    static GradientDrawable fade(int top, int bottom, float radius) {
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] {top, bottom});
        g.setCornerRadius(radius);
        return g;
    }

    /** The panel itself: navy top to near black, a gold edge (menus_kit.py frame). */
    static Drawable frame(float density) {
        GradientDrawable g = fade(NAVY_TOP, NAVY_BOT, 22 * density);
        g.setStroke(Math.round(5 * density), GOLD);
        return g;
    }

    /**
     * Anything pressable: `idle` as it is, and the gold ring over it while the ray is on it (hover), it has d-pad /
     * keyboard focus, or it is pressed (a touch brighter). `stripe` != 0: a band of the section's colour along the top
     * (the tiles).
     */
    static Drawable pressable(Drawable idle, int stripe, float radius, float density) {
        return pressable(idle, stripe, radius, density, GOLD);
    }

    /** pressable() with another ring colour (cream on the gold buttons, where a gold ring wouldn't show). */
    static Drawable pressable(Drawable idle, int stripe, float radius, float density, int ring) {
        StateListDrawable s = new StateListDrawable();
        s.addState(new int[] {android.R.attr.state_enabled, android.R.attr.state_pressed},
                ringed(idle, stripe, radius, density, 0x30FFFFFF, ring));
        s.addState(new int[] {android.R.attr.state_enabled, android.R.attr.state_hovered},
                ringed(idle, stripe, radius, density, 0, ring));
        s.addState(new int[] {android.R.attr.state_enabled, android.R.attr.state_focused},
                ringed(idle, stripe, radius, density, 0, ring));
        s.addState(new int[] {}, striped(idle, stripe, radius, density));
        return s;
    }

    private static Drawable striped(Drawable idle, int stripe, float radius, float density) {
        if (stripe == 0) return idle.getConstantState() != null ? idle.getConstantState().newDrawable().mutate() : idle;
        Drawable base = idle.getConstantState() != null ? idle.getConstantState().newDrawable().mutate() : idle;
        GradientDrawable band = new GradientDrawable();
        band.setColor(stripe);
        band.setCornerRadii(new float[] {radius, radius, radius, radius, 0, 0, 0, 0});
        LayerDrawable l = new LayerDrawable(new Drawable[] {base, band});
        l.setLayerGravity(1, Gravity.TOP | Gravity.FILL_HORIZONTAL);
        l.setLayerHeight(1, Math.round(9 * density));
        return l;
    }

    private static Drawable ringed(Drawable idle, int stripe, float radius, float density, int wash, int edge) {
        Drawable base = striped(idle, stripe, radius, density);
        GradientDrawable ring = new GradientDrawable();
        ring.setColor(wash);
        ring.setCornerRadius(radius);
        ring.setStroke(Math.round(5 * density), edge);
        return new LayerDrawable(new Drawable[] {base, ring});
    }

    // ------------------------------------------------------------------ icons
    static final int ICON_GLOBE = 0, ICON_PEOPLE = 1, ICON_GEAR = 2, ICON_DOWNLOAD = 3, ICON_HOUSE = 4, ICON_CHAT = 5,
            ICON_TROPHY = 6, ICON_BALL = 7, ICON_CHECK = 8, ICON_NOTE = 9, ICON_BACK = 10, ICON_FLOPPY = 11,
            ICON_SWAP = 12, ICON_WARN = 13, ICON_PLAY_ART = 14, ICON_CRUMB = 15;

    /** One flat white icon (menus_kit.py draws the same shapes), any size; `ink` its colour. */
    static final class Icon extends Drawable {
        private final int kind;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final int ink;

        Icon(int kind, int ink) {
            this.kind = kind;
            this.ink = ink;
        }

        @Override
        public void draw(Canvas c) {
            final float w = getBounds().width(), h = getBounds().height();
            final float s = Math.min(w, h), x0 = getBounds().left + (w - s) / 2, y0 = getBounds().top + (h - s) / 2;
            final float cx = x0 + s / 2, cy = y0 + s / 2;
            p.setColor(ink);
            p.setStyle(Paint.Style.FILL);
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeJoin(Paint.Join.ROUND);
            Path path = new Path();
            switch (kind) {
                case ICON_GLOBE: {
                    stroke(s * 0.075f);
                    float r = s * 0.42f;
                    c.drawCircle(cx, cy, r, p);
                    c.drawOval(new RectF(cx - r * 0.45f, cy - r, cx + r * 0.45f, cy + r), p);
                    c.drawLine(cx - r, cy, cx + r, cy, p);
                    c.drawLine(cx - r * 0.86f, cy - r * 0.5f, cx + r * 0.86f, cy - r * 0.5f, p);
                    c.drawLine(cx - r * 0.86f, cy + r * 0.5f, cx + r * 0.86f, cy + r * 0.5f, p);
                    break;
                }
                case ICON_PEOPLE: {
                    p.setAlpha(150);
                    for (float dx : new float[] {-0.25f, 0.25f}) {
                        c.drawCircle(cx + dx * s, cy - s * 0.2f, s * 0.1f, p);
                        c.drawRoundRect(new RectF(cx + dx * s - s * 0.17f, cy - s * 0.05f, cx + dx * s + s * 0.17f,
                                cy + s * 0.25f), s * 0.1f, s * 0.1f, p);
                    }
                    p.setAlpha(255);
                    c.drawCircle(cx, cy - s * 0.13f, s * 0.13f, p);
                    c.drawRoundRect(new RectF(cx - s * 0.21f, cy + s * 0.04f, cx + s * 0.21f, cy + s * 0.38f),
                            s * 0.14f, s * 0.14f, p);
                    break;
                }
                case ICON_GEAR: {
                    for (int i = 0; i < 8; i++) {
                        c.save();
                        c.rotate(i * 45f, cx, cy);
                        c.drawRect(cx - s * 0.08f, cy - s * 0.46f, cx + s * 0.08f, cy - s * 0.25f, p);
                        c.restore();
                    }
                    stroke(s * 0.17f);
                    c.drawCircle(cx, cy, s * 0.24f, p);
                    break;
                }
                case ICON_DOWNLOAD: {
                    c.drawRect(cx - s * 0.06f, y0 + s * 0.08f, cx + s * 0.06f, cy + s * 0.05f, p);
                    path.moveTo(cx - s * 0.26f, cy);
                    path.lineTo(cx + s * 0.26f, cy);
                    path.lineTo(cx, cy + s * 0.3f);
                    path.close();
                    c.drawPath(path, p);
                    c.drawRoundRect(new RectF(cx - s * 0.36f, y0 + s * 0.86f, cx + s * 0.36f, y0 + s * 0.94f),
                            s * 0.04f, s * 0.04f, p);
                    break;
                }
                case ICON_HOUSE: {
                    path.moveTo(cx, y0 + s * 0.1f);
                    path.lineTo(cx + s * 0.44f, cy);
                    path.lineTo(cx - s * 0.44f, cy);
                    path.close();
                    c.drawPath(path, p);
                    c.drawRect(cx - s * 0.32f, cy - s * 0.02f, cx + s * 0.32f, y0 + s * 0.88f, p);
                    p.setColor(0xFF2A3350);
                    c.drawRect(cx - s * 0.08f, cy + s * 0.12f, cx + s * 0.08f, y0 + s * 0.88f, p);
                    break;
                }
                case ICON_CHAT: {
                    c.drawRoundRect(new RectF(x0 + s * 0.08f, y0 + s * 0.16f, x0 + s * 0.92f, y0 + s * 0.66f),
                            s * 0.14f, s * 0.14f, p);
                    path.moveTo(x0 + s * 0.3f, y0 + s * 0.6f);
                    path.lineTo(x0 + s * 0.5f, y0 + s * 0.6f);
                    path.lineTo(x0 + s * 0.24f, y0 + s * 0.88f);
                    path.close();
                    c.drawPath(path, p);
                    break;
                }
                case ICON_TROPHY: {
                    c.drawRect(cx - s * 0.26f, y0 + s * 0.1f, cx + s * 0.26f, y0 + s * 0.18f, p);
                    path.moveTo(cx - s * 0.26f, y0 + s * 0.16f);
                    path.lineTo(cx + s * 0.26f, y0 + s * 0.16f);
                    path.lineTo(cx + s * 0.18f, y0 + s * 0.5f);
                    path.lineTo(cx - s * 0.18f, y0 + s * 0.5f);
                    path.close();
                    c.drawPath(path, p);
                    c.drawRect(cx - s * 0.05f, y0 + s * 0.48f, cx + s * 0.05f, y0 + s * 0.72f, p);
                    c.drawRoundRect(new RectF(cx - s * 0.2f, y0 + s * 0.72f, cx + s * 0.2f, y0 + s * 0.84f),
                            s * 0.03f, s * 0.03f, p);
                    stroke(s * 0.06f);
                    c.drawCircle(cx - s * 0.3f, y0 + s * 0.27f, s * 0.08f, p);
                    c.drawCircle(cx + s * 0.3f, y0 + s * 0.27f, s * 0.08f, p);
                    break;
                }
                case ICON_BALL: {
                    c.drawCircle(cx, cy, s * 0.44f, p);
                    p.setColor(0xFF151C36);
                    float r = s * 0.15f;
                    for (int i = 0; i < 5; i++) {
                        double a = Math.toRadians(-90 + 72 * i);
                        float px = cx + (float) Math.cos(a) * r, py = cy + (float) Math.sin(a) * r;
                        if (i == 0) path.moveTo(px, py); else path.lineTo(px, py);
                    }
                    path.close();
                    c.drawPath(path, p);
                    stroke(s * 0.04f);
                    p.setColor(0xFF151C36);
                    for (int i = 0; i < 5; i++) {
                        double a = Math.toRadians(-90 + 72 * i);
                        c.drawLine(cx + (float) Math.cos(a) * r, cy + (float) Math.sin(a) * r,
                                cx + (float) Math.cos(a) * s * 0.42f, cy + (float) Math.sin(a) * s * 0.42f, p);
                    }
                    break;
                }
                case ICON_CHECK: {
                    p.setColor(CHECK);
                    c.drawCircle(cx, cy, s * 0.46f, p);
                    p.setColor(0xFFFFFFFF);
                    stroke(s * 0.11f);
                    path.moveTo(cx - s * 0.22f, cy + s * 0.01f);
                    path.lineTo(cx - s * 0.06f, cy + s * 0.17f);
                    path.lineTo(cx + s * 0.24f, cy - s * 0.17f);
                    c.drawPath(path, p);
                    break;
                }
                case ICON_NOTE: {
                    c.drawCircle(cx - s * 0.22f, y0 + s * 0.76f, s * 0.12f, p);
                    c.drawCircle(cx + s * 0.22f, y0 + s * 0.68f, s * 0.12f, p);
                    c.drawRect(cx - s * 0.14f, y0 + s * 0.2f, cx - s * 0.06f, y0 + s * 0.76f, p);
                    c.drawRect(cx + s * 0.3f, y0 + s * 0.12f, cx + s * 0.38f, y0 + s * 0.68f, p);
                    path.moveTo(cx - s * 0.14f, y0 + s * 0.2f);
                    path.lineTo(cx + s * 0.38f, y0 + s * 0.1f);
                    path.lineTo(cx + s * 0.38f, y0 + s * 0.24f);
                    path.lineTo(cx - s * 0.14f, y0 + s * 0.34f);
                    path.close();
                    c.drawPath(path, p);
                    break;
                }
                case ICON_BACK: {
                    path.moveTo(x0 + s * 0.24f, cy);
                    path.lineTo(x0 + s * 0.72f, y0 + s * 0.16f);
                    path.lineTo(x0 + s * 0.72f, y0 + s * 0.84f);
                    path.close();
                    c.drawPath(path, p);
                    break;
                }
                case ICON_FLOPPY: {
                    p.setColor(GOLD);
                    c.drawRoundRect(new RectF(x0 + s * 0.1f, y0 + s * 0.1f, x0 + s * 0.9f, y0 + s * 0.9f),
                            s * 0.06f, s * 0.06f, p);
                    p.setColor(0xFF3A3424);
                    c.drawRect(x0 + s * 0.3f, y0 + s * 0.1f, x0 + s * 0.7f, y0 + s * 0.36f, p);
                    p.setColor(0xFF8A7A3A);
                    c.drawRect(x0 + s * 0.24f, y0 + s * 0.52f, x0 + s * 0.76f, y0 + s * 0.9f, p);
                    break;
                }
                case ICON_SWAP: {
                    stroke(s * 0.08f);
                    c.drawLine(x0 + s * 0.16f, y0 + s * 0.34f, x0 + s * 0.8f, y0 + s * 0.34f, p);
                    c.drawLine(x0 + s * 0.84f, y0 + s * 0.66f, x0 + s * 0.2f, y0 + s * 0.66f, p);
                    p.setStyle(Paint.Style.FILL);
                    path.moveTo(x0 + s * 0.9f, y0 + s * 0.34f);
                    path.lineTo(x0 + s * 0.7f, y0 + s * 0.2f);
                    path.lineTo(x0 + s * 0.7f, y0 + s * 0.48f);
                    path.close();
                    path.moveTo(x0 + s * 0.1f, y0 + s * 0.66f);
                    path.lineTo(x0 + s * 0.3f, y0 + s * 0.52f);
                    path.lineTo(x0 + s * 0.3f, y0 + s * 0.8f);
                    path.close();
                    c.drawPath(path, p);
                    break;
                }
                case ICON_WARN: {
                    c.drawRoundRect(new RectF(cx - s * 0.08f, y0 + s * 0.1f, cx + s * 0.08f, y0 + s * 0.64f),
                            s * 0.06f, s * 0.06f, p);
                    c.drawCircle(cx, y0 + s * 0.82f, s * 0.09f, p);
                    break;
                }
                case ICON_CRUMB: {  // r157 round 2: the breadcrumb's gold arrow, drawn to the view's own box
                    final float l = getBounds().left, t = getBounds().top;
                    path.moveTo(l + w * 0.15f, t + h * 0.12f);
                    path.lineTo(l + w * 0.9f, t + h * 0.5f);
                    path.lineTo(l + w * 0.15f, t + h * 0.88f);
                    path.close();
                    c.drawPath(path, p);
                    break;
                }
                case ICON_PLAY_ART:
                    playArt(c, x0, y0, s);
                    break;
                default:
                    break;
            }
        }

        /**
         * r157 round 2: HOME's PLAY art, our own (no game art ships): a ball bursting into the net, gold rays behind,
         * speed streaks and sparkles, in the navy + gold look. launcher_preview.py draws the same numbers.
         */
        private void playArt(Canvas c, float x0, float y0, float s) {
            final float cx = x0 + s / 2, cy = y0 + s / 2;
            p.setStyle(Paint.Style.FILL);
            p.setShader(null);
            // the burst: 16 rays fading out, gold and cream in turn
            for (int i = 0; i < 16; i++) {
                final int col = (i % 2 == 0) ? GOLD : CREAM;
                p.setShader(new android.graphics.RadialGradient(cx, cy, s * 0.5f,
                        new int[] {(col & 0x00FFFFFF) | ((i % 2 == 0 ? 0xE0 : 0x80) << 24), col & 0x00FFFFFF},
                        null, android.graphics.Shader.TileMode.CLAMP));
                final double a = Math.toRadians(i * 22.5 - 90 + 11.25), h = Math.toRadians(i % 2 == 0 ? 6.5 : 4.0);
                Path ray = new Path();
                ray.moveTo(cx, cy);
                ray.lineTo(cx + (float) Math.cos(a - h) * s * 0.5f, cy + (float) Math.sin(a - h) * s * 0.5f);
                ray.lineTo(cx + (float) Math.cos(a + h) * s * 0.5f, cy + (float) Math.sin(a + h) * s * 0.5f);
                ray.close();
                c.drawPath(ray, p);
            }
            // a soft glow behind the ball
            p.setShader(new android.graphics.RadialGradient(cx, cy, s * 0.34f, new int[] {0x80FFFFFF, 0x00FFFFFF},
                    null, android.graphics.Shader.TileMode.CLAMP));
            c.drawCircle(cx, cy, s * 0.34f, p);
            p.setShader(null);
            // the goal: net then frame
            final float gl = cx - s * 0.40f, gr = cx + s * 0.40f, gt = cy - s * 0.20f, gb = cy + s * 0.30f;
            stroke(s * 0.008f);
            p.setColor(0x70FFFFFF);
            for (float x = gl + s * 0.08f; x < gr - s * 0.01f; x += s * 0.08f) c.drawLine(x, gt, x, gb, p);
            for (float y = gt + s * 0.08f; y < gb - s * 0.01f; y += s * 0.08f) c.drawLine(gl, y, gr, y, p);
            stroke(s * 0.04f);
            p.setColor(0xFFFFFFFF);
            Path frame = new Path();
            frame.moveTo(gl, gb);
            frame.lineTo(gl, gt);
            frame.lineTo(gr, gt);
            frame.lineTo(gr, gb);
            c.drawPath(frame, p);
            // speed streaks
            final float bx = cx + s * 0.05f, by = cy + s * 0.06f, r = s * 0.23f;
            stroke(s * 0.035f);
            p.setColor(CREAM);
            c.drawLine(cx - s * 0.44f, by - s * 0.11f, cx - s * 0.26f, by - s * 0.11f, p);
            c.drawLine(cx - s * 0.48f, by, cx - s * 0.22f, by, p);
            c.drawLine(cx - s * 0.42f, by + s * 0.11f, cx - s * 0.25f, by + s * 0.11f, p);
            // its shadow on the grass
            p.setStyle(Paint.Style.FILL);
            p.setColor(0x60000000);
            c.drawOval(new RectF(bx - s * 0.19f, cy + s * 0.29f, bx + s * 0.19f, cy + s * 0.35f), p);
            // the ball
            p.setShader(new android.graphics.RadialGradient(bx - r * 0.35f, by - r * 0.4f, r * 1.35f,
                    new int[] {0xFFFFFFFF, 0xFFC9D2E6}, null, android.graphics.Shader.TileMode.CLAMP));
            c.drawCircle(bx, by, r, p);
            p.setShader(null);
            c.save();
            Path clip = new Path();
            clip.addCircle(bx, by, r, Path.Direction.CW);
            c.clipPath(clip);
            p.setColor(0xFF1A2244);
            c.drawPath(pentagon(bx, by, r * 0.36f, -90), p);
            for (int k = 0; k < 5; k++) {
                final double d = Math.toRadians(-90 + 36 + 72 * k);
                final float px = bx + (float) Math.cos(d) * r * 0.95f, py = by + (float) Math.sin(d) * r * 0.95f;
                c.drawPath(pentagon(px, py, r * 0.36f, -90 + 36 + 72 * k + 180), p);
            }
            stroke(r * 0.04f);
            for (int k = 0; k < 5; k++) {
                final double d = Math.toRadians(-90 + 72 * k);
                c.drawLine(bx + (float) Math.cos(d) * r * 0.36f, by + (float) Math.sin(d) * r * 0.36f,
                        bx + (float) Math.cos(d) * r * 0.7f, by + (float) Math.sin(d) * r * 0.7f, p);
            }
            c.restore();
            stroke(r * 0.05f);
            p.setColor(0xFF1A2244);
            c.drawCircle(bx, by, r, p);
            p.setStyle(Paint.Style.FILL);
            p.setColor(0x96FFFFFF);
            c.save();
            c.rotate(-30, bx - r * 0.4f, by - r * 0.48f);
            c.drawOval(new RectF(bx - r * 0.66f, by - r * 0.63f, bx - r * 0.14f, by - r * 0.33f), p);
            c.restore();
            // sparkles
            p.setColor(CREAM);
            sparkle(c, cx + s * 0.36f, cy - s * 0.30f, s * 0.05f);
            sparkle(c, cx - s * 0.34f, cy - s * 0.33f, s * 0.035f);
            sparkle(c, cx + s * 0.43f, cy + s * 0.08f, s * 0.03f);
        }

        private static Path pentagon(float x, float y, float r, double startDeg) {
            Path path = new Path();
            for (int i = 0; i < 5; i++) {
                final double a = Math.toRadians(startDeg + 72 * i);
                final float px = x + (float) Math.cos(a) * r, py = y + (float) Math.sin(a) * r;
                if (i == 0) path.moveTo(px, py); else path.lineTo(px, py);
            }
            path.close();
            return path;
        }

        private void sparkle(Canvas c, float x, float y, float r) {
            Path path = new Path();
            path.moveTo(x, y - r);
            path.lineTo(x + r * 0.28f, y - r * 0.28f);
            path.lineTo(x + r, y);
            path.lineTo(x + r * 0.28f, y + r * 0.28f);
            path.lineTo(x, y + r);
            path.lineTo(x - r * 0.28f, y + r * 0.28f);
            path.lineTo(x - r, y);
            path.lineTo(x - r * 0.28f, y - r * 0.28f);
            path.close();
            c.drawPath(path, p);
        }

        private void stroke(float width) {
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(width);
        }

        @Override public void setAlpha(int alpha) {}
        @Override public void setColorFilter(ColorFilter cf) {}
        @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    }
}
