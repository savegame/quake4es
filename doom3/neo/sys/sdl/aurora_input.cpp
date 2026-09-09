// Included from sys/sdl/events.cpp, which owns the event loop.

#ifdef _AURORA_FBO

/*
====================
Aurora_RotateDelta

Input arrives from the compositor in the coordinates of the display; the
rotation of the content does not affect it. The engine, on the other hand,
works in the coordinates of the framebuffer, so a movement has to be turned
by the inverse of the current transform before it is handed over.

Screen and framebuffer pixels both count y downwards, while the transform is
a counter-clockwise rotation in a y-up space, which is where the signs below
come from. Moving right on a screen showing 90 degree turned content, for
instance, is moving down inside the framebuffer.

    transform     (dx, dy) becomes
    0             ( dx,  dy)
    90            (-dy,  dx)
    180           (-dx, -dy)
    270           ( dy, -dx)
====================
*/
static void Aurora_RotateDelta(auroraTransform_t transform, int dx, int dy, int *outX, int *outY)
{
	switch (transform) {
		case AURORA_TRANSFORM_90:
			*outX = -dy;
			*outY = dx;
			break;

		case AURORA_TRANSFORM_180:
			*outX = -dx;
			*outY = -dy;
			break;

		case AURORA_TRANSFORM_270:
			*outX = dy;
			*outY = -dx;
			break;

		case AURORA_TRANSFORM_NORMAL:
		default:
			*outX = dx;
			*outY = dy;
			break;
	}
}

/*
====================
Aurora_TransformDelta

Turns a movement in display pixels into one in framebuffer pixels: the
rotation above, then the scale the framebuffer is drawn at. The engine reads
these as framebuffer pixels — the menu converts them into its own 640x480
space by dividing by renderSystem->GetScreenWidth(), which is the width of
the framebuffer — so a buffer rendered at half resolution has to receive half
the movement for the cursor to keep up with the finger.
====================
*/
static void Aurora_TransformDelta(int dx, int dy, int *outX, int *outY)
{
	Aurora_RotateDelta(auroraFramebuffer.GetRotation(), dx, dy, outX, outY);

	const float scale = auroraFramebuffer.GetScale();

	if (scale != 1.0f) {
		*outX = (int)(*outX * scale);
		*outY = (int)(*outY * scale);
	}
}

/*
====================
Aurora_TransformPosition

Turns a position in display pixels into one in framebuffer pixels. Going
through normalized coordinates covers the rotation and the scale at once,
because the quad maps the whole buffer onto the whole window.
====================
*/
static void Aurora_TransformPosition(int x, int y, int *outX, int *outY)
{
	const int windowWidth = auroraFramebuffer.WindowWidth();
	const int windowHeight = auroraFramebuffer.WindowHeight();
	const int bufferWidth = auroraFramebuffer.Width();
	const int bufferHeight = auroraFramebuffer.Height();

	if (windowWidth <= 0 || windowHeight <= 0) {
		*outX = x;
		*outY = y;
		return;
	}

	// to normalized device coordinates, y up
	const float nx = 2.0f * x / windowWidth - 1.0f;
	const float ny = 1.0f - 2.0f * y / windowHeight;

	float bx;
	float by;

	switch (auroraFramebuffer.GetRotation()) {
		case AURORA_TRANSFORM_90:
			bx = ny;
			by = -nx;
			break;

		case AURORA_TRANSFORM_180:
			bx = -nx;
			by = -ny;
			break;

		case AURORA_TRANSFORM_270:
			bx = -ny;
			by = nx;
			break;

		case AURORA_TRANSFORM_NORMAL:
		default:
			bx = nx;
			by = ny;
			break;
	}

	*outX = (int)((bx + 1.0f) * 0.5f * bufferWidth);
	*outY = (int)((1.0f - by) * 0.5f * bufferHeight);
}

/*
====================
Aurora_TransformInputEvent

Rewrites the coordinates of an SDL event in place, before the engine gets to
see it. Gamepad events are deliberately left alone: a stick does not care
which way the screen is turned.
====================
*/
void Aurora_TransformInputEvent(SDL_Event *ev)
{
	int x;
	int y;

	if (!auroraFramebuffer.IsActive()) {
		return;
	}

	if (auroraFramebuffer.GetRotation() == AURORA_TRANSFORM_NORMAL && auroraFramebuffer.GetScale() == 1.0f) {
		return;
	}

	switch (ev->type) {
		case SDL_MOUSEMOTION:
			Aurora_TransformDelta(ev->motion.xrel, ev->motion.yrel, &x, &y);
			ev->motion.xrel = x;
			ev->motion.yrel = y;

			Aurora_TransformPosition(ev->motion.x, ev->motion.y, &x, &y);
			ev->motion.x = x;
			ev->motion.y = y;
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			Aurora_TransformPosition(ev->button.x, ev->button.y, &x, &y);
			ev->button.x = x;
			ev->button.y = y;
			break;

		default:
			break;
	}
}

#endif
