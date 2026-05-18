#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"

// Palette
#define PANEL_DARK   ((Color){ 18,  22,  30, 225})
#define PANEL_MID    ((Color){ 28,  34,  46, 230})
#define BORDER_HI    ((Color){ 90, 200, 255, 255})
#define BORDER_LO    ((Color){ 55,  75,  95, 255})
#define TEXT_PRIMARY ((Color){235, 240, 245, 255})
#define TEXT_MUTED   ((Color){170, 180, 195, 255})
#define TEXT_ACCENT  ((Color){255, 215,  80, 255})
#define MODE_AUTO    ((Color){ 80, 200, 120, 255})
#define MODE_MANUAL  ((Color){250, 180,  60, 255})
#define BUBBLE_BG    ((Color){ 18,  22,  30, 220})
#define BUBBLE_EDGE  ((Color){140, 200, 255, 255})

static Font g_font = {0};

// Lazy-loaded scaled default font.
static Font font(void) {
    if (g_font.texture.id == 0) g_font = GetFontDefault();
    return g_font;
}

static void draw_text(const char *s, int x, int y, int size, Color c) {
    DrawTextEx(font(), s, (Vector2){(float)x, (float)y}, (float)size, 1.5f, c);
}

static int text_width(const char *s, int size) {
    return (int)MeasureTextEx(font(), s, (float)size, 1.5f).x;
}

static void panel(int x, int y, int w, int h, Color bg, Color edge) {
    DrawRectangle(x, y, w, h, bg);
    DrawRectangleLines(x, y, w, h, edge);
}

// Wraps text into lines fitting `max_width` (very simple, splits on space only).
// Writes up to `max_lines` lines into `out` and returns count.
static int wrap_text(const char *s, int font_size, int max_width,
                     char out[][256], int max_lines) {
    int n = 0;
    const char *p = s;
    while (*p && n < max_lines) {
        // Build line
        char line[256] = {0};
        int line_len = 0;
        const char *word_start = p;
        while (*p) {
            // Find end of next word
            const char *q = p;
            while (*q && *q != ' ') q++;
            int word_len = (int)(q - p);
            if (word_len >= (int)sizeof(line) - line_len - 2) break;
            // Try appending
            char trial[256];
            int sep = (line_len > 0) ? 1 : 0;
            snprintf(trial, sizeof(trial), "%.*s%s%.*s",
                     line_len, line, sep ? " " : "", word_len, p);
            if (text_width(trial, font_size) > max_width && line_len > 0) break;
            // Commit
            snprintf(line, sizeof(line), "%s", trial);
            line_len = (int)strlen(line);
            p = q;
            while (*p == ' ') p++;
        }
        if (line_len == 0) {
            // single huge word — just take it
            const char *q = word_start;
            while (*q && *q != ' ') q++;
            snprintf(line, sizeof(line), "%.*s",
                     (int)(q - word_start), word_start);
            p = q;
            while (*p == ' ') p++;
        }
        snprintf(out[n], 256, "%s", line);
        n++;
    }
    return n;
}

void hud_draw(const GameState *s, int W, int H) {
    // ── Top bar (mode badge + goal) ───────────────────────────────────
    int top_h = 70;
    panel(0, 0, W, top_h, PANEL_DARK, BORDER_LO);

    // Mode badge
    Color mode_col = s->manual_mode ? MODE_MANUAL : MODE_AUTO;
    const char *mode_text = s->manual_mode ? "MANUAL" : "AUTO";
    int badge_w = 130;
    DrawRectangle(12, 14, badge_w, 30, Fade(mode_col, 0.20f));
    DrawRectangleLines(12, 14, badge_w, 30, mode_col);
    draw_text(mode_text, 22, 20, 18, mode_col);
    // Agent name pill
    draw_text(s->agent_name[0] ? s->agent_name : "AGENT", 22, 46, 16, TEXT_MUTED);

    // Goal text (wrapped, two lines max)
    char lines[2][256];
    int  n = wrap_text(s->goal, 18, W - badge_w - 220, lines, 2);
    for (int i = 0; i < n; i++) {
        draw_text(lines[i], badge_w + 24, 14 + i * 22, 18, TEXT_ACCENT);
    }

    // Step + crystals (top right)
    char step_buf[64];
    snprintf(step_buf, sizeof(step_buf), "STEP %d", s->step);
    int sw = text_width(step_buf, 20);
    draw_text(step_buf, W - sw - 20, 14, 20, TEXT_PRIMARY);

    char crystal_buf[32];
    snprintf(crystal_buf, sizeof(crystal_buf), "CRYSTALS %d/%d",
             s->crystals_collected, s->crystals_total);
    int cw = text_width(crystal_buf, 16);
    draw_text(crystal_buf, W - cw - 20, 42, 16,
              (Color){140, 220, 240, 255});

    // ── CV indicator (under mode badge) ───────────────────────────────
    Color cv = s->cv_active
        ? (Color){80, 230, 110, 255}
        : (Color){180, 60, 60, 255};
    DrawCircle(W - 14, top_h + 20, 7, cv);
    const char *cv_label = s->cv_active ? "VISION ACTIVE" : "VISION IDLE";
    int vw = text_width(cv_label, 14);
    draw_text(cv_label, W - 24 - vw, top_h + 12, 14, cv);

    // ── CAM speech bubble (just below top bar, centred) ───────────────
    if (s->bubble[0] && s->bubble_timer > 0.0f) {
        char b_lines[3][256];
        int bn = wrap_text(s->bubble, 18, W - 240, b_lines, 3);
        int bh = bn * 24 + 24;
        int bw = 0;
        for (int i = 0; i < bn; i++) {
            int lw = text_width(b_lines[i], 18);
            if (lw > bw) bw = lw;
        }
        bw += 32;
        int bx = (W - bw) / 2;
        int by = top_h + 50;
        Color fade = BUBBLE_BG;
        float alpha = fminf(s->bubble_timer, 1.0f);
        fade.a = (unsigned char)(220 * alpha);
        DrawRectangleRounded((Rectangle){(float)bx, (float)by, (float)bw, (float)bh},
                             0.25f, 8, fade);
        DrawRectangleRoundedLines((Rectangle){(float)bx, (float)by, (float)bw, (float)bh},
                                  0.25f, 8, Fade(BUBBLE_EDGE, alpha));
        char tag[24];
        snprintf(tag, sizeof(tag), "%s:", s->agent_name[0] ? s->agent_name : "CAM");
        draw_text(tag, bx + 14, by + 8, 14, BUBBLE_EDGE);
        for (int i = 0; i < bn; i++) {
            draw_text(b_lines[i], bx + 16, by + 24 + i * 24, 18, TEXT_PRIMARY);
        }
    }

    // ── Inventory (bottom-left chips) ─────────────────────────────────
    int inv_y = H - 170;
    panel(12, inv_y, 340, 140, PANEL_DARK, BORDER_LO);
    draw_text("INVENTORY", 24, inv_y + 10, 14, TEXT_MUTED);
    if (s->inv_count == 0) {
        draw_text("(empty)", 24, inv_y + 36, 18, TEXT_MUTED);
    } else {
        int cx = 24, cy = inv_y + 36;
        for (int i = 0; i < s->inv_count; i++) {
            int chw = text_width(s->inventory[i], 16) + 22;
            if (cx + chw > 340) { cx = 24; cy += 32; }
            DrawRectangleRounded((Rectangle){(float)cx, (float)cy, (float)chw, 26.0f},
                                 0.4f, 6, Fade(TEXT_ACCENT, 0.18f));
            DrawRectangleRoundedLines((Rectangle){(float)cx, (float)cy, (float)chw, 26.0f},
                                      0.4f, 6, TEXT_ACCENT);
            draw_text(s->inventory[i], cx + 10, cy + 5, 16, TEXT_ACCENT);
            cx += chw + 8;
        }
    }

    // ── Stats (bottom-right) ──────────────────────────────────────────
    int st_w = 240, st_h = 100;
    panel(W - st_w - 12, H - 170, st_w, st_h, PANEL_DARK, BORDER_LO);
    draw_text("STATUS", W - st_w + 0, H - 162, 14, TEXT_MUTED);
    char pbuf[128];
    float rot = fmodf(s->char_rot, 360.0f); if (rot < 0) rot += 360.0f;
    snprintf(pbuf, sizeof(pbuf), "x: %+.1f", s->char_pos.x);
    draw_text(pbuf, W - st_w + 0, H - 140, 18, TEXT_PRIMARY);
    snprintf(pbuf, sizeof(pbuf), "z: %+.1f", s->char_pos.z);
    draw_text(pbuf, W - st_w + 0, H - 118, 18, TEXT_PRIMARY);
    snprintf(pbuf, sizeof(pbuf), "rot: %.0f", rot);
    draw_text(pbuf, W - st_w + 0, H - 96, 18, TEXT_PRIMARY);

    // ── Status bar (just above input) ─────────────────────────────────
    int sb_y = H - 92;
    panel(0, sb_y, W, 30, PANEL_MID, BORDER_LO);
    char tr[256];
    if ((int)strlen(s->status) > 110) {
        snprintf(tr, sizeof(tr), "%.107s...", s->status);
    } else {
        snprintf(tr, sizeof(tr), "%s", s->status);
    }
    draw_text(tr, 16, sb_y + 6, 18, (Color){190, 225, 255, 255});

    // ── Input bar (bottom) ────────────────────────────────────────────
    int ib_y = H - 56;
    panel(0, ib_y, W, 56, PANEL_DARK, BORDER_HI);
    draw_text(">", 16, ib_y + 16, 22, BORDER_HI);
    char display[260];
    snprintf(display, sizeof(display), "%s", s->input_buffer);
    draw_text(display, 44, ib_y + 16, 20, TEXT_PRIMARY);
    // Blinking caret
    int caret_x = 44 + text_width(display, 20) + 2;
    if (((int)(GetTime() * 2)) & 1) {
        DrawRectangle(caret_x, ib_y + 18, 2, 22, TEXT_PRIMARY);
    }
    const char *hints = "ENTER submit  -  TAB auto/manual  -  F1 describe  -  ESC clear";
    int hw = text_width(hints, 14);
    draw_text(hints, W - hw - 14, ib_y + 38, 14, TEXT_MUTED);
}
