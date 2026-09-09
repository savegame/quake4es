#ifndef _AURORA_FRAMEBUFFER_H
#define _AURORA_FRAMEBUFFER_H

#ifdef _AURORA_FBO

class idFramebuffer;

// Intermediate screen framebuffer.
//
// The engine renders the scene and then its UI into this framebuffer as if it
// was the backbuffer: every path that returns "to the screen" binds this one
// instead of 0, and glConfig.vidWidth/vidHeight report its size. Right before
// the buffer swap the color texture is drawn onto the real backbuffer as a
// single quad. The compositor never modifies the buffer, so rotating and
// scaling the content is up to us and happens in that final quad.
//
// At this stage the quad is drawn 1:1, without rotation or scaling.
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

    private:
        bool                CreateColorTexture(void);
        bool                CreateProgram(void);
        bool                CreateGeometry(void);
        void                InvalidateEngineGLState(void);

        bool                active;

        int                 width;
        int                 height;
        int                 windowWidth;
        int                 windowHeight;

        idFramebuffer *     fb;
        uint32_t            colorTexture;

        uint32_t            program;
        uint32_t            vertexShader;
        uint32_t            fragmentShader;
        int                 textureUniform;
        int                 vertexAttrib;
        int                 texCoordAttrib;

        uint32_t            vertexBuffer;

        idAuroraFramebuffer(const idAuroraFramebuffer &);
        idAuroraFramebuffer & operator=(const idAuroraFramebuffer &);
};

extern idAuroraFramebuffer auroraFramebuffer;

// Handle to bind where the engine used to hardcode 0. Falls back to 0 while
// the module is inactive, so the engine keeps rendering to the backbuffer.
#define AURORA_SCREEN_FRAMEBUFFER() ( auroraFramebuffer.GetFramebuffer() )

#else

#define AURORA_SCREEN_FRAMEBUFFER() 0

#endif

#endif
