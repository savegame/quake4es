#ifndef _AURORA_FSR_H
#define _AURORA_FSR_H

#ifdef _AURORA_FBO

// AMD FidelityFX Super Resolution 1.0 for the intermediate framebuffer.
//
// The scene is rendered small and FSR brings it up to an output buffer of its
// own size, which is neither the scene nor the screen: EASU, the edge adaptive
// upscale, into a texture of the output size, then RCAS, the contrast adaptive
// sharpening, from it into the output buffer. The 2D the engine draws after
// the scene goes into the output buffer at its full resolution, and the quad
// before swap stretches the output buffer onto the screen as it did the scene.
//
// Both passes are spatial: they read nothing but the frame, need no motion
// vectors and no history. The shaders follow ffx_fsr1.h (MIT license), with
// texelFetch in place of textureGather, which GLSL ES 3.00 does not have.
class idAuroraFsr
{
    public:
        idAuroraFsr();

        // builds the targets for an input (scene) and an output size, the
        // programs once; false if anything could not be created
        bool                Build(int inputWidth, int inputHeight, int outputWidth, int outputHeight);
        void                Destroy(void);
        void                Shutdown(void);

        bool                IsBuilt(void) const {
            return outputFramebuffer != 0;
        }

        // EASU from the scene texture, RCAS into the output buffer, which is
        // left bound. sharpness is in stops: 0 is the sharpest, 2 hardly any.
        void                Upscale(uint32_t sceneTexture, float sharpness);

        uint32_t            OutputFramebuffer(void) const {
            return outputFramebuffer;
        }
        uint32_t            OutputTexture(void) const {
            return outputTexture;
        }
        int                 OutputWidth(void) const {
            return outputWidth;
        }
        int                 OutputHeight(void) const {
            return outputHeight;
        }

    private:
        bool                CreatePrograms(void);

        int                 inputWidth;
        int                 inputHeight;
        int                 outputWidth;
        int                 outputHeight;

        uint32_t            easuProgram;
        int                 easuInputUniform;
        int                 easuCon0Uniform;
        int                 easuInputMaxUniform;

        uint32_t            rcasProgram;
        int                 rcasInputUniform;
        int                 rcasConUniform;

        uint32_t            easuTexture;
        uint32_t            easuFramebuffer;

        uint32_t            outputTexture;
        uint32_t            outputDepthBuffer;     // later 3D views (GUI models) draw into the output too
        uint32_t            outputFramebuffer;

        idAuroraFsr(const idAuroraFsr &);
        idAuroraFsr & operator=(const idAuroraFsr &);
};

#endif

#endif
