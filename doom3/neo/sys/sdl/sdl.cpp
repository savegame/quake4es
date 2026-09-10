#include <SDL.h>

#include "../../idlib/precompiled.h"
#include "../sys_public.h"

extern void Sys_InitThreads();

void Sys_InitSDL(void)
{
    // the game controller subsystem brings up the joystick one as well, which
    // is also needed to work around SDL 2.0.9 bug #4391. The controller
    // mappings are loaded later, in Sys_InitInput(), once fs_savepath is known
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER))
        Sys_Error("Error while initializing SDL: %s", SDL_GetError());

    Sys_InitThreads();
}