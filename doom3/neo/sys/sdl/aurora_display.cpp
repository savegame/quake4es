// Included from sys/sdl/glimp.cpp, which owns the SDL window and already
// pulls in SDL_syswm.h.

#ifdef _AURORA_FBO

#ifdef _AURORA
#include <SDL_syswm.h>
#include <wayland-client.h>
#endif

// Quake 4 is a landscape game: portrait orientations are mapped onto the
// nearest landscape one rather than shown upright.
//
// On a portrait panel the buffer transform is therefore always 90 or 270,
// never 0 or 180, which is why the buffer keeps its size when the device is
// turned: both of those swap the sides of the window.
static bool auroraPanelIsPortrait = true;
static int auroraDisplayIndex = -1;

/*
====================
Aurora_DetectPanel

The shape of the panel comes from the native resolution of the display the
window is on: taller than wide is a phone, wider than tall is a tablet, a
monitor or a TV. Worked out once and again only when the window moves to
another display, never per frame: SDL already caches what the compositor
reported.
====================
*/
static void Aurora_DetectPanel(void)
{
	SDL_DisplayMode mode;

	if (!window) {
		return;
	}

	const int index = SDL_GetWindowDisplayIndex(window);

	if (index < 0) {
		common->Warning("[Aurora display]: SDL_GetWindowDisplayIndex failed: %s", SDL_GetError());
		return;
	}

	auroraDisplayIndex = index;

	if (SDL_GetCurrentDisplayMode(index, &mode) != 0) {
		common->Warning("[Aurora display]: SDL_GetCurrentDisplayMode failed: %s", SDL_GetError());
		return;
	}

	auroraPanelIsPortrait = mode.h > mode.w;

	common->Printf("[Aurora display]: display %d is %d x %d, %s panel\n",
	               index, mode.w, mode.h, auroraPanelIsPortrait ? "portrait" : "landscape");
}

/*
====================
Aurora_TransformForOrientation

Both observable values, the orientation SDL reports and the shape of the
panel, decide the buffer transform of a landscape game:

    orientation           portrait panel   landscape panel
    landscape                  90                0
    landscape flipped         270              180
    portrait                  270              180
    portrait flipped           90                0

SDL has already read wl_output::transform for us, taking the display into
account, so it is not interpreted a second time here.
====================
*/
static auroraTransform_t Aurora_TransformForOrientation(SDL_DisplayOrientation orientation)
{
	bool inverted;

	switch (orientation) {
		case SDL_ORIENTATION_LANDSCAPE_FLIPPED:
		case SDL_ORIENTATION_PORTRAIT:
			inverted = true;
			break;

		case SDL_ORIENTATION_LANDSCAPE:
		case SDL_ORIENTATION_PORTRAIT_FLIPPED:
		default:
			inverted = false;
			break;
	}

	if (auroraPanelIsPortrait) {
		return inverted ? AURORA_TRANSFORM_270 : AURORA_TRANSFORM_90;
	}

	return inverted ? AURORA_TRANSFORM_180 : AURORA_TRANSFORM_NORMAL;
}

/*
====================
Aurora_SetBufferTransform

Tells the compositor which way the content is turned. It does not rotate the
buffer, but it needs the value to line up the system gestures with what the
viewer sees. The very same wl_output_transform value went into the quad.
====================
*/
static void Aurora_SetBufferTransform(auroraTransform_t transform)
{
#ifdef _AURORA
	SDL_SysWMinfo wmInfo;

	if (!window) {
		return;
	}

	SDL_VERSION(&wmInfo.version);

	if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
		common->Warning("[Aurora display]: SDL_GetWindowWMInfo failed: %s", SDL_GetError());
		return;
	}

	if (wmInfo.subsystem != SDL_SYSWM_WAYLAND) {
		return;
	}

	struct wl_surface *surface = wmInfo.info.wl.surface;

	if (!surface) {
		return;
	}

	wl_surface_set_buffer_transform(surface, (int32_t)transform);
#endif
}

/*
====================
Aurora_ApplyTransform
====================
*/
static void Aurora_ApplyTransform(auroraTransform_t transform)
{
	if (!auroraFramebuffer.IsActive()) {
		return;
	}

	if (transform == auroraFramebuffer.GetRotation()) {
		return;
	}

	if (!auroraFramebuffer.SetRotation(transform)) {
		return;
	}

	Aurora_SetBufferTransform(transform);
}

/*
====================
Aurora_ApplyCurrentOrientation
====================
*/
static void Aurora_ApplyCurrentOrientation(void)
{
	if (r_auroraRotation.GetInteger() >= 0) {
		// held by hand for testing, the display is not asked
		return;
	}

	if (auroraDisplayIndex < 0) {
		return;
	}

	Aurora_ApplyTransform(Aurora_TransformForOrientation(SDL_GetDisplayOrientation(auroraDisplayIndex)));
}

/*
====================
Aurora_SyncWindowSize

The buffer follows the real size of the window. Turning the device does not
change it: no resize event arrives then, only an orientation one.
====================
*/
static void Aurora_SyncWindowSize(void)
{
	int w = 0;
	int h = 0;

	if (!window || !auroraFramebuffer.IsActive()) {
		return;
	}

	SDL_GetWindowSize(window, &w, &h);

	if (w <= 0 || h <= 0) {
		return;
	}

	auroraFramebuffer.Resize(w, h);
}

/*
====================
Aurora_DisplayInit
====================
*/
void Aurora_DisplayInit(void)
{
	Aurora_DetectPanel();

	if (r_auroraRotation.GetInteger() >= 0) {
		Aurora_ApplyTransform((auroraTransform_t)r_auroraRotation.GetInteger());
	} else {
		Aurora_ApplyCurrentOrientation();
	}

	r_auroraRotation.ClearModified();
	r_auroraScale.ClearModified();
}

/*
====================
Aurora_DisplayShutdown
====================
*/
void Aurora_DisplayShutdown(void)
{
	auroraDisplayIndex = -1;
	auroraPanelIsPortrait = true;
}

/*
====================
Aurora_HandleDisplayEvent
====================
*/
bool Aurora_HandleDisplayEvent(const SDL_Event *ev)
{
	if (!auroraFramebuffer.IsActive()) {
		return false;
	}

	switch (ev->type) {
		case SDL_DISPLAYEVENT:
			switch (ev->display.event) {
				case SDL_DISPLAYEVENT_ORIENTATION:
					Aurora_ApplyCurrentOrientation();
					return true;

				default:
					return false;
			}

		case SDL_WINDOWEVENT:
			switch (ev->window.event) {
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				case SDL_WINDOWEVENT_RESIZED:
					Aurora_SyncWindowSize();
					return true;

				case SDL_WINDOWEVENT_MOVED:
					// the window may have landed on another display, and a
					// display of a different shape reads the same orientation
					// the other way round
					if (SDL_GetWindowDisplayIndex(window) != auroraDisplayIndex) {
						Aurora_DetectPanel();
						Aurora_SyncWindowSize();
						Aurora_ApplyCurrentOrientation();
						return true;
					}

					return false;

				default:
					return false;
			}

		default:
			return false;
	}
}

/*
====================
Aurora_DisplayFrame

Only the testing cvars are looked at here. Rotation and size are driven by
events, not polled.
====================
*/
void Aurora_DisplayFrame(void)
{
	if (!auroraFramebuffer.IsActive()) {
		return;
	}

	if (r_auroraRotation.IsModified()) {
		r_auroraRotation.ClearModified();

		if (r_auroraRotation.GetInteger() >= 0) {
			Aurora_ApplyTransform((auroraTransform_t)r_auroraRotation.GetInteger());
		} else {
			Aurora_ApplyCurrentOrientation();
		}
	}

	if (r_auroraScale.IsModified()) {
		r_auroraScale.ClearModified();
		auroraFramebuffer.SetScale(r_auroraScale.GetFloat());
	}
}

#endif
