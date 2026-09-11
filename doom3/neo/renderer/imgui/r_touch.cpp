// Included from renderer/tr_rendertools.cpp, next to the ImGui settings.

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

/*
===============================================================================

	On-screen touch controls

	Drawn with an ImGui context of their own: the settings window keeps its
	context, style and input to itself, and this one is current only while
	the overlay is being drawn. Nothing but a draw list is used, no windows,
	no input; the icons are lines and circles, sharp at any size, and the
	words are the launcher's font, baked at the size they are drawn at.

	The controls are laid out in pixels of the content, the screen the way
	the viewer sees it. The finished draw data is turned into pixels of the
	window with the transform of the frame's quad and goes straight onto the
	real backbuffer, at the resolution of the screen, whatever the frame was
	rendered at. The turns are multiples of 90 degrees, so the pixels of the
	content fall exactly onto pixels of the window.

===============================================================================
*/

static ImGuiContext *touchImGui = NULL;
static bool touchImGuiFailed = false;

// the pixel sizes the words are drawn at, each baked into the atlas
static const int TOUCH_MAX_FONTS = 4;
static int touchFontSizes[TOUCH_MAX_FONTS];
static ImFont *touchFonts[TOUCH_MAX_FONTS];
static int touchNumFonts = 0;
static ImVector<ImWchar> touchGlyphs;	// the letters of the words

// the accent of the launcher's theme
static const int TOUCH_ACCENT_R = 0x3B;
static const int TOUCH_ACCENT_G = 0x82;
static const int TOUCH_ACCENT_B = 0xF6;

/*
====================
RB_TouchOverlay_Init

Made on the first frame that has something to show, with the context of the
back end current, and kept until the renderer shuts down.
====================
*/
static bool RB_TouchOverlay_Init(void)
{
	if (touchImGui) {
		return true;
	}

	if (touchImGuiFailed) {
		return false;
	}

	ImGuiContext *previous = ImGui::GetCurrentContext();

	touchImGui = ImGui::CreateContext();
	ImGui::SetCurrentContext(touchImGui);

	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = NULL;
	io.LogFilename = NULL;

	bool ok;
#ifdef _OPENGLES3
	if (USING_GLES3) {
		ok = ImGui_ImplOpenGL3_Init("#version 300 es");
	} else
#endif
	ok = ImGui_ImplOpenGL3_Init("#version 100");

	if (!ok) {
		common->Warning("Touch overlay: ImGui renderer init failed, the touch controls are not drawn");
		ImGui::DestroyContext(touchImGui);
		touchImGui = NULL;
		touchImGuiFailed = true;
	}

	ImGui::SetCurrentContext(previous);

	return ok;
}

/*
====================
RB_ShutdownTouchOverlay
====================
*/
void RB_ShutdownTouchOverlay(void)
{
	touchImGuiFailed = false;

	if (!touchImGui) {
		return;
	}

	ImGuiContext *previous = ImGui::GetCurrentContext();

	ImGui::SetCurrentContext(touchImGui);
	ImGui_ImplOpenGL3_Shutdown();
	ImGui::DestroyContext(touchImGui);

	ImGui::SetCurrentContext(previous != touchImGui ? previous : NULL);
	touchImGui = NULL;

	// the fonts went with the atlas of the context
	touchNumFonts = 0;
}

/*
====================
RB_TouchColor
====================
*/
static ImU32 RB_TouchColor(int r, int g, int b, float alpha)
{
	return IM_COL32(r, g, b, idMath::Ftoi(idMath::ClampFloat(0.0f, 1.0f, alpha) * 255.0f));
}

/*
====================
RB_TouchArrowHead

A chevron with its tip at tip, pointing along dir (unit length)
====================
*/
static void RB_TouchArrowHead(ImDrawList *list, const ImVec2 &tip, const ImVec2 &dir, float size, ImU32 color, float thickness)
{
	const ImVec2 back(tip.x - dir.x * size, tip.y - dir.y * size);
	const ImVec2 side(-dir.y * size, dir.x * size);

	ImVec2 points[3];
	points[0] = ImVec2(back.x + side.x, back.y + side.y);
	points[1] = tip;
	points[2] = ImVec2(back.x - side.x, back.y - side.y);

	list->AddPolyline(points, 3, color, ImDrawFlags_None, thickness);
}

/*
====================
RB_TouchFont

The font baked at a pixel size, NULL when it isn't
====================
*/
static ImFont *RB_TouchFont(int size, bool *baked)
{
	for (int i = 0; i < touchNumFonts; i++) {
		if (touchFontSizes[i] == size) {
			*baked = true;
			return touchFonts[i];
		}
	}

	*baked = false;
	return NULL;
}

/*
====================
RB_TouchFontSize
====================
*/
static int RB_TouchFontSize(float buttonHeight)
{
	return Max(8, idMath::Ftoi(TOUCH_FONT_SIZE * buttonHeight + 0.5f));
}

/*
====================
RB_TouchListed
====================
*/
static bool RB_TouchListed(const int *sizes, int numSizes, int size)
{
	for (int i = 0; i < numSizes; i++) {
		if (sizes[i] == size) {
			return true;
		}
	}

	return false;
}

/*
====================
RB_TouchOverlay_Fonts

The words are written in the launcher's Noto Sans Bold, baked at the very
pixel size each is drawn at, so that they are as sharp as the lines of the
icons. The sizes change only with the layout; then the atlas is baked anew,
with nothing but the letters of the words. Called between the frames of
the context, while its font texture may be replaced.
====================
*/
static void RB_TouchOverlay_Fonts(const touchOverlay_t *overlay)
{
	int sizes[TOUCH_MAX_FONTS];
	int numSizes = 0;
	bool missing = false;

	// the sizes this frame writes at come first
	for (int i = 0; i < overlay->numButtons; i++) {
		const touchOverlayButton_t &button = overlay->buttons[i];

		if (!TouchOverlay_IconText(button.icon)) {
			continue;
		}

		const int size = RB_TouchFontSize(button.h);
		bool baked;

		RB_TouchFont(size, &baked);
		missing = missing || !baked;

		if (numSizes < TOUCH_MAX_FONTS && !RB_TouchListed(sizes, numSizes, size)) {
			sizes[numSizes++] = size;
		}
	}

	if (!missing) {
		return;
	}

	// what is baked already stays while there is room: the menu button's
	// SKIP comes and goes with the cinematics
	for (int i = 0; i < touchNumFonts && numSizes < TOUCH_MAX_FONTS; i++) {
		if (!RB_TouchListed(sizes, numSizes, touchFontSizes[i])) {
			sizes[numSizes++] = touchFontSizes[i];
		}
	}

	if (!touchGlyphs.Size) {
		ImFontGlyphRangesBuilder builder;

		for (int icon = 0; icon < TOUCH_ICON_COUNT; icon++) {
			const char *text = TouchOverlay_IconText((touchIcon_t)icon);

			if (text) {
				builder.AddText(text);
			}
		}

		builder.BuildRanges(&touchGlyphs);
	}

	unsigned int dataSize = 0;
	const unsigned int *data = R_ImGui_NotoSansBold(&dataSize);
	ImFontAtlas *atlas = ImGui::GetIO().Fonts;
	bool any = false;

	atlas->Clear();

	for (int i = 0; i < numSizes; i++) {
		// a size that fails stays listed, without a font, so that it is not
		// tried again every frame
		touchFontSizes[i] = sizes[i];
		touchFonts[i] = atlas->AddFontFromMemoryCompressedTTF(data, (int)dataSize, (float)sizes[i], NULL, touchGlyphs.Data);
		any = any || touchFonts[i] != NULL;
	}

	touchNumFonts = numSizes;

	if (!any) {
		common->Warning("Touch overlay: can't load the font, the words are written in ImGui's own");
		atlas->AddFontDefault();
	}

	ImGui_ImplOpenGL3_DestroyFontsTexture();
	ImGui_ImplOpenGL3_CreateFontsTexture();
}

/*
====================
RB_TouchText

A word centred on c, made smaller should it not fit into maxWidth
====================
*/
static void RB_TouchText(ImDrawList *list, const char *text, const ImVec2 &c, float buttonHeight, float maxWidth, ImU32 color)
{
	bool baked;
	ImFont *font = RB_TouchFont(RB_TouchFontSize(buttonHeight), &baked);

	if (!font) {
		font = ImGui::GetFont();
	}

	float size = (float)RB_TouchFontSize(buttonHeight);
	ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);

	if (extent.x > maxWidth && maxWidth > 0.0f) {
		size *= maxWidth / extent.x;
		extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
	}

	// the capitals are centred, not the line with room for accents and
	// descenders: their top and bottom are those of their glyphs
	float top = FLT_MAX;
	float bottom = -FLT_MAX;

	for (int i = 0; text[i]; i++) {
		const ImFontGlyph *glyph = font->FindGlyph((ImWchar)text[i]);

		if (glyph) {
			top = Min(top, glyph->Y0);
			bottom = Max(bottom, glyph->Y1);
		}
	}

	if (top > bottom) {
		top = 0.0f;
		bottom = font->FontSize;
	}

	const float scale = size / font->FontSize;

	// on whole pixels, where the atlas was baked for them
	const ImVec2 pos(idMath::Floor(c.x - extent.x * 0.5f + 0.5f), idMath::Floor(c.y - (top + bottom) * 0.5f * scale + 0.5f));

	list->AddText(font, size, pos, color, text);
}

/*
====================
RB_TouchIcon

The icon of a button around its centre c, s is half the size of the icon
====================
*/
static void RB_TouchIcon(ImDrawList *list, touchIcon_t icon, const ImVec2 &c, float s, ImU32 color, float thickness)
{
	switch (icon) {
		case TOUCH_ICON_MENU: {
			for (int i = -1; i <= 1; i++) {
				const float y = c.y + i * s * 0.55f;
				list->AddLine(ImVec2(c.x - s * 0.75f, y), ImVec2(c.x + s * 0.75f, y), color, thickness);
			}
			break;
		}

		case TOUCH_ICON_FIRE: {
			// a crosshair
			list->AddCircle(c, s * 0.6f, color, 0, thickness);

			for (int i = 0; i < 4; i++) {
				const float dx = (i == 0) ? 1.0f : (i == 1) ? -1.0f : 0.0f;
				const float dy = (i == 2) ? 1.0f : (i == 3) ? -1.0f : 0.0f;
				list->AddLine(ImVec2(c.x + dx * s * 0.3f, c.y + dy * s * 0.3f),
							  ImVec2(c.x + dx * s * 1.05f, c.y + dy * s * 1.05f), color, thickness);
			}
			break;
		}

		case TOUCH_ICON_JUMP: {
			// an arrow up
			const ImVec2 tip(c.x, c.y - s * 0.9f);
			list->AddLine(ImVec2(c.x, c.y + s * 0.9f), tip, color, thickness);
			RB_TouchArrowHead(list, tip, ImVec2(0.0f, -1.0f), s * 0.6f, color, thickness);
			break;
		}

		case TOUCH_ICON_CROUCH: {
			// an arrow down onto the floor
			const ImVec2 tip(c.x, c.y + s * 0.45f);
			list->AddLine(ImVec2(c.x, c.y - s * 0.95f), tip, color, thickness);
			RB_TouchArrowHead(list, tip, ImVec2(0.0f, 1.0f), s * 0.55f, color, thickness);
			list->AddLine(ImVec2(c.x - s * 0.9f, c.y + s * 0.95f), ImVec2(c.x + s * 0.9f, c.y + s * 0.95f), color, thickness);
			break;
		}

		case TOUCH_ICON_ZOOM: {
			// a magnifying glass
			const ImVec2 lens(c.x - s * 0.2f, c.y - s * 0.2f);
			const float r = s * 0.6f;
			const float edge = r * 0.7071f;
			list->AddCircle(lens, r, color, 0, thickness);
			list->AddLine(ImVec2(lens.x + edge, lens.y + edge), ImVec2(c.x + s * 0.95f, c.y + s * 0.95f), color, thickness * 1.5f);
			break;
		}

		case TOUCH_ICON_RELOAD: {
			// a circular arrow, three quarters round
			const float r = s * 0.8f;
			const float start = idMath::PI * -0.25f;
			const float end = idMath::PI * 1.25f;
			list->PathArcTo(c, r, start, end);
			list->PathStroke(color, ImDrawFlags_None, thickness);

			const ImVec2 tip(c.x + idMath::Cos(end) * r, c.y + idMath::Sin(end) * r);
			const ImVec2 dir(-idMath::Sin(end), idMath::Cos(end));
			RB_TouchArrowHead(list, tip, dir, s * 0.45f, color, thickness);
			break;
		}

		case TOUCH_ICON_PREVWEAPON:
		case TOUCH_ICON_NEXTWEAPON: {
			// a skip: a triangle against a bar
			const float d = (icon == TOUCH_ICON_NEXTWEAPON) ? 1.0f : -1.0f;
			list->AddTriangleFilled(ImVec2(c.x - d * s * 0.6f, c.y - s * 0.75f),
									ImVec2(c.x + d * s * 0.5f, c.y),
									ImVec2(c.x - d * s * 0.6f, c.y + s * 0.75f), color);
			list->AddLine(ImVec2(c.x + d * s * 0.75f, c.y - s * 0.75f), ImVec2(c.x + d * s * 0.75f, c.y + s * 0.75f), color, thickness);
			break;
		}

		case TOUCH_ICON_FLASHLIGHT: {
			// a light with its rays
			list->AddCircleFilled(c, s * 0.32f, color);

			for (int i = 0; i < 8; i++) {
				const float a = idMath::PI * 0.25f * i;
				const float x = idMath::Cos(a);
				const float y = idMath::Sin(a);
				list->AddLine(ImVec2(c.x + x * s * 0.55f, c.y + y * s * 0.55f),
							  ImVec2(c.x + x * s * 0.95f, c.y + y * s * 0.95f), color, thickness);
			}
			break;
		}

		case TOUCH_ICON_OBJECTIVES: {
			// a list
			for (int i = -1; i <= 1; i++) {
				const float y = c.y + i * s * 0.6f;
				list->AddCircleFilled(ImVec2(c.x - s * 0.7f, y), thickness * 0.8f, color);
				list->AddLine(ImVec2(c.x - s * 0.35f, y), ImVec2(c.x + s * 0.85f, y), color, thickness);
			}
			break;
		}

		default:
			// the words, drawn on capsules by RB_TouchOverlay_Build()
			break;
	}
}

/*
====================
RB_TouchOverlay_Build
====================
*/
static void RB_TouchOverlay_Build(ImDrawList *list, const touchOverlay_t *overlay)
{
	const float alpha = overlay->alpha;
	const float ring = Max(1.5f, 0.3f * overlay->pixelsPerMm);
	const float stroke = Max(2.0f, 0.45f * overlay->pixelsPerMm);

	for (int i = 0; i < overlay->numButtons; i++) {
		const touchOverlayButton_t &button = overlay->buttons[i];
		const ImVec2 c(button.x + button.w * 0.5f, button.y + button.h * 0.5f);
		const ImU32 fill = button.pressed ? RB_TouchColor(TOUCH_ACCENT_R, TOUCH_ACCENT_G, TOUCH_ACCENT_B, alpha * 0.8f) : RB_TouchColor(0, 0, 0, alpha * 0.4f);
		const ImU32 edge = button.pressed ? RB_TouchColor(255, 255, 255, alpha) : RB_TouchColor(255, 255, 255, alpha * 0.7f);
		const ImU32 content = RB_TouchColor(255, 255, 255, alpha * 0.95f);
		const char *text = TouchOverlay_IconText(button.icon);

		if (text) {
			// a capsule filling the rectangle, its ring inside it like a circle's
			const ImVec2 min(button.x + ring * 0.5f, button.y + ring * 0.5f);
			const ImVec2 max(button.x + button.w - ring * 0.5f, button.y + button.h - ring * 0.5f);
			const float rounding = (max.y - min.y) * 0.5f;

			list->AddRectFilled(min, max, fill, rounding);
			list->AddRect(min, max, edge, rounding, ImDrawFlags_None, ring);
			RB_TouchText(list, text, c, button.h, button.w - TOUCH_TEXT_PADDING * button.h, content);
			continue;
		}

		const float r = Min(button.w, button.h) * 0.5f - ring * 0.5f;

		list->AddCircleFilled(c, r, fill);
		list->AddCircle(c, r, edge, 0, ring);
		RB_TouchIcon(list, button.icon, c, r * 0.5f, content, stroke);
	}

	if (overlay->stick) {
		const ImVec2 base(overlay->stickX, overlay->stickY);
		const ImVec2 knob(overlay->knobX, overlay->knobY);
		const float r = overlay->stickRadius;

		list->AddCircleFilled(base, r, RB_TouchColor(0, 0, 0, alpha * 0.25f));
		list->AddCircle(base, r, RB_TouchColor(255, 255, 255, alpha * 0.5f), 0, ring);
		list->AddCircleFilled(knob, r * 0.45f, RB_TouchColor(255, 255, 255, alpha * 0.45f));
		list->AddCircle(knob, r * 0.45f, RB_TouchColor(255, 255, 255, alpha * 0.8f), 0, ring);
	}
}

#ifdef _AURORA_FBO
/*
====================
RB_TouchOverlay_ToWindow

From pixels of the content to pixels of the window, the turn the quad makes.
A clip rectangle stays a rectangle, only its corners trade places.
====================
*/
static void RB_TouchOverlay_ToWindow(ImDrawData *drawData, float contentWidth, float contentHeight, float windowWidth, float windowHeight)
{
	for (int i = 0; i < drawData->CmdListsCount; i++) {
		ImDrawList *list = drawData->CmdLists[i];

		for (int v = 0; v < list->VtxBuffer.Size; v++) {
			ImVec2 &pos = list->VtxBuffer[v].pos;
			float x, y;

			auroraFramebuffer.ContentToWindow(pos.x / contentWidth, pos.y / contentHeight, &x, &y);
			pos.x = x * windowWidth;
			pos.y = y * windowHeight;
		}

		for (int c = 0; c < list->CmdBuffer.Size; c++) {
			ImVec4 &clip = list->CmdBuffer[c].ClipRect;
			float x1, y1, x2, y2;

			auroraFramebuffer.ContentToWindow(clip.x / contentWidth, clip.y / contentHeight, &x1, &y1);
			auroraFramebuffer.ContentToWindow(clip.z / contentWidth, clip.w / contentHeight, &x2, &y2);

			clip.x = Min(x1, x2) * windowWidth;
			clip.y = Min(y1, y2) * windowHeight;
			clip.z = Max(x1, x2) * windowWidth;
			clip.w = Max(y1, y2) * windowHeight;
		}
	}

	drawData->DisplayPos = ImVec2(0.0f, 0.0f);
	drawData->DisplaySize = ImVec2(windowWidth, windowHeight);
}
#endif

/*
====================
RB_DrawTouchOverlay

Called from RB_SwapBuffers() once the frame is on the real backbuffer.
====================
*/
void RB_DrawTouchOverlay(const touchOverlay_t *overlay)
{
	if (!overlay) {
		return;
	}

	int windowWidth = glConfig.vidWidth;
	int windowHeight = glConfig.vidHeight;
	int contentWidth = windowWidth;
	int contentHeight = windowHeight;

#ifdef _AURORA_FBO
	const bool turned = auroraFramebuffer.IsActive();

	if (turned) {
		windowWidth = auroraFramebuffer.WindowWidth();
		windowHeight = auroraFramebuffer.WindowHeight();
		contentWidth = auroraFramebuffer.ContentWidth();
		contentHeight = auroraFramebuffer.ContentHeight();
	}
#endif

	// laid out for a screen that has turned or changed its size since
	if (overlay->width != contentWidth || overlay->height != contentHeight || contentWidth <= 0 || contentHeight <= 0) {
		return;
	}

	if (!RB_TouchOverlay_Init()) {
		return;
	}

	ImGuiContext *previous = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(touchImGui);

	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)contentWidth, (float)contentHeight);
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	io.DeltaTime = 1.0f / 60.0f;

	ImGui_ImplOpenGL3_NewFrame();
	RB_TouchOverlay_Fonts(overlay);
	ImGui::NewFrame();
	RB_TouchOverlay_Build(ImGui::GetForegroundDrawList(), overlay);
	ImGui::Render();

	ImDrawData *drawData = ImGui::GetDrawData();

#ifdef _AURORA_FBO
	if (turned) {
		RB_TouchOverlay_ToWindow(drawData, (float)contentWidth, (float)contentHeight, (float)windowWidth, (float)windowHeight);
	}
#endif

	// the real backbuffer, then back to the one the engine renders into
	qglBindFramebuffer(GL_FRAMEBUFFER, 0);
	ImGui_ImplOpenGL3_RenderDrawData(drawData);
	qglBindFramebuffer(GL_FRAMEBUFFER, AURORA_SCREEN_FRAMEBUFFER());

	ImGui::SetCurrentContext(previous);

	// ImGui puts back the GL state it changes, but not through the engine's
	// cache of it
	backEnd.glState.forceGlState = true;
	backEnd.glState.currentProgram = NULL;
	backEnd.glState.currentFramebuffer = NULL;
	backEnd.glState.tmu[backEnd.glState.currenttmu].current2DMap = -1;
}
