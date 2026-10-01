/*
theme.h - AuroraOS launcher ImGui theme
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

#pragma once
#ifndef LAUNCHER_THEME_H
#define LAUNCHER_THEME_H

// Apply the port's ImGui theme: the palette and the base metrics grid from
// docs/imgui-theme-spec.md, with the whole grid scaled by the actual font
// size (fontSizePx / 13). Call after ImGui::CreateContext.
void AuroraLauncher_ApplyTheme( float fontSizePx );

// On/off switch: a capsule with a round knob, the knob on the right and the
// capsule in the accent colour while on, on the left and grey while off. The
// knob slides between the two. height is the height of the capsule, the width
// is twice that. Returns true on the frame the value was flipped.
bool AuroraLauncher_Toggle( const char *id, bool *value, float height );

// A row of the settings: the label on the left, the switch on the right. The
// whole row flips it, which is an easier target for a finger than the switch.
// Returns true on the frame the value was flipped.
bool AuroraLauncher_LabelToggle( const char *label, bool *value, float height );

// A slider with "-" and "+" buttons at its sides, every value snapped to step
// from min. The buttons move it a step each. sliderRect, when given, receives
// the screen rectangle of the slider itself (x0, y0, x1, y1), so that the
// caller's touch handling can tell a drag on it from a scroll. Returns true
// on the frame the value changed.
bool AuroraLauncher_StepSlider( const char *id, float *value, float min, float max, float step,
                                const char *format, float height, float sliderRect[4] );

#endif // LAUNCHER_THEME_H
