#ifndef CHARACTER_H
#define CHARACTER_H

#include <stdbool.h>
#include "state.h"

#define PICKUP_RANGE    3.2f
#define INTERACT_RANGE  4.2f

typedef struct {
    bool    nav_active;
    float   target_x, target_z;
    int     nav_target_idx;   // last world object targeted by NAV, or -1
} Character;

void  character_init(Character *c, GameState *s);
void  character_set_target(Character *c, float x, float z);
void  character_stop(Character *c);
void  character_update(Character *c, GameState *s, float dt);
void  character_draw(const GameState *s);

// Returns object index or -1.
int   character_nearest_pickable(const Character *c, const GameState *s);
int   character_nearest_interactable(const Character *c, const GameState *s);

#endif
