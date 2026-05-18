#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#include "raymath.h"

#include "state.h"
#include "world.h"
#include "character.h"
#include "hud.h"
#include "ipc.h"

#define WIN_W 1280
#define WIN_H 768

static void handle_keyboard_input(GameState *state) {
    // Type-able characters
    int ch;
    while ((ch = GetCharPressed()) > 0) {
        if (state->input_len < (int)sizeof(state->input_buffer) - 1
            && ch >= 32 && ch < 127) {
            state->input_buffer[state->input_len++] = (char)ch;
            state->input_buffer[state->input_len] = '\0';
        }
    }
    // Control keys
    int key;
    while ((key = GetKeyPressed()) != 0) {
        if (key == KEY_BACKSPACE) {
            if (state->input_len > 0) {
                state->input_buffer[--state->input_len] = '\0';
            }
        } else if (key == KEY_ENTER || key == KEY_KP_ENTER) {
            if (state->input_len > 0) {
                ipc_emit_event("INPUT %s", state->input_buffer);
                state->input_len = 0;
                state->input_buffer[0] = '\0';
            }
        } else if (key == KEY_TAB) {
            state->manual_mode = !state->manual_mode;
            ipc_emit_event("MODE %s", state->manual_mode ? "manual" : "auto");
        } else if (key == KEY_ESCAPE) {
            state->input_len = 0;
            state->input_buffer[0] = '\0';
        } else if (key == KEY_F1) {
            ipc_emit_event("DESCRIBE");
        }
    }
}

int main(void) {
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "Humanoid -- CAM (C/raylib)");
    SetTargetFPS(60);
    SetExitKey(0);  // disable ESC closing window — we use it to clear input

    GameState  state;
    Character  character;
    state_init(&state);
    world_init(&state);
    character_init(&character, &state);

    ipc_init();

    Camera3D cam = {0};
    cam.up = (Vector3){0.0f, 1.0f, 0.0f};
    cam.fovy = 65.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    cam.position = (Vector3){state.char_pos.x, state.char_pos.y + 9.0f,
                             state.char_pos.z - 14.0f};
    cam.target   = (Vector3){state.char_pos.x, state.char_pos.y + 1.2f, state.char_pos.z};

    float anim_t = 0.0f;
    bool  screenshot_pending = false;
    char  screenshot_path[512] = {0};

    bool keep_running = true;

    while (keep_running && !WindowShouldClose()) {
        float dt = GetFrameTime();
        anim_t += dt;
        if (state.bubble_timer > 0.0f) state.bubble_timer -= dt;

        handle_keyboard_input(&state);

        if (!ipc_poll(&state, &character,
                      &screenshot_pending, screenshot_path, sizeof(screenshot_path))) {
            keep_running = false;
        }

        character_update(&character, &state, dt);

        // Smooth follow camera
        Vector3 desired_cam = {state.char_pos.x, state.char_pos.y + 9.0f,
                               state.char_pos.z - 14.0f};
        float follow = fminf(dt * 3.0f, 1.0f);
        cam.position.x += (desired_cam.x - cam.position.x) * follow;
        cam.position.y += (desired_cam.y - cam.position.y) * follow;
        cam.position.z += (desired_cam.z - cam.position.z) * follow;
        cam.target = (Vector3){state.char_pos.x, state.char_pos.y + 1.2f, state.char_pos.z};

        BeginDrawing();
        ClearBackground((Color){135, 195, 235, 255});

        BeginMode3D(cam);
        world_draw(&state, anim_t);
        character_draw(&state);
        EndMode3D();

        hud_draw(&state, WIN_W, WIN_H);
        EndDrawing();

        if (screenshot_pending) {
            Image img = LoadImageFromScreen();
            bool ok = ExportImage(img, screenshot_path);
            UnloadImage(img);
            ipc_screenshot_done(ok);
            screenshot_pending = false;
        }
    }

    if (keep_running) ipc_emit_event("QUIT");
    CloseWindow();
    return 0;
}
