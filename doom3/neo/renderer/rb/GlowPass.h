#ifndef _GLOW_PASS_H
#define _GLOW_PASS_H

// Prey's glow overlay.
//
// A material stage marked glowStage is drawn as usual and once more into a
// glow buffer that holds nothing else. The buffer is shrunk, blurred and added
// over the view, which puts a soft halo around the glowing parts: the weapons,
// the crawlers, the portals, the energy of the Hunters...
//
// The glow buffer shares the depth buffer of the intermediate screen
// framebuffer, so the glowing stages are hidden by the scene exactly like their
// normal copies are, and nothing has to be copied. The overlay is therefore
// only available while that framebuffer is active.
#if defined(_HUMANHEAD) && defined(_AURORA_FBO)
#define _GLOW_PASS 1

extern idCVar r_glow;
extern idCVar r_glowStrength;
extern idCVar r_glowSteps;

// Called from RB_STD_DrawView() once the scene is shaded, before the post
// process surfaces read it from _currentRender. drawSurfs are the surfaces the
// first shader pass drew.
void RB_Glow_DrawView(drawSurf_t **drawSurfs, int numDrawSurfs);

void RB_Glow_Shutdown(void);

ID_INLINE bool R_Glow_MaterialHasGlow(const idMaterial *material)
{
	for (int i = 0; i < material->GetNumStages(); i++) {
		if (material->GetStage(i)->isGlow) {
			return true;
		}
	}

	return false;
}
#endif

#endif
