#ifndef IPC_H
#define IPC_H

#include <stdbool.h>
#include "state.h"
#include "character.h"

void ipc_init(void);

// Process all pending command lines on stdin. Returns false if QUIT was received.
// `screenshot_path` (out) — if a SCREENSHOT command was received, writes the
//   requested path into the buffer and sets `*screenshot_pending = true`.
//   The caller must capture the framebuffer, write it to that path, and call
//   ipc_screenshot_done() to release the blocked caller.
bool ipc_poll(GameState *s, Character *c,
              bool *screenshot_pending, char *screenshot_path, int path_size);

void ipc_screenshot_done(bool ok);

// Emit an asynchronous event line ("EVT ...\n") to Python. Safe to call
// from the render thread between IPC commands.
void ipc_emit_event(const char *fmt, ...);

#endif
