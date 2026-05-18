#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LINE_MAX_LEN 1024

static char    line_buf[LINE_MAX_LEN];
static int     line_len = 0;
static bool    awaiting_screenshot = false;
static char    pending_screenshot_path[512];

void ipc_init(void) {
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

static void write_line(const char *s) {
    fputs(s, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void respond_ok(const char *msg) {
    char buf[1200];
    snprintf(buf, sizeof(buf), "OK %s", msg ? msg : "");
    write_line(buf);
}

static void respond_err(const char *msg) {
    char buf[1200];
    snprintf(buf, sizeof(buf), "ERR %s", msg ? msg : "");
    write_line(buf);
}

void ipc_emit_event(const char *fmt, ...) {
    char buf[1200];
    int n = snprintf(buf, sizeof(buf), "EVT ");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    write_line(buf);
}

static void approach_offset(const GameState *s, float px, float pz,
                            float stop_distance,
                            float *ox, float *oz) {
    float dx = px - s->char_pos.x;
    float dz = pz - s->char_pos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.0001f) dist = 1.0f;
    *ox = -dx / dist * stop_distance;
    *oz = -dz / dist * stop_distance;
}

// If the character is still walking, snap to the destination so the next
// action (pickup/use/interact) doesn't fail just because nav hasn't finished.
static void finish_nav_if_active(GameState *s, Character *c) {
    if (c->nav_active) {
        s->char_pos.x = c->target_x;
        s->char_pos.z = c->target_z;
        c->nav_active = false;
    }
}

static void set_status(GameState *s, const char *msg) {
    snprintf(s->status, sizeof(s->status), "%s", msg);
}

static void set_bubble(GameState *s, const char *msg, float seconds) {
    snprintf(s->bubble, sizeof(s->bubble), "%s", msg);
    s->bubble_timer = seconds;
}

static bool handle_command(char *line, GameState *s, Character *c,
                           bool *screenshot_pending,
                           char *screenshot_path, int path_size) {
    int L = (int)strlen(line);
    while (L > 0 && (line[L-1] == '\r' || line[L-1] == '\n')) line[--L] = '\0';
    if (L == 0) { respond_ok(""); return false; }

    char *verb = line;
    char *args = strchr(line, ' ');
    if (args) { *args++ = '\0'; while (*args == ' ') args++; } else { args = ""; }

    if (strcmp(verb, "STATE") == 0) {
        char json[8192];
        state_to_json(s, json, sizeof(json));
        char out[8400];
        snprintf(out, sizeof(out), "OK %s", json);
        write_line(out);
    }
    else if (strcmp(verb, "STATUS") == 0) {
        set_status(s, args);
        respond_ok("");
    }
    else if (strcmp(verb, "GOAL") == 0) {
        snprintf(s->goal, sizeof(s->goal), "%s", args);
        respond_ok("");
    }
    else if (strcmp(verb, "CV") == 0) {
        s->cv_active = (args[0] == '1');
        respond_ok("");
    }
    else if (strcmp(verb, "MODE") == 0) {
        s->manual_mode = (strcmp(args, "manual") == 0);
        respond_ok(s->manual_mode ? "manual" : "auto");
    }
    else if (strcmp(verb, "BUBBLE") == 0) {
        set_bubble(s, args, 8.0f);
        respond_ok("");
    }
    else if (strcmp(verb, "AGENT_NAME") == 0) {
        snprintf(s->agent_name, sizeof(s->agent_name), "%s", args);
        respond_ok("");
    }
    else if (strcmp(verb, "NAV") == 0) {
        if (!args[0]) { respond_err("missing target"); return false; }
        int idx = state_find_object(s, args);
        if (idx < 0) {
            char m[200]; snprintf(m, sizeof(m), "Can't find '%s' in the world.", args);
            respond_err(m); state_log(s, m); return false;
        }
        const GameObject *obj = &s->objects[idx];
        float ox, oz;
        float stop_distance = obj->pickable ? 0.0f : 1.0f;
        approach_offset(s, obj->position.x, obj->position.z, stop_distance, &ox, &oz);
        c->nav_target_idx = idx;
        character_set_target(c,
            obj->position.x + ox,
            obj->position.z + oz);
        char m[200]; snprintf(m, sizeof(m), "Navigating to %s...", args);
        state_log(s, m);
        s->step++;
        set_status(s, m);
        respond_ok(m);
    }
    else if (strcmp(verb, "PICKUP") == 0) {
        finish_nav_if_active(s, c);
        int idx = character_nearest_pickable(c, s);
        if (idx < 0 && c->nav_target_idx >= 0 && c->nav_target_idx < s->object_count) {
            GameObject *target = &s->objects[c->nav_target_idx];
            if (target->exists && target->pickable) {
                s->char_pos.x = target->position.x;
                s->char_pos.z = target->position.z;
                idx = c->nav_target_idx;
            }
        }
        if (idx < 0) {
            const char *m = "Nothing within reach to pick up.";
            respond_err(m); state_log(s, m); return false;
        }
        const char *nm = s->objects[idx].name;
        bool is_crystal = strstr(nm, "crystal") != NULL;
        state_add_inventory(s, nm);
        char m[200]; snprintf(m, sizeof(m), "Picked up %s.", nm);
        s->objects[idx].exists = false;
        s->objects[idx].visible = false;
        if (is_crystal) {
            s->crystals_collected++;
            char m2[256];
            snprintf(m2, sizeof(m2), "Picked up %s. (%d/%d crystals)",
                     nm, s->crystals_collected, s->crystals_total);
            state_log(s, m2);
            set_status(s, m2);
            s->step++;
            respond_ok(m2);
            return false;
        }
        state_log(s, m);
        s->step++;
        set_status(s, m);
        c->nav_target_idx = -1;
        respond_ok(m);
    }
    else if (strcmp(verb, "USE") == 0) {
        finish_nav_if_active(s, c);
        if (!args[0]) { respond_err("missing item"); return false; }
        if (!state_in_inventory(s, args)) {
            char m[200]; snprintf(m, sizeof(m), "'%s' is not in your inventory.", args);
            respond_err(m); state_log(s, m); return false;
        }
        int t = character_nearest_interactable(c, s);
        if (t < 0) {
            const char *m = "No interactable object nearby.";
            respond_err(m); state_log(s, m); return false;
        }
        const char *tname = s->objects[t].name;
        bool item_is_key = strstr(args, "key") != NULL;
        bool target_is_gate = strstr(tname, "gate") != NULL;
        if (item_is_key && target_is_gate) {
            if (s->objects[t].locked) {
                s->objects[t].locked = false;
                s->objects[t].visible = false;
                const char *m = "Gate unlocked! The bars glow green.";
                state_log(s, m);
                s->step++;
                set_status(s, m);
                respond_ok(m);
            } else {
                const char *m = "The gate is already unlocked.";
                respond_err(m); state_log(s, m);
            }
            return false;
        }
        char m[200]; snprintf(m, sizeof(m), "Can't use '%s' on %s.", args, tname);
        respond_err(m); state_log(s, m);
    }
    else if (strcmp(verb, "INTERACT") == 0) {
        finish_nav_if_active(s, c);
        int idx = character_nearest_interactable(c, s);
        if (idx < 0) {
            const char *m = "Nothing nearby to interact with.";
            respond_err(m); state_log(s, m); return false;
        }
        const char *nm = s->objects[idx].name;
        if (strstr(nm, "chest")) {
            s->goal_complete = true;
            const char *m = "You open the treasure chest -- GOAL COMPLETE!";
            state_log(s, m);
            s->step++;
            set_status(s, m);
            respond_ok(m);
            return false;
        }
        if (strstr(nm, "gate")) {
            if (s->objects[idx].locked) {
                const char *m = "The gate is locked. You need the brass key.";
                respond_err(m); state_log(s, m); return false;
            }
            const char *m = "The gate is open -- walk through!";
            state_log(s, m);
            s->step++;
            set_status(s, m);
            respond_ok(m);
            return false;
        }
        if (strstr(nm, "lever")) {
            s->objects[idx].pulled = !s->objects[idx].pulled;
            const char *m = s->objects[idx].pulled
                ? "You pull the lever. A faint click echoes from the south wall."
                : "You return the lever to its resting position.";
            state_log(s, m);
            s->step++;
            set_status(s, m);
            respond_ok(m);
            return false;
        }
        if (strstr(nm, "sign")) {
            s->objects[idx].read = true;
            const char *m =
                "The signpost reads: 'Brass for the gate. Crystals for the curious.'";
            state_log(s, m);
            s->step++;
            set_status(s, m);
            respond_ok(m);
            return false;
        }
        char m[200]; snprintf(m, sizeof(m), "Interacted with %s.", nm);
        state_log(s, m);
        s->step++;
        respond_ok(m);
    }
    else if (strcmp(verb, "WAIT") == 0) {
        const char *m = "Observing...";
        state_log(s, m);
        s->step++;
        set_status(s, m);
        respond_ok(m);
    }
    else if (strcmp(verb, "SCREENSHOT") == 0) {
        if (!args[0]) { respond_err("missing path"); return false; }
        snprintf(pending_screenshot_path, sizeof(pending_screenshot_path), "%s", args);
        snprintf(screenshot_path, path_size, "%s", args);
        *screenshot_pending = true;
        awaiting_screenshot = true;
    }
    else if (strcmp(verb, "QUIT") == 0) {
        respond_ok("bye");
        return true;
    }
    else {
        char m[200]; snprintf(m, sizeof(m), "Unknown verb '%s'", verb);
        respond_err(m);
    }
    return false;
}

bool ipc_poll(GameState *s, Character *c,
              bool *screenshot_pending, char *screenshot_path, int path_size) {
    if (awaiting_screenshot) return true;

    char tmp[256];
    for (;;) {
        ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                char ch = tmp[i];
                if (ch == '\n') {
                    line_buf[line_len] = '\0';
                    bool quit = handle_command(line_buf, s, c,
                                               screenshot_pending,
                                               screenshot_path, path_size);
                    line_len = 0;
                    if (quit) return false;
                    if (awaiting_screenshot) return true;
                } else if (line_len < LINE_MAX_LEN - 1) {
                    line_buf[line_len++] = ch;
                }
            }
        } else if (n == 0) {
            return false;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
            break;
        }
    }
    return true;
}

void ipc_screenshot_done(bool ok) {
    if (ok) respond_ok(pending_screenshot_path);
    else    respond_err("screenshot failed");
    awaiting_screenshot = false;
}
