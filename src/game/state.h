#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include "raylib.h"

#define MAX_OBJECTS    16
#define MAX_INVENTORY   8
#define MAX_EVENTS     32
#define MAX_NAME       48
#define MAX_EVENT_LEN 128
#define MAX_GOAL_LEN  512
#define MAX_STATUS    256

typedef struct {
    char     name[MAX_NAME];
    Vector3  position;
    bool     pickable;
    bool     interactable;
    bool     locked;      // gate
    bool     pulled;      // lever
    bool     read;        // signpost
    bool     visible;     // false hides + skips rendering
    bool     exists;      // false means slot empty
} GameObject;

typedef struct {
    GameObject objects[MAX_OBJECTS];
    int        object_count;

    char       inventory[MAX_INVENTORY][MAX_NAME];
    int        inv_count;

    Vector3    char_pos;
    float      char_rot;          // degrees; 0 = +Z (north)
    bool       goal_complete;
    int        crystals_collected;
    int        crystals_total;

    char       events[MAX_EVENTS][MAX_EVENT_LEN];
    int        event_count;       // total ever; ring buffer

    char       goal[MAX_GOAL_LEN];
    char       status[MAX_STATUS];
    int        step;
    bool       cv_active;

    // Manual/auto mode + in-window input box + CAM speech bubble
    bool       manual_mode;
    char       input_buffer[256];
    int        input_len;
    char       bubble[512];
    float      bubble_timer;     // seconds remaining visible
    char       agent_name[16];   // "CAM"
} GameState;

void  state_init(GameState *s);
void  state_log(GameState *s, const char *msg);
int   state_find_object(const GameState *s, const char *query);
bool  state_in_inventory(const GameState *s, const char *name);
void  state_add_inventory(GameState *s, const char *name);
void  state_to_json(const GameState *s, char *out, int out_size);

#endif
