#ifndef __SYS_SDL_TOUCH_UI_H__
#define __SYS_SDL_TOUCH_UI_H__

/*
===============================================================================

	Touch input of the SDL backend, sys/sdl/touch_ui.cpp

	In the game: on-screen controls, a floating stick on the left half of the
	screen, a camera pad on the right half and buttons that press usercmd
	actions themselves. The renderer draws them (renderer/TouchOverlay.h).
	In the menus and the ImGui settings: one finger works the cursor like a
	trackpad, a tap clicks.

===============================================================================
*/

union SDL_Event;

// called by events.cpp
void		TouchUI_Init(void);
void		TouchUI_Shutdown(void);

// once a frame, from Sys_GenerateEvents()
void		TouchUI_Frame(void);

// the input was thrown away, or the window lost the focus: whatever the
// fingers held is let go, without clicking or pressing anything new
void		TouchUI_Reset(void);

// SDL_FINGERDOWN, SDL_FINGERMOTION and SDL_FINGERUP
void		TouchUI_FingerEvent(const SDL_Event *ev);

// the keyboard or the mouse was used: the controls get out of the way
// until the screen is touched again
void		TouchUI_OtherInput(void);

// events for the engine: Escape from the menu button, the cursor moves and
// clicks of a finger in the menus
bool		TouchUI_NextEvent(sysEvent_t &ev);

// the stick, by joystickAxis_t, -1.0 .. 1.0
float		TouchUI_JoystickAxis(int axis);

// provided by events.cpp: the queues the usercmd generator reads
void		Sys_QueueMouseMove(int dx, int dy);
void		Sys_QueueUsercmdAction(int action, bool down);

#endif /* !__SYS_SDL_TOUCH_UI_H__ */
