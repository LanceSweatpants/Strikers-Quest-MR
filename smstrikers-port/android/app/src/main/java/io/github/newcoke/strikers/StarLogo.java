package io.github.newcoke.strikers;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;

/**
 * strikers-quest r45: a team's logo in the tournaments (Lance: "the star player (mario and such) being
 * the logo"): a round badge in the captain's colours with a letter, drawn here, no game art. Stars are
 * the game's eTeamID: 0 Daisy, 1 Donkey Kong, 2 Luigi, 3 Mario, 4 Peach, 5 Waluigi, 6 Wario, 7 Yoshi;
 * -1 none (a grey "?").
 */
final class StarLogo extends Drawable {
    static final int COUNT = 8;
    static final String[] NAMES = {"Daisy", "Donkey Kong", "Luigi", "Mario", "Peach", "Waluigi", "Wario", "Yoshi"};
    private static final int[] FILL = {0xFFFF8C1A, 0xFF8B5A2B, 0xFF1FA83A, 0xFFE4202A, 0xFFFF6FB5, 0xFF6B2FB3,
                                       0xFFFFD21F, 0xFF3CC23C};
    private static final int[] RING = {0xFFFFD21F, 0xFFFFD83A, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFE27A, 0xFF1A1A1A,
                                       0xFF6E2C9C, 0xFFFFFFFF};
    private static final int[] INK = {0xFFFFFFFF, 0xFFFFD83A, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFF0C419,
                                      0xFF6E2C9C, 0xFFFFFFFF};
    // Waluigi's is the upside-down L of his cap (a Greek capital gamma).
    private static final String[] MARK = {"D", "DK", "L", "M", "P", "Γ", "W", "Y"};

    private final int star;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

    StarLogo(int star) {
        this.star = star >= 0 && star < COUNT ? star : -1;
    }

    /** The character's name (the same in every language), or "No star yet" in the panel's. */
    static String name(Context context, int star) {
        return star >= 0 && star < COUNT ? NAMES[star] : context.getString(R.string.star_none);
    }

    @Override
    public void draw(Canvas canvas) {
        draw(canvas, getBounds().exactCenterX(), getBounds().exactCenterY(),
                Math.min(getBounds().width(), getBounds().height()) * 0.5f, star, paint);
    }

    /** A badge of radius r centred on (cx, cy). */
    static void draw(Canvas canvas, float cx, float cy, float r, int star, Paint paint) {
        final boolean known = star >= 0 && star < COUNT;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xFF000000);
        canvas.drawCircle(cx, cy + r * 0.06f, r, paint);  // a little shadow under it
        paint.setColor(known ? RING[star] : 0xFF9AA6BE);
        canvas.drawCircle(cx, cy, r, paint);
        paint.setColor(known ? FILL[star] : 0xFF48526A);
        canvas.drawCircle(cx, cy, r * 0.84f, paint);
        // A soft shine over the top.
        paint.setColor(0x33FFFFFF);
        canvas.drawCircle(cx, cy - r * 0.28f, r * 0.55f, paint);
        final String mark = known ? MARK[star] : "?";
        paint.setColor(known ? INK[star] : Color.WHITE);
        paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(r * (mark.length() > 1 ? 0.78f : 1.05f));
        final Rect b = new Rect();
        paint.getTextBounds(mark, 0, mark.length(), b);
        canvas.drawText(mark, cx, cy + b.height() * 0.5f, paint);
    }

    @Override
    public void setAlpha(int alpha) {
        paint.setAlpha(alpha);
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        paint.setColorFilter(colorFilter);
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
