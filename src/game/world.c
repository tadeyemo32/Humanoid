#include "world.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

// ── Palette (brighter, higher-contrast version) ──────────────────────────
#define COL_STONE       ((Color){175, 170, 165, 255})
#define COL_STONE_DARK  ((Color){130, 125, 120, 255})
#define COL_WOOD        ((Color){120,  78,  38, 255})
#define COL_LEAF        ((Color){ 50, 150,  60, 255})
#define COL_LEAF_DARK   ((Color){ 35, 110,  45, 255})
#define COL_GRASS       ((Color){ 85, 165,  75, 255})
#define COL_GRASS_DARK  ((Color){ 65, 140,  55, 255})
#define COL_GOLD        ((Color){240, 195,  40, 255})
#define COL_FIRE        ((Color){255, 165,  35, 255})
#define COL_FIRE_GLOW   ((Color){255, 220, 110, 120})
#define COL_BAR_LOCKED  ((Color){ 75,  75,  85, 255})
#define COL_BAR_OPEN    ((Color){ 60, 200,  80, 255})
#define COL_CRYSTAL     ((Color){ 90, 220, 230, 255})
#define COL_CRYSTAL_GL  ((Color){140, 240, 255, 120})
#define COL_LEVER_BASE  ((Color){ 90,  90, 100, 255})
#define COL_LEVER_HAND  ((Color){200, 100,  60, 255})
#define COL_SIGN        ((Color){135,  90,  50, 255})
#define COL_SIGN_PLATE  ((Color){240, 220, 170, 255})

static int gate_idx     = -1;
static int key_idx      = -1;
static int chest_idx    = -1;
static int lever_idx    = -1;
static int sign_idx     = -1;
static int crystal_idx[3] = { -1, -1, -1 };

static int register_object(GameState *s, const char *name, Vector3 pos,
                           bool pickable, bool interactable, bool locked) {
    if (s->object_count >= MAX_OBJECTS) return -1;
    int i = s->object_count++;
    GameObject *o = &s->objects[i];
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", name);
    o->position     = pos;
    o->pickable     = pickable;
    o->interactable = interactable;
    o->locked       = locked;
    o->visible      = true;
    o->exists       = true;
    return i;
}

void world_init(GameState *state) {
    gate_idx  = register_object(state, "gate",
        (Vector3){0.0f, WALL_H * 0.5f, 0.0f}, false, true, true);
    key_idx   = register_object(state, "brass key",
        (Vector3){-8.0f, 1.35f, 10.0f}, true, false, false);
    chest_idx = register_object(state, "treasure chest",
        (Vector3){0.0f, 0.55f, -12.0f}, false, true, false);
    lever_idx = register_object(state, "lever",
        (Vector3){10.0f, 0.7f, 14.0f}, false, true, false);
    sign_idx  = register_object(state, "signpost",
        (Vector3){8.0f, 1.0f, 8.0f}, false, true, false);

    crystal_idx[0] = register_object(state, "blue crystal",
        (Vector3){-14.0f, 0.6f, 14.0f}, true, false, false);
    crystal_idx[1] = register_object(state, "green crystal",
        (Vector3){14.0f, 0.6f, -6.0f}, true, false, false);
    crystal_idx[2] = register_object(state, "violet crystal",
        (Vector3){-12.0f, 0.6f, -16.0f}, true, false, false);

    state->crystals_total = 3;
    state->crystals_collected = 0;
}

// ── Draw helpers ─────────────────────────────────────────────────────────

static void draw_box(Vector3 pos, Vector3 size, Color c) {
    DrawCube(pos, size.x, size.y, size.z, c);
    DrawCubeWires(pos, size.x, size.y, size.z, Fade(BLACK, 0.18f));
}

static void draw_box_nowire(Vector3 pos, Vector3 size, Color c) {
    DrawCube(pos, size.x, size.y, size.z, c);
}

// Small dark disc beneath an object for visual grounding.
static void draw_shadow(float x, float z, float r) {
    DrawCylinder((Vector3){x, 0.012f, z}, r, r, 0.005f, 16, Fade(BLACK, 0.25f));
}

static void draw_ground(void) {
    // Grass — extend well past the courtyard so horizon is green.
    draw_box_nowire((Vector3){0, -0.08f, 0}, (Vector3){120.0f, 0.12f, 120.0f}, COL_GRASS);
    // Speckle grass with a few darker patches
    const float patches[][2] = {
        {-30, -25}, {28, -32}, {-35, 30}, {32, 28},
        {-40, 0}, {40, 5}, {0, 40}, {0, -40},
    };
    for (int i = 0; i < (int)(sizeof(patches)/sizeof(patches[0])); i++) {
        draw_box_nowire((Vector3){patches[i][0], -0.06f, patches[i][1]},
                        (Vector3){8.0f, 0.05f, 8.0f}, COL_GRASS_DARK);
    }
    // Stone courtyard
    draw_box_nowire((Vector3){0, 0.0f, 0}, (Vector3){AREA * 2, 0.04f, AREA * 2}, COL_STONE);
    // Checker overlay
    for (int row = -3; row <= 3; row++) {
        for (int col = -3; col <= 3; col++) {
            if (((row + col) & 1) == 0) {
                draw_box_nowire((Vector3){col * 3.4f, 0.025f, row * 3.4f},
                                (Vector3){3.3f, 0.005f, 3.3f}, COL_STONE_DARK);
            }
        }
    }
}

static void draw_perimeter(void) {
    struct { float sx, sz, px, pz; } specs[4] = {
        {AREA * 2 + 2, 1,  0,       AREA},
        {AREA * 2 + 2, 1,  0,      -AREA},
        {1, AREA * 2,  AREA,       0},
        {1, AREA * 2, -AREA,       0},
    };
    for (int i = 0; i < 4; i++) {
        draw_box((Vector3){specs[i].px, WALL_H * 0.5f, specs[i].pz},
                 (Vector3){specs[i].sx, WALL_H, specs[i].sz},
                 COL_STONE);
        // Crenellations
        float major = fmaxf(specs[i].sx, specs[i].sz);
        int count = (int)(major / 2.0f);
        for (int k = 0; k < count; k++) {
            float t = ((float)k / (float)(count > 1 ? count - 1 : 1)) - 0.5f;
            float cx = specs[i].px;
            float cz = specs[i].pz;
            if (specs[i].sz == 1) cx += t * specs[i].sx;
            else                  cz += t * specs[i].sz * 2.0f;
            draw_box_nowire((Vector3){cx, WALL_H + 0.4f, cz},
                            (Vector3){0.7f, 0.8f, 0.7f}, COL_STONE_DARK);
        }
    }
}

static void draw_dividing_wall(const GameState *s) {
    float gate_w = 4.5f;
    float seg_len = AREA - gate_w * 0.5f;

    draw_box((Vector3){-seg_len * 0.5f - gate_w * 0.5f, WALL_H * 0.5f, 0},
             (Vector3){seg_len, WALL_H, 1.0f}, COL_STONE);
    draw_box((Vector3){ seg_len * 0.5f + gate_w * 0.5f, WALL_H * 0.5f, 0},
             (Vector3){seg_len, WALL_H, 1.0f}, COL_STONE);

    bool locked = (gate_idx >= 0) ? s->objects[gate_idx].locked : true;
    bool gate_visible = (gate_idx >= 0) ? s->objects[gate_idx].visible : true;
    Color bar_col = locked ? COL_BAR_LOCKED : COL_BAR_OPEN;
    if (gate_visible) {
        for (int i = 0; i < 5; i++) {
            float x = (i - 2) * (gate_w / 5.0f);
            draw_box((Vector3){x, WALL_H * 0.5f, 0},
                     (Vector3){0.25f, WALL_H, 0.25f}, bar_col);
        }
        draw_box((Vector3){0, WALL_H * 0.5f + WALL_H * 0.1f, 0},
                 (Vector3){gate_w, 0.3f, 0.3f}, bar_col);
    }
}

static void draw_key(const GameState *s, float anim_t) {
    if (key_idx < 0 || !s->objects[key_idx].exists || !s->objects[key_idx].visible) return;
    Vector3 pos = s->objects[key_idx].position;
    DrawCylinder((Vector3){pos.x, 0.0f, pos.z}, 0.3f, 0.3f, 0.9f, 16, COL_STONE_DARK);
    draw_box((Vector3){pos.x, 0.95f, pos.z}, (Vector3){0.8f, 0.1f, 0.8f}, COL_STONE_DARK);
    float y = pos.y + sinf(anim_t * 2.2f) * 0.15f;
    DrawSphere((Vector3){pos.x, y, pos.z}, 0.22f, COL_GOLD);
    DrawSphere((Vector3){pos.x, y, pos.z}, 0.45f, Fade(COL_GOLD, 0.18f));
    draw_shadow(pos.x, pos.z, 0.5f);
}

static void draw_treasure(const GameState *s) {
    if (chest_idx < 0) return;
    Vector3 pos = s->objects[chest_idx].position;
    draw_box((Vector3){pos.x, 0.55f, pos.z}, (Vector3){2.2f, 1.1f, 1.5f}, COL_WOOD);
    draw_box((Vector3){pos.x, 1.45f, pos.z}, (Vector3){2.2f, 0.7f, 1.5f}, COL_WOOD);
    for (int k = 0; k < 2; k++) {
        float xoff = (k == 0) ? -0.6f : 0.6f;
        draw_box((Vector3){pos.x + xoff, 1.0f, pos.z},
                 (Vector3){0.12f, 1.8f, 1.6f}, COL_GOLD);
    }
    draw_box((Vector3){pos.x, 1.0f, pos.z}, (Vector3){2.3f, 0.12f, 1.6f}, COL_GOLD);
    draw_box((Vector3){pos.x, 0.95f, pos.z + 0.74f},
             (Vector3){0.35f, 0.35f, 0.1f}, COL_GOLD);
    DrawSphere((Vector3){pos.x, 1.9f, pos.z}, 0.14f, RED);
    draw_shadow(pos.x, pos.z, 1.3f);
}

static void draw_crystals(const GameState *s, float anim_t) {
    Color cols[3] = {
        (Color){ 90, 200, 240, 255},  // blue
        (Color){ 90, 240, 130, 255},  // green
        (Color){190, 130, 240, 255},  // violet
    };
    for (int i = 0; i < 3; i++) {
        int idx = crystal_idx[i];
        if (idx < 0 || !s->objects[idx].exists || !s->objects[idx].visible) continue;
        Vector3 p = s->objects[idx].position;
        float bob = sinf(anim_t * 1.7f + i * 1.4f) * 0.1f;
        float angle = anim_t * 60.0f + i * 30.0f;
        // Diamond shape via two stacked cones
        rlPushMatrix();
        rlTranslatef(p.x, p.y + bob, p.z);
        rlRotatef(angle, 0, 1, 0);
        DrawCylinder((Vector3){0, 0, 0}, 0.0f, 0.25f, 0.35f, 5, cols[i]);
        DrawCylinder((Vector3){0, 0.35f, 0}, 0.25f, 0.0f, 0.35f, 5, cols[i]);
        rlPopMatrix();
        DrawSphere(p, 0.55f, Fade(cols[i], 0.15f));
        draw_shadow(p.x, p.z, 0.45f);
    }
}

static void draw_lever(const GameState *s) {
    if (lever_idx < 0) return;
    Vector3 p = s->objects[lever_idx].position;
    bool pulled = s->objects[lever_idx].pulled;
    // Base
    draw_box((Vector3){p.x, 0.35f, p.z}, (Vector3){0.9f, 0.7f, 0.9f}, COL_LEVER_BASE);
    // Handle (tilts when pulled)
    rlPushMatrix();
    rlTranslatef(p.x, 0.7f, p.z);
    rlRotatef(pulled ? 55.0f : -55.0f, 0, 0, 1);
    DrawCylinder((Vector3){0, 0.5f, 0}, 0.08f, 0.08f, 1.0f, 12, COL_LEVER_HAND);
    DrawSphere((Vector3){0, 1.0f, 0}, 0.14f, COL_GOLD);
    rlPopMatrix();
    draw_shadow(p.x, p.z, 0.7f);
}

static void draw_signpost(const GameState *s) {
    if (sign_idx < 0) return;
    Vector3 p = s->objects[sign_idx].position;
    DrawCylinder((Vector3){p.x, 0, p.z}, 0.08f, 0.08f, 1.5f, 12, COL_WOOD);
    draw_box((Vector3){p.x, 1.3f, p.z}, (Vector3){1.4f, 0.9f, 0.1f}, COL_SIGN);
    draw_box((Vector3){p.x, 1.3f, p.z + 0.06f}, (Vector3){1.25f, 0.75f, 0.02f}, COL_SIGN_PLATE);
    if (s->objects[sign_idx].read) {
        DrawSphere((Vector3){p.x, 2.0f, p.z}, 0.12f, COL_FIRE);
    }
    draw_shadow(p.x, p.z, 0.4f);
}

static void draw_trees(void) {
    const float spots[][2] = {
        {-16, 14}, {16, 14}, {-16, -14}, {16, -14},
        {-5, 18},  {5, 18},  {0, -18},
        {-18, 3},  {18, -3},
        {-28, 10}, {28, 10}, {-28, -10}, {28, -10},
    };
    int n = sizeof(spots) / sizeof(spots[0]);
    for (int i = 0; i < n; i++) {
        float x = spots[i][0], z = spots[i][1];
        DrawCylinder((Vector3){x, 0, z}, 0.25f, 0.25f, 2.8f, 12, COL_WOOD);
        // Two-tone layered foliage
        DrawSphere((Vector3){x, 4.0f, z}, 1.5f, COL_LEAF_DARK);
        DrawSphere((Vector3){x, 4.4f, z}, 1.2f, COL_LEAF);
        draw_shadow(x, z, 1.5f);
    }
}

static void draw_torches(float anim_t) {
    const float spots[][2] = {
        {-2.5f, 0.4f}, {2.5f, 0.4f}, {-2.5f, -0.4f}, {2.5f, -0.4f},
        {-AREA + 1, 14}, {AREA - 1, 14},
        {-AREA + 1, -14}, {AREA - 1, -14},
    };
    int n = sizeof(spots) / sizeof(spots[0]);
    for (int i = 0; i < n; i++) {
        float x = spots[i][0], z = spots[i][1];
        float y = WALL_H * 0.55f;
        DrawCylinder((Vector3){x, y - 0.4f, z}, 0.05f, 0.05f, 0.8f, 8, COL_WOOD);
        float flick = 1.0f + sinf(anim_t * 9.0f + i * 1.3f) * 0.18f;
        DrawSphere((Vector3){x, y + 0.15f, z}, 0.16f * flick, COL_FIRE);
        DrawSphere((Vector3){x, y + 0.15f, z}, 0.42f * flick, COL_FIRE_GLOW);
    }
}

static void draw_columns(void) {
    const float spots[][2] = {
        {-8, 0.4f}, {8, 0.4f}, {-8, -0.4f}, {8, -0.4f},
        {-6, 11}, {6, 11}, {-6, -11}, {6, -11},
    };
    int n = sizeof(spots) / sizeof(spots[0]);
    for (int i = 0; i < n; i++) {
        float x = spots[i][0], z = spots[i][1];
        DrawCylinder((Vector3){x, 0, z}, 0.32f, 0.3f, WALL_H, 16, COL_STONE);
        draw_box_nowire((Vector3){x, WALL_H + 0.15f, z},
                        (Vector3){0.95f, 0.3f, 0.95f}, COL_STONE_DARK);
    }
}

void world_draw(const GameState *state, float anim_t) {
    draw_ground();
    draw_perimeter();
    draw_dividing_wall(state);
    draw_columns();
    draw_trees();
    draw_torches(anim_t);
    draw_key(state, anim_t);
    draw_treasure(state);
    draw_lever(state);
    draw_signpost(state);
    draw_crystals(state, anim_t);
}
