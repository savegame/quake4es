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
	TOUCH_ICON_SKIP, // the menu button while a cinematic plays
	TOUCH_ICON_QUICKSAVE,
	TOUCH_ICON_QUICKLOAD,

	TOUCH_ICON_COUNT
} touchIcon_t;

typedef struct {
	float				x, y, w, h; // the very rectangle touches are tested against
	touchIcon_t			icon;
	bool				pressed;
} touchOverlayButton_t;

const int TOUCH_OVERLAY_MAX_BUTTONS = 16;

// A button that shows a word instead of an icon is a capsule as wide as the
// word needs, the word written in the launcher's Noto Sans Bold. The system
// lays the capsule out and hit-tests it with these, the renderer draws it
// with them: the capsule is the rectangle of the button, and the word is
// fitted into it.
const float TOUCH_FONT_SIZE		= 0.5f;		// pixel size of the font, in heights of the button
const float TOUCH_TEXT_PADDING	= 0.45f;	// at either end, in heights of the button

// the word a button shows, NULL for an icon
ID_INLINE const char *TouchOverlay_IconText(touchIcon_t icon)
{
	switch (icon) {
		case TOUCH_ICON_SKIP:
			return "SKIP";
		case TOUCH_ICON_QUICKSAVE:
			return "SAVE";
		case TOUCH_ICON_QUICKLOAD:
			return "LOAD";
		default:
			return NULL;
	}
}

// width of a word in capitals at a font pixel size, erring on the wide side:
// a capital of Noto Sans Bold is at most about half the size wide, an I a
// quarter of it
ID_INLINE float TouchOverlay_TextWidth(const char *text, float size)
{
	float width = 0.0f;

	for (int i = 0; text[i]; i++) {
		width += (text[i] == 'I') ? 0.25f * size : 0.5f * size;
	}

	return width;
}

// width of the capsule of a word on a button height high
ID_INLINE float TouchOverlay_CapsuleWidth(const char *text, float height)
{
	return TouchOverlay_TextWidth(text, TOUCH_FONT_SIZE * height) + 2.0f * TOUCH_TEXT_PADDING * height;
}

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
