/*
===============================================================================

	Touch input of the SDL backend

	In the game the screen carries on-screen controls (gameport touch_ui.md):

	- the left half is a floating stick: it appears where the finger lands
	  and moves the player like the left stick of a game controller does;
	- the right half is a camera pad: the finger turns the view through the
	  mouse queue, as a mouse would, so the mouse settings apply to it;
	- buttons press usercmd actions themselves (Sys_QueueUsercmdAction()),
	  never keys, so no binding can change what they do. The finger holding
	  fire, jump, crouch or zoom turns the camera as it slides. Crouch and
	  zoom are toggles, a tap on and the next one off;
	- quick save and quick load run the commands of their default keys, in
	  single player;
	- the menu button is Escape, which the engine wires in; it stays during
	  cinematics, which it skips, and reads SKIP then.

	Every finger is told apart by its SDL id: the stick, the camera pad and
	each button keep the finger that took them until it is lifted, so they
	all work at once. The layout is in millimetres of the screen, from the
	physical DPI, and stays off the edges, which belong to the system
	gestures of Aurora OS. The renderer draws the controls the layout
	describes (renderer/TouchOverlay.h), and touches are tested against the
	very same rectangles.

	In the menus and the ImGui settings one finger works the cursor like a
	trackpad: it moves the cursor by its own movement, and a tap, a touch
	that hardly moved, clicks where the cursor is. A drag never clicks.
	The intro videos the main menu plays at start have nothing to click: a
	tap anywhere skips them, whether the controls are on or not.

	Positions are in pixels of the content: the screen the way the viewer
	sees it. SDL reports fingers in 0..1 of the window, which on Aurora OS
	is turned against the content, so they go through the same turn as the
	picture.

===============================================================================
*/

#include <SDL.h>

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/Console.h"
#include "framework/Session_local.h"
#include "renderer/tr_local.h"
#include "ui/DeviceContext.h"
#include "ui/Window.h"
#include "ui/UserInterfaceLocal.h"

#include "sys/sys_public.h"
#include "sys/sdl/touch_ui.h"

#if SDL_VERSION_ATLEAST(2, 0, 10) // SDL_GetTouchDeviceType()

extern int GLimp_GetDisplayIndex(void);

static idCVar touch_ui("touch_ui", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
		"on-screen touch controls in the game: 0 off, 1 while the touchscreen is what is used, 2 always", 0, 2, idCmdSystem::ArgCompletion_Integer<0, 2>);
static idCVar touch_alpha("touch_alpha", "0.6", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
		"opacity of the on-screen touch controls", 0.1f, 1.0f);
static idCVar touch_buttonSize("touch_buttonSize", "11", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
		"size of the touch buttons, millimetres of the screen", 6.0f, 25.0f);
static idCVar touch_edgeMargin("touch_edgeMargin", "2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
		"distance of the touch controls from the edges of the screen, millimetres; the edges belong to the system gestures", 0.0f, 20.0f);
static idCVar touch_stickRadius("touch_stickRadius", "12", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
		"how far the touch stick goes to run at full speed, millimetres", 4.0f, 40.0f);
static idCVar touch_stickDeadZone("touch_stickDeadZone", "1.5", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
		"how far the touch stick goes before the player moves, millimetres", 0.0f, 10.0f);
static idCVar touch_lookSpeed("touch_lookSpeed", "32", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT,
		"camera pad speed, mouse units per millimetre the finger moves (sensitivity applies on top)", 1.0f, 200.0f);

// a tap in the menus moves less than this, in pixels of the screen; the
// launcher uses the same, tuned on the device
const float TOUCH_TAP_TRAVEL = 16.0f;

typedef enum {
	TOUCH_NONE,				// the touchscreen does nothing: the console, loading
	TOUCH_MENU,				// a menu takes the cursor
	TOUCH_IMGUI,			// the ImGui settings take the cursor
	TOUCH_GAME,				// the controls
	TOUCH_CINEMATIC			// only the menu button, which skips it
} touchContext_t;

const int TBF_LOOK		= BIT(0);	// the finger holding it also turns the camera
const int TBF_LATCH		= BIT(1);	// a tap holds the action down, the next lets go
const int TBF_CINEMATIC	= BIT(2);	// there during cinematics too
const int TBF_COMMAND	= BIT(3);	// the command is a console command, run on the tap
const int TBF_SINGLEPLAYER = BIT(4);	// not there in multiplayer

typedef struct {
	const char *	command;		// usercmd action, console command with TBF_COMMAND, NULL for Escape
	touchIcon_t		icon;
	int				flags;
	const char *	toggleCvar;		// TBF_LATCH: while set, the game toggles by itself

	int				action;			// what the command is to the usercmd generator
	float			x, y, w, h;		// pixels of the content
	bool			held;			// a finger is on it
	SDL_FingerID	finger;
	float			lastX, lastY;	// TBF_LOOK: where that finger was
	bool			down;			// the action is held for the finger
	bool			latched;		// TBF_LATCH: the action is held until the next tap
} touchButton_t;

enum {
	TB_MENU,
	TB_OBJECTIVES,
	TB_FLASHLIGHT,
	TB_PREVWEAPON,
	TB_NEXTWEAPON,
	TB_ZOOM,
	TB_RELOAD,
	TB_FIRE,
	TB_JUMP,
	TB_CROUCH,
	TB_QUICKSAVE,
	TB_QUICKLOAD,
	TB_COUNT
};

static touchButton_t touchButtons[TB_COUNT] = {
	{ NULL,				TOUCH_ICON_MENU,		TBF_CINEMATIC },
#ifdef _RAVEN
	{ "_showScores",	TOUCH_ICON_OBJECTIVES,	0 },			// objectives while held
	{ "_impulse50",		TOUCH_ICON_FLASHLIGHT,	0 },
#else
	{ "_impulse19",		TOUCH_ICON_OBJECTIVES,	0 },			// the PDA
	{ "_impulse11",		TOUCH_ICON_FLASHLIGHT,	0 },
#endif
	{ "_impulse15",		TOUCH_ICON_PREVWEAPON,	0 },
	{ "_impulse14",		TOUCH_ICON_NEXTWEAPON,	0 },
	{ "_zoom",			TOUCH_ICON_ZOOM,		TBF_LOOK | TBF_LATCH,		"in_toggleZoom" },
	{ "_impulse13",		TOUCH_ICON_RELOAD,		0 },
	{ "_attack",		TOUCH_ICON_FIRE,		TBF_LOOK },
	{ "_moveUp",		TOUCH_ICON_JUMP,		TBF_LOOK },
	{ "_moveDown",		TOUCH_ICON_CROUCH,		TBF_LOOK | TBF_LATCH,		"in_toggleCrouch" },
	// what the default config binds to F5 and F9
	{ "savegame quick",	TOUCH_ICON_QUICKSAVE,	TBF_COMMAND | TBF_SINGLEPLAYER },
	{ "loadgame quick",	TOUCH_ICON_QUICKLOAD,	TBF_COMMAND | TBF_SINGLEPLAYER }
};

static struct {
	bool			held;
	SDL_FingerID	finger;
	float			baseX, baseY;	// where the finger landed
	float			knobX, knobY;	// the finger, kept within the radius
	float			side, forward;	// -1..1
} touchStick;

static struct {
	bool			held;
	SDL_FingerID	finger;
	float			lastX, lastY;
} touchLook;

static struct {
	bool			held;
	SDL_FingerID	finger;
	float			lastX, lastY;
	float			travel;			// path length so far, tap or drag
	float			restX, restY;	// what is left of a cursor unit
} touchPointer;

static float touchLookRestX, touchLookRestY;	// what is left of a mouse unit

static bool touchScreen;		// SDL knows a touchscreen
static bool touchUsed = true;	// nothing but the touchscreen was used since it was touched last

static int touchLayoutWidth;
static int touchLayoutHeight;
static float touchPixelsPerMm = 96.0f / 25.4f;

static idList<sysEvent_t> touchEvents;

/*
=================
TouchUI_Context
=================
*/
static touchContext_t TouchUI_Context(void) {
#ifdef _IMGUI
	if (R_ImGui_IsRunning()) {
		return TOUCH_IMGUI;
	}
#endif

	if (console->Active()) {
		return TOUCH_NONE;
	}

	if (sessLocal.GetActiveMenu() != NULL) {
		return TOUCH_MENU;
	}

	if (!sessLocal.mapSpawned) {
		return TOUCH_NONE;
	}

#ifdef _RAVEN
	if (game && game->InCinematic()) {
		return TOUCH_CINEMATIC;
	}
#endif

	return TOUCH_GAME;
}

/*
=================
TouchUI_Enabled

Whether the controls are there at all. By default they are while a
touchscreen is what the player uses: a game controller, the keyboard or the
mouse make them step aside, the next touch brings them back.
=================
*/
static bool TouchUI_Enabled(void) {
	switch (touch_ui.GetInteger()) {
		case 0:
			return false;
		case 2:
			return true;
	}

	return touchScreen && touchUsed && !Sys_GamepadActive();
}

/*
=================
TouchUI_ButtonShown
=================
*/
static bool TouchUI_ButtonShown(const touchButton_t &button, touchContext_t context) {
	if ((button.flags & TBF_SINGLEPLAYER) && sessLocal.IsMultiplayer()) {
		return false;
	}

	if (context == TOUCH_GAME) {
		return true;
	}

	if (context == TOUCH_CINEMATIC) {
		return (button.flags & TBF_CINEMATIC) != 0;
	}

	return false;
}

/*
=================
TouchUI_ButtonIcon

In a cinematic the menu button is what skips it, and says so
=================
*/
static touchIcon_t TouchUI_ButtonIcon(int index, touchContext_t context) {
	if (index == TB_MENU && context == TOUCH_CINEMATIC) {
		return TOUCH_ICON_SKIP;
	}

	return touchButtons[index].icon;
}

/*
=================
TouchUI_ButtonRect

Where a button is drawn and touched. One that shows a word is a capsule as
wide as the word needs, from the left edge of its place.
=================
*/
static void TouchUI_ButtonRect(int index, touchContext_t context, float &x, float &y, float &w, float &h) {
	const touchButton_t &button = touchButtons[index];
	const char *text = TouchOverlay_IconText(TouchUI_ButtonIcon(index, context));

	x = button.x;
	y = button.y;
	w = text ? TouchOverlay_CapsuleWidth(text, button.h) : button.w;
	h = button.h;
}

/*
=================
TouchUI_HasTouchscreen

Only a screen touched directly: a touchpad sends finger events too, but it
is a mouse, not something to put controls on
=================
*/
static bool TouchUI_HasTouchscreen(void) {
	for (int i = 0; i < SDL_GetNumTouchDevices(); i++) {
		if (SDL_GetTouchDeviceType(SDL_GetTouchDevice(i)) == SDL_TOUCH_DEVICE_DIRECT) {
			return true;
		}
	}

	return false;
}

/*
=================
TouchUI_PixelsPerMm

From the physical DPI of the display the window is on. The diagonal DPI
comes first: it stays right even where a driver reports the width and the
height of the panel the other way round.
=================
*/
static float TouchUI_PixelsPerMm(void) {
	float ddpi = 0.0f;
	float hdpi = 0.0f;
	float vdpi = 0.0f;
	float dpi = 0.0f;
	int display = GLimp_GetDisplayIndex();

	if (SDL_GetDisplayDPI(display >= 0 ? display : 0, &ddpi, &hdpi, &vdpi) == 0) {
		dpi = ddpi > 1.0f ? ddpi : vdpi;
	}

	if (dpi <= 1.0f) {
		dpi = 96.0f;
	}

	return idMath::ClampFloat(2.0f, 40.0f, dpi / 25.4f);
}

/*
=================
TouchUI_ContentSize
=================
*/
static void TouchUI_ContentSize(int &width, int &height) {
#ifdef _AURORA_FBO
	if (auroraFramebuffer.IsActive()) {
		width = auroraFramebuffer.ContentWidth();
		height = auroraFramebuffer.ContentHeight();
		return;
	}
#endif

	width = glConfig.vidWidth;
	height = glConfig.vidHeight;
}

/*
=================
TouchUI_ContentPoint

A finger, in 0..1 of the window, in pixels of the content
=================
*/
static void TouchUI_ContentPoint(float x, float y, float &outX, float &outY) {
	x = idMath::ClampFloat(0.0f, 1.0f, x);
	y = idMath::ClampFloat(0.0f, 1.0f, y);

#ifdef _AURORA_FBO
	if (auroraFramebuffer.IsActive()) {
		auroraFramebuffer.WindowToContent(x, y, &x, &y);
	}
#endif

	outX = x * touchLayoutWidth;
	outY = y * touchLayoutHeight;
}

/*
=================
TouchUI_PlaceButton
=================
*/
static void TouchUI_PlaceButton(int index, float x, float y, float size_w, float size_h = 0) {
	touchButton_t &button = touchButtons[index];

	button.x = x * touchPixelsPerMm;
	button.y = y * touchPixelsPerMm;
	if ( int(size_h * 10) == 0 )
		size_h = size_w;
	button.w = size_w * touchPixelsPerMm;
	button.h = size_h * touchPixelsPerMm;
}

/*
=================
TouchUI_Layout

In millimetres of the screen, landscape. The right thumb has fire in the
middle of a cluster in the lower right corner: jump and crouch beside it
against the edge, zoom above and reload before it. The weapons and the
flashlight take the upper right corner, the menu and the objectives the
upper left one, where nothing else is. Everything else is the stick on the
left half and the camera pad on the right half.
=================
*/
static void TouchUI_Layout(void) {
	int width;
	int height;

	TouchUI_ContentSize(width, height);

	if (width == touchLayoutWidth && height == touchLayoutHeight && !touch_buttonSize.IsModified() && !touch_edgeMargin.IsModified()) {
		return;
	}

	touch_buttonSize.ClearModified();
	touch_edgeMargin.ClearModified();

	touchLayoutWidth = width;
	touchLayoutHeight = height;
	touchPixelsPerMm = TouchUI_PixelsPerMm();

	const float screenWidth = width / touchPixelsPerMm;
	const float screenHeight = height / touchPixelsPerMm;
	const float margin = touch_edgeMargin.GetFloat();
	const float size = touch_buttonSize.GetFloat();
	const float small = size * 0.85f;
	const float big = size * 1.45f;
	const float gap = size * 0.3f;

	const float right = screenWidth - margin;
	const float bottom = screenHeight - margin;

	TouchUI_PlaceButton(TB_MENU, margin, margin, small);
	TouchUI_PlaceButton(TB_OBJECTIVES, margin + small + gap, margin, small);

	// quick save and quick load side by side in the top centre, capsules as
	// wide as their words
	const float saveWidth = TouchOverlay_CapsuleWidth(TouchOverlay_IconText(TOUCH_ICON_QUICKSAVE), small);
	const float loadWidth = TouchOverlay_CapsuleWidth(TouchOverlay_IconText(TOUCH_ICON_QUICKLOAD), small);

	TouchUI_PlaceButton(TB_QUICKSAVE, screenWidth * 0.5f - gap * 0.5f - saveWidth, margin, saveWidth, small);
	TouchUI_PlaceButton(TB_QUICKLOAD, screenWidth * 0.5f + gap * 0.5f, margin, loadWidth, small);

	TouchUI_PlaceButton(TB_NEXTWEAPON, screenWidth * 0.5 + small * 1.5, screenHeight - small -margin * 2, small);
	TouchUI_PlaceButton(TB_PREVWEAPON, screenWidth * 0.5 - small * 0.5, screenHeight - small -margin * 2, small);
	TouchUI_PlaceButton(TB_FLASHLIGHT, right - small - margin, margin, small);

	TouchUI_PlaceButton(TB_CROUCH, right - size, bottom - size, size);
	TouchUI_PlaceButton(TB_JUMP, right - size, bottom - 2.0f * size - gap, size);

	// fire between jump and crouch, zoom above it, reload before it
	const float fireX = right - size * 2 - gap - big;
	const float fireY = bottom - 45.0f;

	TouchUI_PlaceButton(TB_FIRE, fireX, fireY, big);
	TouchUI_PlaceButton(TB_RELOAD, fireX + (big - gap) * 0.5f, fireY - gap - size, size);
	TouchUI_PlaceButton(TB_ZOOM, right - size, fireY - size * 0.5, size);
}

/*
=================
TouchUI_Key
=================
*/
static void TouchUI_Key(int key, bool down) {
	sysEvent_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.evType = SE_KEY;
	ev.evValue = key;
	ev.evValue2 = down ? 1 : 0;
	touchEvents.Append(ev);
}

/*
=================
TouchUI_MouseEvent
=================
*/
static void TouchUI_MouseEvent(int dx, int dy) {
	sysEvent_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.evType = SE_MOUSE;
	ev.evValue = dx;
	ev.evValue2 = dy;
	touchEvents.Append(ev);
}

/*
=================
TouchUI_Action
=================
*/
static void TouchUI_Action(const touchButton_t &button, bool down) {
	if (button.action > 0) {
		Sys_QueueUsercmdAction(button.action, down);
	}
}

/*
=================
TouchUI_Unlatch
=================
*/
static void TouchUI_Unlatch(touchButton_t &button) {
	if (button.latched) {
		button.latched = false;
		TouchUI_Action(button, false);
	}
}

/*
=================
TouchUI_Press
=================
*/
static void TouchUI_Press(touchButton_t &button, SDL_FingerID finger, float x, float y) {
	button.held = true;
	button.finger = finger;
	button.lastX = x;
	button.lastY = y;

	if (!button.command) {
		TouchUI_Key(K_ESCAPE, true);
		return;
	}

	if (button.flags & TBF_COMMAND) {
		cmdSystem->BufferCommandText(CMD_EXEC_APPEND, va("%s\n", button.command));
		return;
	}

	// crouch and zoom stay on until tapped again, unless the player asked the
	// game itself to toggle them: then a tap is a press like a key's
	if ((button.flags & TBF_LATCH) && !(button.toggleCvar && cvarSystem->GetCVarBool(button.toggleCvar))) {
		if (button.latched) {
			TouchUI_Unlatch(button);
		} else {
			button.latched = true;
			TouchUI_Action(button, true);
		}
		return;
	}

	// jumping gets up from a crouch
	if (&button == &touchButtons[TB_JUMP]) {
		TouchUI_Unlatch(touchButtons[TB_CROUCH]);
	}

	button.down = true;
	TouchUI_Action(button, true);
}

/*
=================
TouchUI_LetGo

The finger is lifted; a latched action stays
=================
*/
static void TouchUI_LetGo(touchButton_t &button) {
	if (!button.held) {
		return;
	}

	button.held = false;

	if (!button.command) {
		TouchUI_Key(K_ESCAPE, false);
		return;
	}

	if (button.down) {
		button.down = false;
		TouchUI_Action(button, false);
	}
}

/*
=================
TouchUI_Release
=================
*/
static void TouchUI_Release(touchButton_t &button) {
	TouchUI_LetGo(button);
	TouchUI_Unlatch(button);
}

/*
=================
TouchUI_StickMove
=================
*/
static void TouchUI_StickMove(float x, float y) {
	const float radius = touch_stickRadius.GetFloat() * touchPixelsPerMm;
	const float deadZone = Min(touch_stickDeadZone.GetFloat(), touch_stickRadius.GetFloat() * 0.5f) * touchPixelsPerMm;

	float dx = x - touchStick.baseX;
	float dy = y - touchStick.baseY;
	float length = idMath::Sqrt(dx * dx + dy * dy);

	if (length > radius) {
		dx *= radius / length;
		dy *= radius / length;
		length = radius;
	}

	touchStick.knobX = touchStick.baseX + dx;
	touchStick.knobY = touchStick.baseY + dy;

	if (length <= deadZone) {
		touchStick.side = touchStick.forward = 0.0f;
		return;
	}

	// from zero at the edge of the dead zone to full at the radius
	const float scale = (length - deadZone) / (radius - deadZone) / length;

	touchStick.side = dx * scale;
	touchStick.forward = -dy * scale;
}

/*
=================
TouchUI_StickStop
=================
*/
static void TouchUI_StickStop(void) {
	touchStick.held = false;
	touchStick.side = touchStick.forward = 0.0f;
}

/*
=================
TouchUI_Look

The camera turns by mouse moves, touch_lookSpeed mouse units for every
millimetre the finger moves
=================
*/
static void TouchUI_Look(float &lastX, float &lastY, float x, float y) {
	const float scale = touch_lookSpeed.GetFloat() / touchPixelsPerMm;

	touchLookRestX += (x - lastX) * scale;
	touchLookRestY += (y - lastY) * scale;
	lastX = x;
	lastY = y;

	const int dx = (int)touchLookRestX;
	const int dy = (int)touchLookRestY;

	touchLookRestX -= dx;
	touchLookRestY -= dy;

	if (dx || dy) {
		Sys_QueueMouseMove(dx, dy);
	}
}

/*
=================
TouchUI_PointerMove

The finger moves the cursor as far as it moved itself on the screen. The
ImGui cursor counts pixels of the frame; a menu counts its 640x480 units
straight from the mouse moves, unless r_scaleMenusTo43 has it divide them by
the width of the frame in its 4:3 part. Either way the cursor stays under
the path of the finger, whatever the resolution and the render scale.
=================
*/
static void TouchUI_PointerMove(float x, float y, touchContext_t context) {
	const float dx = x - touchPointer.lastX;
	const float dy = y - touchPointer.lastY;

	touchPointer.travel += idMath::Sqrt(dx * dx + dy * dy);
	touchPointer.lastX = x;
	touchPointer.lastY = y;

	float scaleX;
	float scaleY;

	if (context == TOUCH_IMGUI || (context == TOUCH_MENU && cvarSystem->GetCVarBool("r_scaleMenusTo43"))) {
		scaleX = (float)glConfig.vidWidth / touchLayoutWidth;
		scaleY = (float)glConfig.vidHeight / touchLayoutHeight;
	} else if (context == TOUCH_MENU) {
		scaleX = (float)SCREEN_WIDTH / touchLayoutWidth;
		scaleY = (float)SCREEN_HEIGHT / touchLayoutHeight;
	} else {
		// what the finger worked is gone
		return;
	}

	touchPointer.restX += dx * scaleX;
	touchPointer.restY += dy * scaleY;

	const int mx = (int)touchPointer.restX;
	const int my = (int)touchPointer.restY;

	touchPointer.restX -= mx;
	touchPointer.restY -= my;

	if (mx || my) {
		TouchUI_MouseEvent(mx, my);
	}
}

/*
=================
TouchUI_MenuPlaysIntro

The main menu hides its cursor while it plays the intro videos, and only
Escape skips them, one at a time
=================
*/
static bool TouchUI_MenuPlaysIntro(void) {
	if (!sessLocal.guiMainMenu || sessLocal.guiActive != sessLocal.guiMainMenu) {
		return false;
	}

	idWindow *desktop = static_cast<idUserInterfaceLocal *>(sessLocal.guiMainMenu)->GetDesktop();

	return desktop && (desktop->GetFlags() & WIN_NOCURSOR);
}

/*
=================
TouchUI_PointerUp

A tap clicks where the cursor is, a drag doesn't. Only key events: in the
menus the mouse button must not reach the usercmd generator, or resuming the
game with a tap would fire.

The intro videos have nothing to click, a tap skips them with Escape, the
way the main menu skips them.
=================
*/
static void TouchUI_PointerUp(touchContext_t context) {
	touchPointer.held = false;

	if (touchPointer.travel >= TOUCH_TAP_TRAVEL) {
		return;
	}

	if (context == TOUCH_MENU && TouchUI_MenuPlaysIntro()) {
		TouchUI_Key(K_ESCAPE, true);
		TouchUI_Key(K_ESCAPE, false);
	} else if (context == TOUCH_MENU || context == TOUCH_IMGUI) {
		TouchUI_Key(K_MOUSE1, true);
		TouchUI_Key(K_MOUSE1, false);
	}
}

/*
=================
TouchUI_PointerDown

One finger works the cursor, the others are ignored
=================
*/
static void TouchUI_PointerDown(SDL_FingerID finger, float x, float y) {
	if (touchPointer.held) {
		return;
	}

	touchPointer.held = true;
	touchPointer.finger = finger;
	touchPointer.lastX = x;
	touchPointer.lastY = y;
	touchPointer.travel = 0.0f;
	touchPointer.restX = touchPointer.restY = 0.0f;
}

/*
=================
TouchUI_FingerDown
=================
*/
static void TouchUI_FingerDown(SDL_FingerID finger, float x, float y, touchContext_t context) {
	if (context == TOUCH_MENU || context == TOUCH_IMGUI) {
		TouchUI_PointerDown(finger, x, y);
		return;
	}

	if (!TouchUI_Enabled() || (context != TOUCH_GAME && context != TOUCH_CINEMATIC)) {
		return;
	}

	// the buttons come before the stick and the camera
	for (int i = 0; i < TB_COUNT; i++) {
		touchButton_t &button = touchButtons[i];

		if (!TouchUI_ButtonShown(button, context)) {
			continue;
		}

		float bx, by, bw, bh;
		TouchUI_ButtonRect(i, context, bx, by, bw, bh);

		if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
			// another finger holding it keeps it
			if (!button.held) {
				TouchUI_Press(button, finger, x, y);
			}
			return;
		}
	}

	if (context != TOUCH_GAME) {
		return;
	}

	const float margin = touch_edgeMargin.GetFloat() * touchPixelsPerMm;

	if (y < margin || y > touchLayoutHeight - margin || x < margin || x > touchLayoutWidth - margin) {
		return;
	}

	if (x < touchLayoutWidth * 0.5f) {
		if (!touchStick.held) {
			touchStick.held = true;
			touchStick.finger = finger;
			touchStick.baseX = touchStick.knobX = x;
			touchStick.baseY = touchStick.knobY = y;
			touchStick.side = touchStick.forward = 0.0f;
		}
	} else {
		if (!touchLook.held) {
			touchLook.held = true;
			touchLook.finger = finger;
			touchLook.lastX = x;
			touchLook.lastY = y;
		}
	}
}

/*
=================
TouchUI_FingerMotion
=================
*/
static void TouchUI_FingerMotion(SDL_FingerID finger, float x, float y, touchContext_t context) {
	if (touchPointer.held && touchPointer.finger == finger) {
		TouchUI_PointerMove(x, y, context);
		return;
	}

	if (touchStick.held && touchStick.finger == finger) {
		TouchUI_StickMove(x, y);
		return;
	}

	if (touchLook.held && touchLook.finger == finger) {
		TouchUI_Look(touchLook.lastX, touchLook.lastY, x, y);
		return;
	}

	for (int i = 0; i < TB_COUNT; i++) {
		touchButton_t &button = touchButtons[i];

		if (button.held && button.finger == finger) {
			// wherever it slides to, the button stays held
			if (button.flags & TBF_LOOK) {
				TouchUI_Look(button.lastX, button.lastY, x, y);
			}
			return;
		}
	}
}

/*
=================
TouchUI_FingerUp
=================
*/
static void TouchUI_FingerUp(SDL_FingerID finger, touchContext_t context) {
	if (touchPointer.held && touchPointer.finger == finger) {
		TouchUI_PointerUp(context);
	}

	if (touchStick.held && touchStick.finger == finger) {
		TouchUI_StickStop();
	}

	if (touchLook.held && touchLook.finger == finger) {
		touchLook.held = false;
	}

	for (int i = 0; i < TB_COUNT; i++) {
		if (touchButtons[i].held && touchButtons[i].finger == finger) {
			TouchUI_LetGo(touchButtons[i]);
		}
	}
}

/*
=================
TouchUI_FingerEvent
=================
*/
void TouchUI_FingerEvent(const SDL_Event *ev) {
	const SDL_TouchFingerEvent &finger = ev->tfinger;

	if (SDL_GetTouchDeviceType(finger.touchId) != SDL_TOUCH_DEVICE_DIRECT) {
		return;
	}

	touchScreen = true;
	TouchUI_Layout();

	if (touchLayoutWidth <= 0 || touchLayoutHeight <= 0) {
		return;
	}

	float x;
	float y;

	TouchUI_ContentPoint(finger.x, finger.y, x, y);

	const touchContext_t context = TouchUI_Context();

	switch (ev->type) {
		case SDL_FINGERDOWN:
			touchUsed = true;
			TouchUI_FingerDown(finger.fingerId, x, y, context);
			break;

		case SDL_FINGERMOTION:
			TouchUI_FingerMotion(finger.fingerId, x, y, context);
			break;

		case SDL_FINGERUP:
			TouchUI_FingerUp(finger.fingerId, context);
			break;
	}
}

/*
=================
TouchUI_Overlay

What the renderer draws over the frame it is building
=================
*/
static bool TouchUI_Overlay(touchOverlay_t *overlay) {
	if (!TouchUI_Enabled()) {
		return false;
	}

	const touchContext_t context = TouchUI_Context();

	if (context != TOUCH_GAME && context != TOUCH_CINEMATIC) {
		return false;
	}

	TouchUI_Layout();

	if (touchLayoutWidth <= 0 || touchLayoutHeight <= 0) {
		return false;
	}

	overlay->width = touchLayoutWidth;
	overlay->height = touchLayoutHeight;
	overlay->pixelsPerMm = touchPixelsPerMm;
	overlay->alpha = touch_alpha.GetFloat();
	overlay->numButtons = 0;

	for (int i = 0; i < TB_COUNT && overlay->numButtons < TOUCH_OVERLAY_MAX_BUTTONS; i++) {
		const touchButton_t &button = touchButtons[i];

		if (!TouchUI_ButtonShown(button, context)) {
			continue;
		}

		touchOverlayButton_t &drawn = overlay->buttons[overlay->numButtons++];
		TouchUI_ButtonRect(i, context, drawn.x, drawn.y, drawn.w, drawn.h);
		drawn.icon = TouchUI_ButtonIcon(i, context);
		drawn.pressed = button.held || button.latched;
	}

	overlay->stick = touchStick.held && context == TOUCH_GAME;
	overlay->stickX = touchStick.baseX;
	overlay->stickY = touchStick.baseY;
	overlay->knobX = touchStick.knobX;
	overlay->knobY = touchStick.knobY;
	overlay->stickRadius = touch_stickRadius.GetFloat() * touchPixelsPerMm;

	return true;
}

/*
=================
TouchUI_Frame

Whatever is no longer on the screen lets go: a menu opened, the console came
down, a cinematic started, a controller was connected, the keyboard was used
=================
*/
void TouchUI_Frame(void) {
	touchScreen = touchScreen || TouchUI_HasTouchscreen();

	TouchUI_Layout();

	const bool enabled = TouchUI_Enabled();
	const touchContext_t context = TouchUI_Context();

	if (!enabled || context != TOUCH_GAME) {
		TouchUI_StickStop();
		touchLook.held = false;
	}

	for (int i = 0; i < TB_COUNT; i++) {
		if (!enabled || !TouchUI_ButtonShown(touchButtons[i], context)) {
			TouchUI_Release(touchButtons[i]);
		}
	}
}

/*
=================
TouchUI_Reset
=================
*/
void TouchUI_Reset(void) {
	touchEvents.Clear();

	for (int i = 0; i < TB_COUNT; i++) {
		TouchUI_Release(touchButtons[i]);
	}

	TouchUI_StickStop();
	touchLook.held = false;
	touchPointer.held = false;
}

/*
=================
TouchUI_OtherInput
=================
*/
void TouchUI_OtherInput(void) {
	touchUsed = false;
}

/*
=================
TouchUI_NextEvent
=================
*/
bool TouchUI_NextEvent(sysEvent_t &ev) {
	if (!touchEvents.Num()) {
		return false;
	}

	ev = touchEvents[0];
	touchEvents.RemoveIndex(0);
	return true;
}

/*
=================
TouchUI_JoystickAxis
=================
*/
float TouchUI_JoystickAxis(int axis) {
	if (!touchStick.held) {
		return 0.0f;
	}

	switch (axis) {
		case AXIS_SIDE:
			return touchStick.side;
		case AXIS_FORWARD:
			return touchStick.forward;
	}

	return 0.0f;
}

/*
=================
TouchUI_Init
=================
*/
void TouchUI_Init(void) {
	touchEvents.SetGranularity(16);

	for (int i = 0; i < TB_COUNT; i++) {
		touchButton_t &button = touchButtons[i];
		button.action = (button.command && !(button.flags & TBF_COMMAND)) ? usercmdGen->CommandStringUsercmdData(button.command) : 0;
	}

	// laid out anew for whatever screen there is now
	touchLayoutWidth = touchLayoutHeight = 0;

	R_SetTouchOverlaySource(TouchUI_Overlay);
}

/*
=================
TouchUI_Shutdown
=================
*/
void TouchUI_Shutdown(void) {
	R_SetTouchOverlaySource(NULL);

	TouchUI_Reset();
	touchEvents.Clear();
}

#else // no touch devices before SDL 2.0.10

void TouchUI_Init(void) {
}

void TouchUI_Shutdown(void) {
}

void TouchUI_Frame(void) {
}

void TouchUI_Reset(void) {
}

void TouchUI_FingerEvent(const SDL_Event *ev) {
}

void TouchUI_OtherInput(void) {
}

bool TouchUI_NextEvent(sysEvent_t &ev) {
	return false;
}

float TouchUI_JoystickAxis(int axis) {
	return 0.0f;
}

#endif
