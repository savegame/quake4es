#include "GlowPass.h"

#ifdef _GLOW_PASS

extern idCVar r_skipGlowOverlay;
int RB_STD_DrawShaderPasses(drawSurf_t **drawSurfs, int numDrawSurfs);

idCVar r_glow("r_glow", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "draw the glow overlay around the glowStage material stages");
idCVar r_glowStrength("r_glowStrength", "1.0", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE, "brightness of the glow overlay", 0.0f, 4.0f);
idCVar r_glowSteps("r_glowSteps", "2", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "blur passes of the glow overlay, more spreads it wider and costs more", 1, 8);

// the blur runs at a quarter of the resolution of the scene in each direction
#define GLOW_DOWNSCALE		4

#define GLOW_ATTR_POSITION	0
#define GLOW_ATTR_TEXCOORD	1

static const char *glowVertexShaderSource =
    "in vec2 attr_Position;\n"
    "in vec2 attr_TexCoord;\n"
    "uniform vec4 u_uvRect;\n"
    "out vec2 var_TexCoord;\n"
    "void main()\n"
    "{\n"
    "    var_TexCoord = u_uvRect.xy + attr_TexCoord * u_uvRect.zw;\n"
    "    gl_Position = vec4(attr_Position, 0.0, 1.0);\n"
    "}\n";

// 4 bilinear taps a texel off the centre average the 4x4 block shrunk into
// one pixel
static const char *glowDownsampleShaderSource =
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_parm; // xy: a texel of the source\n"
    "in vec2 var_TexCoord;\n"
    "out vec4 _gl_FragColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 d = u_parm.xy;\n"
    "    vec3 c = texture(u_texture, var_TexCoord + vec2(-d.x, -d.y)).rgb;\n"
    "    c += texture(u_texture, var_TexCoord + vec2( d.x, -d.y)).rgb;\n"
    "    c += texture(u_texture, var_TexCoord + vec2(-d.x,  d.y)).rgb;\n"
    "    c += texture(u_texture, var_TexCoord + vec2( d.x,  d.y)).rgb;\n"
    "    _gl_FragColor = vec4(c * 0.25, 1.0);\n"
    "}\n";

// a 9 tap gaussian in 5 bilinear taps along one direction
static const char *glowBlurShaderSource =
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_parm; // xy: the direction, a texel long\n"
    "in vec2 var_TexCoord;\n"
    "out vec4 _gl_FragColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 d1 = u_parm.xy * 1.3846153846;\n"
    "    vec2 d2 = u_parm.xy * 3.2307692308;\n"
    "    vec3 c = texture(u_texture, var_TexCoord).rgb * 0.2270270270;\n"
    "    c += texture(u_texture, var_TexCoord + d1).rgb * 0.3162162162;\n"
    "    c += texture(u_texture, var_TexCoord - d1).rgb * 0.3162162162;\n"
    "    c += texture(u_texture, var_TexCoord + d2).rgb * 0.0702702703;\n"
    "    c += texture(u_texture, var_TexCoord - d2).rgb * 0.0702702703;\n"
    "    _gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static const char *glowCompositeShaderSource =
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_parm; // x: strength\n"
    "in vec2 var_TexCoord;\n"
    "out vec4 _gl_FragColor;\n"
    "void main()\n"
    "{\n"
    "    _gl_FragColor = vec4(texture(u_texture, var_TexCoord).rgb * u_parm.x, 1.0);\n"
    "}\n";

// triangle strip, position then texcoord
static const float glowQuadVertices[16] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

typedef struct {
    uint32_t	program;
    int			textureUniform;
    int			uvRectUniform;
    int			parmUniform;
} glowProgram_t;

typedef struct {
    uint32_t	framebuffer;
    uint32_t	texture;
} glowTarget_t;

static struct {
    bool			failed;			// something could not be created, don't try every frame

    glowProgram_t	downsample;
    glowProgram_t	blurProgram;
    glowProgram_t	composite;
    uint32_t		vertexBuffer;

    // the size and the depth buffer of the screen framebuffer the targets were
    // built for
    int				width;
    int				height;
    uint32_t		depthBuffer;

    glowTarget_t	glow;			// full size, shares the depth buffer
    glowTarget_t	blur[2];		// a quarter of the size, ping-pong
    int				blurWidth;
    int				blurHeight;
} glow;

/*
====================
R_Glow_CompileShader
====================
*/
static uint32_t R_Glow_CompileShader(GLenum type, const char *source)
{
    idStr text;

    if (USING_GLES3) {
        text = "#version 300 es\n";
    } else {
        text = "#version 330 core\n";
    }

    text += "precision highp float;\n";
    text += source;

    uint32_t shader = qglCreateShader(type);

    if (!shader) {
        return 0;
    }

    const char *ptr = text.c_str();
    qglShaderSource(shader, 1, &ptr, NULL);
    qglCompileShader(shader);

    GLint compiled = 0;
    qglGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        char log[1024];
        GLsizei length = 0;
        qglGetShaderInfoLog(shader, sizeof(log), &length, log);
        log[sizeof(log) - 1] = '\0';
        common->Warning("[Glow]: %s shader compile failed: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        qglDeleteShader(shader);
        return 0;
    }

    return shader;
}

/*
====================
R_Glow_CreateProgram
====================
*/
static bool R_Glow_CreateProgram(glowProgram_t &prog, const char *fragmentSource)
{
    uint32_t vs = R_Glow_CompileShader(GL_VERTEX_SHADER, glowVertexShaderSource);
    uint32_t fs = R_Glow_CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

    if (!vs || !fs) {
        return false;
    }

    prog.program = qglCreateProgram();
    qglAttachShader(prog.program, vs);
    qglAttachShader(prog.program, fs);
    qglBindAttribLocation(prog.program, GLOW_ATTR_POSITION, "attr_Position");
    qglBindAttribLocation(prog.program, GLOW_ATTR_TEXCOORD, "attr_TexCoord");
    qglLinkProgram(prog.program);

    // the program keeps them alive as long as it needs them
    qglDeleteShader(vs);
    qglDeleteShader(fs);

    GLint linked = 0;
    qglGetProgramiv(prog.program, GL_LINK_STATUS, &linked);

    if (!linked) {
        char log[1024];
        GLsizei length = 0;
        qglGetProgramInfoLog(prog.program, sizeof(log), &length, log);
        log[sizeof(log) - 1] = '\0';
        common->Warning("[Glow]: program link failed: %s", log);
        return false;
    }

    prog.textureUniform = qglGetUniformLocation(prog.program, "u_texture");
    prog.uvRectUniform = qglGetUniformLocation(prog.program, "u_uvRect");
    prog.parmUniform = qglGetUniformLocation(prog.program, "u_parm");

    return true;
}

/*
====================
R_Glow_DestroyTarget
====================
*/
static void R_Glow_DestroyTarget(glowTarget_t &target)
{
    if (target.framebuffer) {
        qglDeleteFramebuffers(1, &target.framebuffer);
    }

    if (target.texture) {
        qglDeleteTextures(1, &target.texture);
    }

    target.framebuffer = 0;
    target.texture = 0;
}

/*
====================
R_Glow_CreateTarget

A color texture to render into, with the depth buffer of the screen when
depthBuffer is given.
====================
*/
static bool R_Glow_CreateTarget(glowTarget_t &target, int width, int height, uint32_t depthBuffer)
{
    qglGenTextures(1, &target.texture);
    qglBindTexture(GL_TEXTURE_2D, target.texture);
    qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    qglBindTexture(GL_TEXTURE_2D, 0);

    qglGenFramebuffers(1, &target.framebuffer);
    qglBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);

    if (depthBuffer) {
        qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    }

    const GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        common->Warning("[Glow]: framebuffer %dx%d incomplete: 0x%x", width, height, status);
        return false;
    }

    return true;
}

/*
====================
RB_Glow_Shutdown
====================
*/
void RB_Glow_Shutdown(void)
{
    R_Glow_DestroyTarget(glow.glow);
    R_Glow_DestroyTarget(glow.blur[0]);
    R_Glow_DestroyTarget(glow.blur[1]);

    if (glow.downsample.program) {
        qglDeleteProgram(glow.downsample.program);
    }

    if (glow.blurProgram.program) {
        qglDeleteProgram(glow.blurProgram.program);
    }

    if (glow.composite.program) {
        qglDeleteProgram(glow.composite.program);
    }

    if (glow.vertexBuffer) {
        qglDeleteBuffers(1, &glow.vertexBuffer);
    }

    memset(&glow, 0, sizeof(glow));
}

/*
====================
R_Glow_Prepare

Builds what is missing and rebuilds the targets whenever the screen framebuffer
has been rebuilt, which comes with a new size or a new depth buffer. Leaves the
screen framebuffer bound.
====================
*/
static bool R_Glow_Prepare(void)
{
    if (glow.failed) {
        return false;
    }

    const idFramebuffer *screen = auroraFramebuffer.GetFramebufferObject();

    if (!screen || !screen->depthBuffer) {
        return false;
    }

    if (!glow.vertexBuffer) {
        if (!R_Glow_CreateProgram(glow.downsample, glowDownsampleShaderSource)
            || !R_Glow_CreateProgram(glow.blurProgram, glowBlurShaderSource)
            || !R_Glow_CreateProgram(glow.composite, glowCompositeShaderSource)) {
            glow.failed = true;
            return false;
        }

        qglGenBuffers(1, &glow.vertexBuffer);
        qglBindBuffer(GL_ARRAY_BUFFER, glow.vertexBuffer);
        qglBufferData(GL_ARRAY_BUFFER, sizeof(glowQuadVertices), glowQuadVertices, GL_STATIC_DRAW);
        qglBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    const int width = screen->Width();
    const int height = screen->Height();

    if (glow.glow.framebuffer && width == glow.width && height == glow.height && screen->depthBuffer == glow.depthBuffer) {
        return true;
    }

    R_Glow_DestroyTarget(glow.glow);
    R_Glow_DestroyTarget(glow.blur[0]);
    R_Glow_DestroyTarget(glow.blur[1]);

    glow.width = width;
    glow.height = height;
    glow.depthBuffer = screen->depthBuffer;
    glow.blurWidth = Max(1, width / GLOW_DOWNSCALE);
    glow.blurHeight = Max(1, height / GLOW_DOWNSCALE);

    const bool built = R_Glow_CreateTarget(glow.glow, width, height, screen->depthBuffer)
                       && R_Glow_CreateTarget(glow.blur[0], glow.blurWidth, glow.blurHeight, 0)
                       && R_Glow_CreateTarget(glow.blur[1], glow.blurWidth, glow.blurHeight, 0);

    qglBindFramebuffer(GL_FRAMEBUFFER, screen->GetFramebuffer());

    if (!built) {
        R_Glow_DestroyTarget(glow.glow);
        R_Glow_DestroyTarget(glow.blur[0]);
        R_Glow_DestroyTarget(glow.blur[1]);
        glow.failed = true;
        return false;
    }

    common->Printf("[Glow]: %dx%d, blurred at %dx%d\n", width, height, glow.blurWidth, glow.blurHeight);

    return true;
}

/*
====================
R_Glow_Pass

Draws the quad with a program, reading one texture.
====================
*/
static void R_Glow_Pass(const glowProgram_t &prog, uint32_t texture, const float uvRect[4], float p0, float p1)
{
    qglUseProgram(prog.program);
    qglBindTexture(GL_TEXTURE_2D, texture);
    qglUniform1i(prog.textureUniform, 0);
    qglUniform4f(prog.uvRectUniform, uvRect[0], uvRect[1], uvRect[2], uvRect[3]);
    qglUniform4f(prog.parmUniform, p0, p1, 0.0f, 0.0f);
    qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/*
====================
RB_Glow_DrawView
====================
*/
void RB_Glow_DrawView(drawSurf_t **drawSurfs, int numDrawSurfs)
{
    if (!r_glow.GetBool() || r_skipGlowOverlay.GetBool() || numDrawSurfs <= 0) {
        return;
    }

    // the main view only: mirrors, portals and cameras go without it
    if (!backEnd.viewDef->viewEntitys || backEnd.viewDef->isSubview || !auroraFramebuffer.IsActive()) {
        return;
    }

    int i;

    for (i = 0; i < numDrawSurfs; i++) {
        if (R_Glow_MaterialHasGlow(drawSurfs[i]->material)) {
            break;
        }
    }

    if (i == numDrawSurfs) {
        return;
    }

    if (!R_Glow_Prepare()) {
        return;
    }

    const uint32_t screen = auroraFramebuffer.GetFramebuffer();

    GLint viewport[4];
    qglGetIntegerv(GL_VIEWPORT, viewport);

    GLfloat clearColor[4];
    qglGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);

    // the glowing stages, alone, into the glow buffer: the depth buffer is the
    // one of the scene, so whatever hides them in the scene hides them here
    qglBindFramebuffer(GL_FRAMEBUFFER, glow.glow.framebuffer);

    const GLboolean scissorTest = qglIsEnabled(GL_SCISSOR_TEST);
    qglDisable(GL_SCISSOR_TEST);
    qglClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    qglClear(GL_COLOR_BUFFER_BIT);
    qglClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);

    if (scissorTest) {
        qglEnable(GL_SCISSOR_TEST);
    }

    backEnd.glowPass = true;
    RB_STD_DrawShaderPasses(drawSurfs, numDrawSurfs);
    backEnd.glowPass = false;

    // what the engine left set, to give back after the quads
    GLint scissor[4];
    qglGetIntegerv(GL_SCISSOR_BOX, scissor);
    const GLboolean scissorAfter = qglIsEnabled(GL_SCISSOR_TEST);
    const GLboolean depthTest = qglIsEnabled(GL_DEPTH_TEST);
    const GLboolean stencilTest = qglIsEnabled(GL_STENCIL_TEST);
    const GLboolean cullFace = qglIsEnabled(GL_CULL_FACE);
    const GLboolean blend = qglIsEnabled(GL_BLEND);
    const GLboolean polygonOffset = qglIsEnabled(GL_POLYGON_OFFSET_FILL);

    GL_SelectTexture(0);

    qglDisable(GL_SCISSOR_TEST);
    qglDisable(GL_DEPTH_TEST);
    qglDisable(GL_STENCIL_TEST);
    qglDisable(GL_CULL_FACE);
    qglDisable(GL_BLEND);
    qglDisable(GL_POLYGON_OFFSET_FILL);
    qglDepthMask(GL_FALSE);
    qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    qglBindBuffer(GL_ARRAY_BUFFER, glow.vertexBuffer);
    qglEnableVertexAttribArray(GLOW_ATTR_POSITION);
    qglVertexAttribPointer(GLOW_ATTR_POSITION, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
    qglEnableVertexAttribArray(GLOW_ATTR_TEXCOORD);
    qglVertexAttribPointer(GLOW_ATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));

    const float whole[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

    // shrink
    qglBindFramebuffer(GL_FRAMEBUFFER, glow.blur[0].framebuffer);
    qglViewport(0, 0, glow.blurWidth, glow.blurHeight);
    R_Glow_Pass(glow.downsample, glow.glow.texture, whole, 1.0f / glow.width, 1.0f / glow.height);

    // blur, across and then down, as many times as asked
    const int steps = r_glowSteps.GetInteger();

    for (int step = 0; step < steps; step++) {
        qglBindFramebuffer(GL_FRAMEBUFFER, glow.blur[1].framebuffer);
        R_Glow_Pass(glow.blurProgram, glow.blur[0].texture, whole, 1.0f / glow.blurWidth, 0.0f);

        qglBindFramebuffer(GL_FRAMEBUFFER, glow.blur[0].framebuffer);
        R_Glow_Pass(glow.blurProgram, glow.blur[1].texture, whole, 0.0f, 1.0f / glow.blurHeight);
    }

    // add it over the view
    qglBindFramebuffer(GL_FRAMEBUFFER, screen);
    qglViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    qglEnable(GL_BLEND);
    qglBlendFunc(GL_ONE, GL_ONE);

    const float view[4] = {
        (float)viewport[0] / glow.width,
        (float)viewport[1] / glow.height,
        (float)viewport[2] / glow.width,
        (float)viewport[3] / glow.height,
    };
    R_Glow_Pass(glow.composite, glow.blur[0].texture, view, r_glowStrength.GetFloat(), 0.0f);

    qglDisableVertexAttribArray(GLOW_ATTR_POSITION);
    qglDisableVertexAttribArray(GLOW_ATTR_TEXCOORD);
    qglBindBuffer(GL_ARRAY_BUFFER, 0);
    qglBindTexture(GL_TEXTURE_2D, 0);
    qglUseProgram(0);

    // give the engine back what it had
    qglScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
#define GLOW_RESTORE(cap, on) if (on) { qglEnable(cap); } else { qglDisable(cap); }
    GLOW_RESTORE(GL_SCISSOR_TEST, scissorAfter);
    GLOW_RESTORE(GL_DEPTH_TEST, depthTest);
    GLOW_RESTORE(GL_STENCIL_TEST, stencilTest);
    GLOW_RESTORE(GL_CULL_FACE, cullFace);
    GLOW_RESTORE(GL_BLEND, blend);
    GLOW_RESTORE(GL_POLYGON_OFFSET_FILL, polygonOffset);
#undef GLOW_RESTORE

    // the blend function and the depth mask come back with the next GL_State
    backEnd.glState.forceGlState = true;
    backEnd.glState.currentProgram = NULL;
    backEnd.glState.tmu[0].current2DMap = -1;
    // the screen framebuffer is what the engine calls the screen
    backEnd.glState.currentFramebuffer = NULL;

    GL_CheckErrors();
}

#endif
