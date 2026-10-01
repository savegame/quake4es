/*
theme.cpp - AuroraOS launcher ImGui theme
Copyright (C) 2026

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// Palette + metrics grid from docs/imgui-theme-spec.md. The grid is defined
// for a 13 px font and scaled by (fontSizePx / 13) so every metric tracks
// the physical font size. The one deliberate deviation from the spec is the
// frame rounding: the spec says 2 px, the port's signature look rounds
// buttons by a full font size (set AFTER ScaleAllSizes so it isn't scaled).

#include "theme.h"

#include "imgui.h"
#include "imgui_internal.h"

#define THEME_HEX(h) ImVec4(((h >> 16) & 0xFF) / 255.f, ((h >> 8) & 0xFF) / 255.f, (h & 0xFF) / 255.f, 1.0f)

void AuroraLauncher_ApplyTheme( float fontSizePx )
{
	ImGui::StyleColorsDark();

	ImGuiStyle &s = ImGui::GetStyle();
	s.WindowPadding      = ImVec2( 10, 10 );
	s.FramePadding       = ImVec2( 6, 3 );
	s.ItemSpacing        = ImVec2( 6, 4 );
	s.ItemInnerSpacing   = ImVec2( 4, 4 );
	s.IndentSpacing      = 12.0f;
	s.WindowBorderSize   = 1.0f;
	s.FrameBorderSize    = 1.0f;
	s.WindowRounding     = 0.0f;
	s.FrameRounding      = 2.0f;
	s.GrabRounding       = 1.0f;
	s.ScrollbarSize      = 10.0f;
	s.TabRounding        = 0.0f;
	s.ScaleAllSizes( fontSizePx / 13.0f );

	// The port's signature button rounding (~30% of a ~3.3-font button
	// height). Set after ScaleAllSizes on purpose.
	s.FrameRounding      = fontSizePx;

	ImVec4 *c = s.Colors;
	c[ImGuiCol_WindowBg]         = THEME_HEX( 0x1F1F22 );
	c[ImGuiCol_ChildBg]          = THEME_HEX( 0x16161A );
	c[ImGuiCol_PopupBg]          = THEME_HEX( 0x232328 );
	c[ImGuiCol_FrameBg]          = THEME_HEX( 0x2A2A2F );
	c[ImGuiCol_FrameBgHovered]   = THEME_HEX( 0x34343A );
	c[ImGuiCol_FrameBgActive]    = THEME_HEX( 0x3D3D44 );
	c[ImGuiCol_Button]           = THEME_HEX( 0x34343A );
	c[ImGuiCol_ButtonHovered]    = THEME_HEX( 0x3F3F46 );
	c[ImGuiCol_ButtonActive]     = THEME_HEX( 0x4A4A52 );
	c[ImGuiCol_Header]           = THEME_HEX( 0x2E2E34 );
	c[ImGuiCol_HeaderHovered]    = THEME_HEX( 0x383840 );
	c[ImGuiCol_HeaderActive]     = THEME_HEX( 0x424249 );
	c[ImGuiCol_Border]           = THEME_HEX( 0x0B0B0D );
	c[ImGuiCol_Separator]        = THEME_HEX( 0x2E2E33 );
	c[ImGuiCol_Text]             = THEME_HEX( 0xE6E6EA );
	c[ImGuiCol_TextDisabled]     = THEME_HEX( 0x5D5D66 );
	c[ImGuiCol_CheckMark]        = THEME_HEX( 0xFFFFFF );
	c[ImGuiCol_SliderGrab]       = THEME_HEX( 0x3B82F6 ); /* accent */
	c[ImGuiCol_SliderGrabActive] = THEME_HEX( 0x5AA0FF ); /* accent line */
}

// colours of the switch, from the palette above
#define THEME_ACCENT        0x3B82F6
#define THEME_SWITCH_OFF    0x2A2A2F    /* FrameBg */
#define THEME_SWITCH_BORDER 0x0B0B0D    /* Border */
#define THEME_KNOB_ON       0xFFFFFF
#define THEME_KNOB_OFF      0x8A8A93

// seconds the knob takes to slide across
#define THEME_SWITCH_SLIDE  0.12f

static ImU32 ThemeLerpColor( unsigned a, unsigned b, float t )
{
	return ImGui::GetColorU32( ImLerp( THEME_HEX( a ), THEME_HEX( b ), t ));
}

/* Draws the switch with its knob t of the way to the right, 0..1. */
static void DrawSwitch( ImDrawList *list, const ImVec2 &pos, float height, float t )
{
	const float width = height * 2.0f;
	const float radius = height * 0.5f;
	const float knob = radius - height * 0.12f;
	const ImVec2 end( pos.x + width, pos.y + height );

	list->AddRectFilled( pos, end, ThemeLerpColor( THEME_SWITCH_OFF, THEME_ACCENT, t ), radius );
	list->AddRect( pos, end, ImGui::GetColorU32( THEME_HEX( THEME_SWITCH_BORDER )), radius, 0, 1.0f );

	const ImVec2 centre( pos.x + radius + ( width - height ) * t, pos.y + radius );
	list->AddCircleFilled( centre, knob, ThemeLerpColor( THEME_KNOB_OFF, THEME_KNOB_ON, t ));
}

/* Where the knob is, eased towards the value; kept per widget id. */
static float SlideKnob( ImGuiID id, bool value )
{
	ImGuiStorage *storage = ImGui::GetStateStorage();
	const float target = value ? 1.0f : 0.0f;
	float t = storage->GetFloat( id, target );
	const float step = ImGui::GetIO().DeltaTime / THEME_SWITCH_SLIDE;

	if( t < target ) t = ImMin( target, t + step );
	else if( t > target ) t = ImMax( target, t - step );

	storage->SetFloat( id, t );
	return t;
}

bool AuroraLauncher_Toggle( const char *id, bool *value, float height )
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const bool clicked = ImGui::InvisibleButton( id, ImVec2( height * 2.0f, height ));

	if( clicked )
		*value = !*value;

	DrawSwitch( ImGui::GetWindowDrawList(), pos, height, SlideKnob( ImGui::GetItemID(), *value ));
	return clicked;
}

bool AuroraLauncher_LabelToggle( const char *label, bool *value, float height )
{
	const ImGuiStyle &style = ImGui::GetStyle();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const float rowHeight = height + style.FramePadding.y * 2.0f;
	const float switchWidth = height * 2.0f;

	ImGui::PushID( label );
	const bool clicked = ImGui::InvisibleButton( "##row", ImVec2( width, rowHeight ));
	const ImGuiID id = ImGui::GetItemID();
	ImGui::PopID();

	if( clicked )
		*value = !*value;

	ImDrawList *list = ImGui::GetWindowDrawList();

	/* the label, centred on the row and kept clear of the switch */
	const ImVec2 textSize = ImGui::CalcTextSize( label );
	const ImVec2 textPos( pos.x, pos.y + ( rowHeight - textSize.y ) * 0.5f );
	const ImVec4 clip( pos.x, pos.y, pos.x + width - switchWidth - style.ItemSpacing.x, pos.y + rowHeight );
	list->AddText( NULL, 0.0f, textPos, ImGui::GetColorU32( ImGuiCol_Text ), label, NULL, 0.0f, &clip );

	const ImVec2 switchPos( pos.x + width - switchWidth, pos.y + ( rowHeight - height ) * 0.5f );
	DrawSwitch( list, switchPos, height, SlideKnob( id, *value ));

	return clicked;
}

static float SnapToStep( float value, float min, float max, float step )
{
	if( step > 0.0f )
		value = min + floorf(( value - min ) / step + 0.5f ) * step;
	return ImClamp( value, min, max );
}

bool AuroraLauncher_StepSlider( const char *id, float *value, float min, float max, float step,
                                const char *format, float height, float sliderRect[4] )
{
	const ImGuiStyle &style = ImGui::GetStyle();
	const float old = *value;

	ImGui::PushID( id );

	if( ImGui::Button( "-", ImVec2( height * 1.4f, height )))
		*value = SnapToStep( *value - step, min, max, step );

	ImGui::SameLine();

	/* the slider fills what the buttons leave, as tall as they are */
	const float sliderWidth = ImGui::GetContentRegionAvail().x - height * 1.4f - style.ItemSpacing.x;
	const float padY = ImMax( 0.0f, ( height - ImGui::GetFontSize()) * 0.5f );
	ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( style.FramePadding.x, padY ));
	ImGui::PushStyleVar( ImGuiStyleVar_GrabMinSize, height * 0.6f );
	ImGui::PushItemWidth( sliderWidth );
	if( ImGui::SliderFloat( "##slider", value, min, max, format ))
		*value = SnapToStep( *value, min, max, step );
	ImGui::PopItemWidth();
	ImGui::PopStyleVar( 2 );

	if( sliderRect )
	{
		const ImVec2 a = ImGui::GetItemRectMin();
		const ImVec2 b = ImGui::GetItemRectMax();
		sliderRect[0] = a.x;
		sliderRect[1] = a.y;
		sliderRect[2] = b.x;
		sliderRect[3] = b.y;
	}

	ImGui::SameLine();

	if( ImGui::Button( "+", ImVec2( height * 1.4f, height )))
		*value = SnapToStep( *value + step, min, max, step );

	ImGui::PopID();

	return *value != old;
}
