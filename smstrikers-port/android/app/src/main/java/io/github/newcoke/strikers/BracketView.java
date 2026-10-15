package io.github.newcoke.strikers;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.view.View;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * strikers-quest r45: the tournament's playoff tree (Lance: "a tournament tree?? like a playoff tree??
 * with the star player ... being the logo and the team name being shown on the bracket"). One column
 * per round, left to right, each match a card of two teams (logo, name, goals), joined by lines to the
 * match its winner goes to, and the champion at the end. Drawn from the hub's tournament object.
 */
final class BracketView extends View {
    private static final int GOLD = 0xFFFFC400;
    private static final int CARD = 0xFF1E2634;
    private static final int CARD_YOU = 0xFF2B3A55;
    private static final int LINE = 0xFF5A6478;
    private static final int TEXT = 0xFFF0F3F8;
    private static final int DIM = 0xFF8C96A8;

    private JSONObject tourney;
    private int you = -1;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float dp;

    // Layout, in pixels.
    private final float rowH, cardW, colGap, cardGap, pad;

    BracketView(Context context) {
        super(context);
        dp = context.getResources().getDisplayMetrics().density;
        rowH = 34 * dp;
        cardW = 230 * dp;
        colGap = 40 * dp;
        cardGap = 18 * dp;
        pad = 12 * dp;
    }

    void set(JSONObject tourney, int you) {
        this.tourney = tourney;
        this.you = you;
        requestLayout();
        invalidate();
    }

    private int rounds() {
        JSONArray r = tourney != null ? tourney.optJSONArray("rounds") : null;
        return r != null ? r.length() : 0;
    }

    private int firstRoundMatches() {
        JSONArray r = tourney != null ? tourney.optJSONArray("rounds") : null;
        JSONArray m = r != null ? r.optJSONArray(0) : null;
        return m != null ? Math.max(1, m.length()) : 1;
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        final int cols = rounds() + 1;  // + the champion
        final float w = pad * 2 + cols * cardW + (cols - 1) * colGap;
        final float h = pad * 2 + firstRoundMatches() * (2 * rowH + cardGap) + 24 * dp;
        setMeasuredDimension((int) Math.ceil(w), (int) Math.ceil(h));
    }

    private String name(int i) {
        JSONArray n = tourney.optJSONArray("names");
        return n != null && i >= 0 && i < n.length() ? n.optString(i) : "";
    }

    private int star(int i) {
        JSONArray s = tourney.optJSONArray("stars");
        return s != null && i >= 0 && i < s.length() ? s.optInt(i, -1) : -1;
    }

    /** The vertical centre of match i of round r: halfway between the two it is fed by. */
    private float centreY(int r, int i) {
        final float top = pad + 24 * dp;
        final float slot = 2 * rowH + cardGap;  // one first-round match
        final float span = slot * (1 << r);
        return top + span * i + span * 0.5f;
    }

    private float columnX(int r) {
        return pad + r * (cardW + colGap);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (tourney == null) return;
        final JSONArray rounds = tourney.optJSONArray("rounds");
        if (rounds == null) return;
        final int n = rounds.length();
        final int champion = tourney.isNull("champion") ? -1 : tourney.optInt("champion", -1);
        // The round names over their columns.
        paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(14 * dp);
        for (int r = 0; r <= n; r++) {
            final int fromEnd = n - 1 - r;
            final Context c = getContext();
            final String label = r == n ? c.getString(R.string.tourney_champion)
                    : fromEnd == 0 ? c.getString(R.string.tourney_final)
                    : fromEnd == 1 ? c.getString(R.string.tourney_semi)
                    : fromEnd == 2 ? c.getString(R.string.tourney_quarter)
                    : c.getString(R.string.tourney_round, r + 1);
            paint.setColor(r == n ? GOLD : DIM);
            canvas.drawText(label, columnX(r) + cardW * 0.5f, pad + 14 * dp, paint);
        }
        // The lines first, under the cards: each match to the one its winner goes to.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2.5f * dp);
        for (int r = 0; r < n; r++) {
            JSONArray ms = rounds.optJSONArray(r);
            for (int i = 0; ms != null && i < ms.length(); i++) {
                JSONObject m = ms.optJSONObject(i);
                final boolean done = m != null && "done".equals(m.optString("status"));
                paint.setColor(done ? GOLD : LINE);
                final float x0 = columnX(r) + cardW, y0 = centreY(r, i);
                final float x1 = columnX(r + 1), y1 = r + 1 < n ? centreY(r + 1, i / 2) : centreY(r, 0);
                final float xm = x0 + colGap * 0.5f;
                canvas.drawLine(x0, y0, xm, y0, paint);
                canvas.drawLine(xm, y0, xm, y1, paint);
                canvas.drawLine(xm, y1, x1, y1, paint);
            }
        }
        paint.setStyle(Paint.Style.FILL);
        // The matches.
        for (int r = 0; r < n; r++) {
            JSONArray ms = rounds.optJSONArray(r);
            for (int i = 0; ms != null && i < ms.length(); i++) {
                JSONObject m = ms.optJSONObject(i);
                if (m != null) drawMatch(canvas, m, columnX(r), centreY(r, i));
            }
        }
        // The champion.
        final float cx = columnX(n), cy = centreY(n - 1, 0);
        final RectF box = new RectF(cx, cy - rowH * 1.4f, cx + cardW, cy + rowH * 1.4f);
        paint.setColor(champion >= 0 ? 0xFF3A2E05 : CARD);
        canvas.drawRoundRect(box, 12 * dp, 12 * dp, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2.5f * dp);
        paint.setColor(champion >= 0 ? GOLD : LINE);
        canvas.drawRoundRect(box, 12 * dp, 12 * dp, paint);
        paint.setStyle(Paint.Style.FILL);
        if (champion >= 0) {
            StarLogo.draw(canvas, cx + rowH * 1.1f, cy, rowH * 0.9f, star(champion), paint);
            paint.setTextAlign(Paint.Align.LEFT);
            paint.setTypeface(Typeface.DEFAULT_BOLD);
            paint.setColor(GOLD);
            paint.setTextSize(16 * dp);
            canvas.drawText(fit(name(champion), cardW - rowH * 2.4f, 16 * dp), cx + rowH * 2.2f, cy + 6 * dp, paint);
        } else {
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setColor(DIM);
            paint.setTextSize(14 * dp);
            paint.setTypeface(Typeface.DEFAULT);
            canvas.drawText("?", cx + cardW * 0.5f, cy + 5 * dp, paint);
        }
    }

    private void drawMatch(Canvas canvas, JSONObject m, float x, float cy) {
        final int a = m.isNull("a") ? -1 : m.optInt("a", -1), b = m.isNull("b") ? -1 : m.optInt("b", -1);
        final String status = m.optString("status");
        final int winner = m.isNull("winner") ? -1 : m.optInt("winner", -1);
        final JSONArray score = m.optJSONArray("score");
        final boolean meIn = you >= 0 && (a == you || b == you);
        final RectF card = new RectF(x, cy - rowH, x + cardW, cy + rowH);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(meIn ? CARD_YOU : CARD);
        canvas.drawRoundRect(card, 10 * dp, 10 * dp, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth((meIn ? 2.5f : 1.5f) * dp);
        paint.setColor("ready".equals(status) ? GOLD : meIn ? 0xFF8FB3FF : LINE);
        canvas.drawRoundRect(card, 10 * dp, 10 * dp, paint);
        paint.setStrokeWidth(1 * dp);
        paint.setColor(LINE);
        canvas.drawLine(x + 6 * dp, cy, x + cardW - 6 * dp, cy, paint);
        paint.setStyle(Paint.Style.FILL);
        drawTeam(canvas, x, cy - rowH, a, winner, score != null && score.length() == 2 ? score.optInt(0) : -1, status);
        drawTeam(canvas, x, cy, b, winner, score != null && score.length() == 2 ? score.optInt(1) : -1, status);
        if ("ready".equals(status)) {
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTypeface(Typeface.DEFAULT_BOLD);
            paint.setTextSize(10 * dp);
            paint.setColor(GOLD);
            canvas.drawText(getContext().getString(m.optInt("replays", 0) > 0 ? R.string.tourney_badge_replay
                    : R.string.tourney_badge_playing), x + cardW * 0.5f, cy - rowH - 4 * dp, paint);
        }
    }

    private void drawTeam(Canvas canvas, float x, float top, int team, int winner, int goals, String status) {
        final float cy = top + rowH * 0.5f;
        final boolean done = "done".equals(status);
        final boolean won = done && team >= 0 && team == winner;
        final boolean lost = done && team >= 0 && team != winner;
        if (team >= 0) {
            StarLogo.draw(canvas, x + rowH * 0.6f, cy, rowH * 0.36f, star(team), paint);
        }
        paint.setTextAlign(Paint.Align.LEFT);
        paint.setTypeface(won || team == you ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        paint.setTextSize(14 * dp);
        paint.setColor(team < 0 ? DIM : won ? GOLD : lost ? DIM : TEXT);
        final String label = team < 0 ? getContext().getString(R.string.tourney_waiting)
                : name(team) + (team == you ? "  " + getContext().getString(R.string.tourney_tag_you) : "");
        canvas.drawText(fit(label, cardW - rowH * 1.3f - 36 * dp, 14 * dp), x + rowH * 1.15f, cy + 5 * dp, paint);
        if (done && goals >= 0) {
            paint.setTextAlign(Paint.Align.RIGHT);
            paint.setTypeface(Typeface.DEFAULT_BOLD);
            paint.setColor(won ? GOLD : DIM);
            canvas.drawText(String.valueOf(goals), x + cardW - 10 * dp, cy + 5 * dp, paint);
        }
    }

    /** `text` cut to `width` at text size `size`, with an ellipsis. */
    private String fit(String text, float width, float size) {
        paint.setTextSize(size);
        if (paint.measureText(text) <= width) return text;
        String t = text;
        while (t.length() > 1 && paint.measureText(t + "…") > width) t = t.substring(0, t.length() - 1);
        return t + "…";
    }
}
