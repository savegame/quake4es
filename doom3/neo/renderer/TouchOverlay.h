#ifndef __TOUCHOVERLAY_H__
#define __TOUCHOVERLAY_H__

/*
===============================================================================

	On-screen touch controls, the way the renderer sees them

	The system lays the controls out and knows what is held; the renderer
	only draws them, over the finished frame and at the resolution of the
	screen, so that a scene rendered smaller than the screen doesn't blur
	them. Every coordinate is in pixels of the content: the screen the way
	the viewer sees it, which on Aurora OS is the window turned by the
	display transform.

===============================================================================
*/

typedef enum {
	TOUCH_ICON_MENU,
	TOUCH_ICON_FIRE,
	TOUCH_ICON_JUMP,
	TOUCH_ICON_CROUCH,
	TOUCH_ICON_ZOOM,
	TOUCH_ICON_RELOAD,
	TOUCH_ICON_PREVWEAPON,
	TOUCH_ICON_NEXTWEAPON,
	TOUCH_ICON_FLASHLIGHT,
	TOUCH_ICON_OBJECTIVES,
	TOUCH_ICON_SKIP // the menu button while a cinematic plays
} touchIcon_t;

typedef struct {
	float				x, y, w, h; // the very rectangle touches are tested against
	touchIcon_t			icon;
	bool				pressed;
} touchOverlayButton_t;

const int TOUCH_OVERLAY_MAX_BUTTONS = 16;

typedef struct touchOverlay_s {
	int					width;			// content size the layout was made for
	int					height;
	float				pixelsPerMm;
	float				alpha;			// opacity of the whole overlay

	int					numButtons;
	touchOverlayButton_t buttons[TOUCH_OVERLAY_MAX_BUTTONS];

	bool				stick;			// the stick is held
	float				stickX, stickY;	// where it was touched
	float				knobX, knobY;	// where the finger holds the knob
	float				stickRadius;	// how far the knob goes
} touchOverlay_t;

// fills in the overlay for the frame being built, false for none
typedef bool (*touchOverlaySource_t)(touchOverlay_t *overlay);

// front end: where EndFrame() takes the overlay of each frame from, NULL
// for none. The source is asked while the frame is being built, so what is
// drawn always matches the state of that frame, a loading screen included
void	R_SetTouchOverlaySource(touchOverlaySource_t source);

// back end: draws over the real backbuffer, with the frame already on it
void	RB_DrawTouchOverlay(const touchOverlay_t *overlay);
void	RB_ShutdownTouchOverlay(void);

#endif /* !__TOUCHOVERLAY_H__ */
