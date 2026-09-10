#ifndef _AURORA_FRAMEBUFFER_H
#define _AURORA_FRAMEBUFFER_H

#ifdef _AURORA_FBO

class idFramebuffer;

// values of the wl_output_transform enum, the angles are counter-clockwise.
// The same value goes into the quad rotation and into
// wl_surface.set_buffer_transform, so the two can never disagree.
typedef enum {
    AURORA_TRANSFORM_NORMAL = 0,
    AURORA_TRANSFORM_90     = 1,
    AURORA_TRANSFORM_180    = 2,
    AURORA_TRANSFORM_270    = 3,

    AURORA_TRANSFORM_COUNT
} auroraTransform_t;

// Intermediate screen framebuffer.
//
// The engine renders the scene and then its UI into this framebuffer as if it
// was the backbuffer: every path that returns "to the screen" binds this one
// instead of 0, and glConfig.vidWidth/vidHeight report its size. Right before
// the buffer swap the color texture is drawn onto the real backbuffer as a
// single quad, and that quad carries the rotation and the scaling, because the
// compositor does neither.
//
// Everything that can be prepared up front is: the rotation matrices for all
// four transforms are built once, the quad and its buffer are built once, and
// SetRotation() only picks which matrix is current. Drawing a frame does no
// computing and allocates nothing.
class idAuroraFramebuffer
{
    public:
        idAuroraFramebuffer();

        bool                Init(int windowWidth, int windowHeight);
        void                Shutdown(void);

        bool                IsActive(void) const {
            return active;
        }

        // handle the engine binds instead of 0 while the module is active
        uint32_t            GetFramebuffer(void) const;
        idFramebuffer *     GetFramebufferObject(void) const {
            return fb;
        }

        // draw the color texture onto the real backbuffer, called before swap
        void                Draw(void);

        // Rotation of the content. Takes a wl_output_transform value, picks the
        // matching prepared matrix and, when the transform changes between an
        // upright and a sideways one, rebuilds the buffer with swapped sides.
        bool                SetRotation(auroraTransform_t transform);
        auroraTransform_t   GetRotation(void) const {
            return rotation;
        }

        // Resolution the scene is rendered at, as a fraction of the window.
        // 1.0 renders at the window resolution, less renders smaller and the
        // quad scales it up. Out of range values are clamped.
        bool                SetScale(float scale);
        float               GetScale(void) const {
            return scale;
        }

        // Real window size changed: rebuild the buffer to match it.
        bool                Resize(int windowWidth, int windowHeight);

        // Display 0 is the built-in one and it is the reference: the user
        // tuned the render scale for it, so its size is what the pixel budget
        // is measured against, whatever window the application ends up in.
        void                SetReferenceDisplaySize(int w, int h);

        // size the engine believes the screen is
        int                 Width(void) const {
            return width;
        }
        int                 Height(void) const {
            return height;
        }

        // size of the real window/backbuffer
        int                 WindowWidth(void) const {
            return windowWidth;
        }
        int                 WindowHeight(void) const {
            return windowHeight;
        }

        // true while the content is turned onto its side, which is when the
        // buffer has the sides of the window swapped
        static bool         TransformIsSideways(auroraTransform_t transform) {
            return transform == AURORA_TRANSFORM_90 || transform == AURORA_TRANSFORM_270;
        }

    private:
        bool                Build(int windowWidth, int windowHeight, auroraTransform_t transform, float scale);
        void                BufferSizeForWindow(int windowWidth, int windowHeight, float scale, int *outWidth, int *outHeight) const;
        void                DestroyBuffer(void);
        bool                CreateColorTexture(void);
        bool                CreateProgram(void);
        bool                CreateGeometry(void);
        void                BuildRotationMatrices(void);
        void                PublishSize(void);
        void                InvalidateEngineGLState(void);

        bool                active;

        int                 width;
        int                 height;
        int                 windowWidth;
        int                 windowHeight;

        int                 referenceDisplayWidth;
        int                 referenceDisplayHeight;

        auroraTransform_t   rotation;
        float               scale;

        // column major 2x2 matrices, one per transform, built once
        float               rotationMatrices[AURORA_TRANSFORM_COUNT][4];
        const float *       currentRotationMatrix;

        idFramebuffer *     fb;
        uint32_t            colorTexture;

        uint32_t            program;
        uint32_t            vertexShader;
        uint32_t            fragmentShader;
        int                 textureUniform;
        int                 rotationUniform;
        int                 vertexAttrib;
        int                 texCoordAttrib;

        uint32_t            vertexBuffer;

        idAuroraFramebuffer(const idAuroraFramebuffer &);
        idAuroraFramebuffer & operator=(const idAuroraFramebuffer &);
};

extern idAuroraFramebuffer auroraFramebuffer;

extern idCVar r_auroraFramebuffer;
extern idCVar r_auroraScale;
extern idCVar r_auroraRotation;

union SDL_Event;

// Display side of the port, in sys/sdl/aurora_display.cpp: works out how the
// content has to be turned from the display orientation and the shape of the
// panel, hands the result to the module above and to the compositor, and keeps
// the buffer in step with the size of the window.
void Aurora_DisplayInit(void);
void Aurora_DisplayShutdown(void);

// Handles SDL_DISPLAYEVENT and SDL_WINDOWEVENT. Returns true when the event
// was a display or size change the port acted on; the engine goes on handling
// it either way.
bool Aurora_HandleDisplayEvent(const SDL_Event *ev);

// Picks up the testing cvars. Called once a frame, before the swap.
void Aurora_DisplayFrame(void);

// Input side of the port, in sys/sdl/aurora_input.cpp. Rewrites the
// coordinates of an event in place so the engine reads them in the
// coordinates of the framebuffer rather than those of the display.
void Aurora_TransformInputEvent(SDL_Event *ev);

// Handle to bind where the engine used to hardcode 0. Falls back to 0 while
// the module is inactive, so the engine keeps rendering to the backbuffer.
#define AURORA_SCREEN_FRAMEBUFFER() ( auroraFramebuffer.GetFramebuffer() )

#else

#define AURORA_SCREEN_FRAMEBUFFER() 0

#endif

#endif
