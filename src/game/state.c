#include "state.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *GOAL_TEXT =
    "Find the brass key on the pedestal in the north part of the courtyard. "
    "Pick it up, then use it on the locked gate in the centre. "
    "Once the gate is open, walk through and reach the treasure chest to the south.";

void state_init(GameState *s) {
    memset(s, 0, sizeof(*s));
    s->char_pos = (Vector3){0.0f, 0.0f, 8.0f};
    s->char_rot = 180.0f;
    snprintf(s->goal, sizeof(s->goal), "%s", GOAL_TEXT);
    snprintf(s->status, sizeof(s->status), "Initialising CAM...");
    snprintf(s->agent_name, sizeof(s->agent_name), "CAM");
    s->manual_mode = false;
    s->input_len = 0;
    s->input_buffer[0] = '\0';
    s->bubble[0] = '\0';
    s->bubble_timer = 0.0f;
    s->crystals_collected = 0;
    s->crystals_total = 0;
}

void state_log(GameState *s, const char *msg) {
    int slot = s->event_count % MAX_EVENTS;
    snprintf(s->events[slot], MAX_EVENT_LEN, "%s", msg);
    s->event_count++;
    fprintf(stderr, "[world] %s\n", msg);
}

static bool contains_ci(const char *hay, const char *needle) {
    if (!needle[0]) return false;
    size_t hl = strlen(hay), nl = strlen(needle);
    if (nl > hl) return false;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        for (; j < nl; j++) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == nl) return true;
    }
    return false;
}

int state_find_object(const GameState *s, const char *query) {
    for (int i = 0; i < s->object_count; i++) {
        if (!s->objects[i].exists) continue;
        if (contains_ci(s->objects[i].name, query)) return i;
    }
    return -1;
}

bool state_in_inventory(const GameState *s, const char *name) {
    for (int i = 0; i < s->inv_count; i++) {
        if (strcmp(s->inventory[i], name) == 0) return true;
    }
    return false;
}

void state_add_inventory(GameState *s, const char *name) {
    if (s->inv_count >= MAX_INVENTORY) return;
    snprintf(s->inventory[s->inv_count++], MAX_NAME, "%s", name);
}

static const char *compass(float dx, float dz) {
    static const char *sectors[8] = {"N","NE","E","SE","S","SW","W","NW"};
    float angle = atan2f(dx, dz) * 180.0f / 3.14159265f;
    while (angle < 0) angle += 360.0f;
    int idx = ((int)((angle + 22.5f) / 45.0f)) & 7;
    return sectors[idx];
}

// Append-with-bounds: writes at *pos, advances *pos, never overflows `cap`.
static void jappend(char *buf, int *pos, int cap, const char *fmt, ...) {
    if (*pos >= cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    *pos += n;
    if (*pos > cap - 1) *pos = cap - 1;
}

// Minimal JSON string escape (handles ", \, newline).
static void json_escape(const char *in, char *out, int out_size) {
    int o = 0;
    for (int i = 0; in[i] && o < out_size - 2; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            if (o < out_size - 3) { out[o++] = '\\'; out[o++] = c; }
        } else if (c == '\n') {
            if (o < out_size - 3) { out[o++] = '\\'; out[o++] = 'n'; }
        } else if ((unsigned char)c < 0x20) {
            // skip control chars
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

void state_to_json(const GameState *s, char *out, int out_size) {
    int p = 0;
    char esc[MAX_GOAL_LEN + 8];

    jappend(out, &p, out_size,
        "{\"position\":{\"x\":%.1f,\"z\":%.1f},\"facing_degrees\":%.1f,",
        s->char_pos.x, s->char_pos.z,
        fmodf(s->char_rot, 360.0f) < 0 ? fmodf(s->char_rot, 360.0f) + 360.0f
                                        : fmodf(s->char_rot, 360.0f));

    // inventory
    jappend(out, &p, out_size, "\"inventory\":[");
    for (int i = 0; i < s->inv_count; i++) {
        json_escape(s->inventory[i], esc, sizeof(esc));
        jappend(out, &p, out_size, "%s\"%s\"", i ? "," : "", esc);
    }
    jappend(out, &p, out_size, "],");

    // nearby objects (radius 10)
    jappend(out, &p, out_size, "\"nearby_objects\":[");
    bool first = true;
    for (int i = 0; i < s->object_count; i++) {
        const GameObject *o = &s->objects[i];
        if (!o->exists) continue;
        float dx = o->position.x - s->char_pos.x;
        float dz = o->position.z - s->char_pos.z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 10.0f) continue;
        json_escape(o->name, esc, sizeof(esc));
        jappend(out, &p, out_size,
            "%s{\"name\":\"%s\",\"distance\":%.1f,\"direction\":\"%s\"",
            first ? "" : ",", esc, dist, compass(dx, dz));
        if (o->pickable)     jappend(out, &p, out_size, ",\"pickable\":true");
        if (o->interactable) jappend(out, &p, out_size, ",\"interactable\":true");
        if (o->interactable && strstr(o->name, "gate")) {
            jappend(out, &p, out_size, ",\"properties\":{\"locked\":%s}",
                    o->locked ? "true" : "false");
        } else if (o->interactable && strstr(o->name, "lever")) {
            jappend(out, &p, out_size, ",\"properties\":{\"pulled\":%s}",
                    o->pulled ? "true" : "false");
        } else if (o->interactable && strstr(o->name, "sign")) {
            jappend(out, &p, out_size, ",\"properties\":{\"read\":%s}",
                    o->read ? "true" : "false");
        }
        jappend(out, &p, out_size, "}");
        first = false;
    }
    jappend(out, &p, out_size, "],");

    // goal + completion + bonus tracking
    json_escape(s->goal, esc, sizeof(esc));
    jappend(out, &p, out_size,
        "\"goal\":\"%s\",\"goal_complete\":%s,"
        "\"crystals_collected\":%d,\"crystals_total\":%d,"
        "\"mode\":\"%s\",",
        esc, s->goal_complete ? "true" : "false",
        s->crystals_collected, s->crystals_total,
        s->manual_mode ? "manual" : "auto");

    // recent events (last 6 in chronological order)
    jappend(out, &p, out_size, "\"recent_events\":[");
    int total = s->event_count;
    int start = total > 6 ? total - 6 : 0;
    bool ef = true;
    for (int k = start; k < total; k++) {
        int slot = k % MAX_EVENTS;
        json_escape(s->events[slot], esc, sizeof(esc));
        jappend(out, &p, out_size, "%s\"%s\"", ef ? "" : ",", esc);
        ef = false;
    }
    jappend(out, &p, out_size, "]}");
}
