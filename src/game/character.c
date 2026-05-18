#include "character.h"

#include <math.h>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#define SPEED     5.5f
#define ROT_SPEED 8.0f

void character_init(Character *c, GameState *s) {
    c->nav_active = false;
    c->target_x = 0;
    c->target_z = 0;
    c->nav_target_idx = -1;
    s->char_pos = (Vector3){0.0f, 0.0f, 8.0f};
    s->char_rot = 180.0f;
}

void character_set_target(Character *c, float x, float z) {
    c->nav_active = true;
    c->target_x = x;
    c->target_z = z;
}

void character_stop(Character *c) {
    c->nav_active = false;
    c->nav_target_idx = -1;
}

void character_update(Character *c, GameState *s, float dt) {
    if (!c->nav_active) return;

    float dx = c->target_x - s->char_pos.x;
    float dz = c->target_z - s->char_pos.z;
    float dist = sqrtf(dx * dx + dz * dz);

    if (dist < 0.35f) {
        s->char_pos.x = c->target_x;
        s->char_pos.z = c->target_z;
        c->nav_active = false;
        return;
    }

    float target_angle = atan2f(dx, dz) * 180.0f / 3.14159265f;
    // Lerp rotation
    float diff = target_angle - s->char_rot;
    while (diff > 180.0f)  diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    s->char_rot += diff * fminf(dt * ROT_SPEED, 1.0f);

    float speed = fminf(SPEED, dist * 6.0f);
    s->char_pos.x += (dx / dist) * speed * dt;
    s->char_pos.z += (dz / dist) * speed * dt;
}

void character_draw(const GameState *s) {
    // Push transform manually with rlgl
    rlPushMatrix();
    rlTranslatef(s->char_pos.x, s->char_pos.y, s->char_pos.z);
    rlRotatef(s->char_rot, 0.0f, 1.0f, 0.0f);

    Color body = (Color){ 30, 140, 200, 255};
    Color head = (Color){ 50, 170, 230, 255};
    Color stripe = (Color){255, 140,   0, 255};

    // Body
    DrawCube((Vector3){0, 0.65f, 0}, 0.75f, 1.3f, 0.65f, body);
    // Head
    DrawSphere((Vector3){0, 1.55f, 0}, 0.29f, head);
    // Eyes
    DrawSphere((Vector3){-0.12f, 1.63f, 0.28f}, 0.07f, WHITE);
    DrawSphere(( Vector3){ 0.12f, 1.63f, 0.28f}, 0.07f, WHITE);
    DrawSphere((Vector3){-0.12f, 1.63f, 0.32f}, 0.035f, BLACK);
    DrawSphere(( Vector3){ 0.12f, 1.63f, 0.32f}, 0.035f, BLACK);
    // Direction stripe (orange front)
    DrawCube((Vector3){0, 0.65f, 0.33f}, 0.77f, 0.15f, 0.1f, stripe);

    rlPopMatrix();
}

int character_nearest_pickable(const Character *c, const GameState *s) {
    (void)c;
    int best = -1;
    float best_d = PICKUP_RANGE;
    for (int i = 0; i < s->object_count; i++) {
        const GameObject *o = &s->objects[i];
        if (!o->exists || !o->pickable) continue;
        float dx = o->position.x - s->char_pos.x;
        float dz = o->position.z - s->char_pos.z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d <= best_d) { best = i; best_d = d; }
    }
    return best;
}

int character_nearest_interactable(const Character *c, const GameState *s) {
    (void)c;
    int best = -1;
    float best_d = INTERACT_RANGE;
    for (int i = 0; i < s->object_count; i++) {
        const GameObject *o = &s->objects[i];
        if (!o->exists || !o->interactable) continue;
        float dx = o->position.x - s->char_pos.x;
        float dz = o->position.z - s->char_pos.z;
        float d = sqrtf(dx * dx + dz * dz);
        if (d <= best_d) { best = i; best_d = d; }
    }
    return best;
}
