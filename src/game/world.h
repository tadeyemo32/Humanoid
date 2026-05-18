#ifndef WORLD_H
#define WORLD_H

#include "state.h"

#define WALL_H 5.0f
#define AREA  24.0f

// Builds the static world geometry data and registers game objects in `state`.
void world_init(GameState *state);

// Draws all world geometry (must be called inside a 3D rendering pass).
// `anim_t` drives the key bob/rotation.
void world_draw(const GameState *state, float anim_t);

#endif
