#pragma once
// gfx.cpp <-> c3d.cpp
#include <3ds.h>
#include <memory>

void portWaitVBlank();        // block until the next 60 Hz tick
u32 portVBlankCount();
// After C3D_FrameEnd queued transfers into the screens' back buffers:
// swap the screens that received one and queue a presentation.
void portFrameEndPresent(bool top, bool bottom);
// Fast path: the screen shows its GPU snapshot of the frame (no GPU->CPU
// readback). Cleared by any CPU-side swap of that screen.
// Returns false when MKW_READBACK=1 asks for the old copy-through-RAM path.
bool portSetDirect(int screen);
namespace c3d { void flushRecorded(); }
