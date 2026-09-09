#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"

#ifdef _AURORA_FBO

idAuroraFramebuffer auroraFramebuffer;

idCVar r_auroraFramebuffer("r_auroraFramebuffer", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_INIT, "render the frame into an intermediate framebuffer and draw it with a quad before swap");
idCVar r_auroraScale("r_auroraScale", "1.0", CVAR_RENDERER | CVAR_FLOAT | CVAR_ARCHIVE, "resolution the scene is rendered at, as a fraction of the window: below 1 renders smaller and scales up, above 1 supersamples", 0.25f, 2.0f);
idCVar r_auroraRotation("r_auroraRotation", "-1", CVAR_RENDERER | CVAR_INTEGER, "override the content rotation for testing: -1 follows the display, 0/1/2/3 are the wl_output_transform values", -1, 3);

static const char *AURORA_FBO_NAME = "_auroraScreen";

static const float AURORA_SCALE_MIN = 0.25f;
static const float AURORA_SCALE_MAX = 2.0f;

// the engine renders with the same orientation it would use for the backbuffer,
// so the texture is sampled without flipping: v = 0 at the bottom edge
static const float auroraQuadVertices[] = {
    // x      y      u     v
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
};

static const char *auroraVertexShaderSource =
    "uniform mat2 u_rotation;\n"
    "\n"
    "in vec2 attr_Position;\n"
    "in vec2 attr_TexCoord;\n"
    "out vec2 var_TexCoord;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    var_TexCoord = attr_TexCoord;\n"
    "    gl_Position = vec4(u_rotation * attr_Position, 0.0, 1.0);\n"
    "}\n";

static const char *auroraFragmentShaderSource =
    "uniform sampler2D u_texture;\n"
    "\n"
    "in vec2 var_TexCoord;\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = texture(u_texture, var_TexCoord);\n"
    "}\n";

/*
====================
R_AuroraShaderVersion

The module only supports the shader dialects that carry in/out declarations:
GLSL ES 3.00 for the device and desktop GLSL 3.30 core so the same code path
can be debugged on the host. GLES2 has no such dialect, there the module
stays disabled and the engine keeps rendering straight to the backbuffer.
====================
*/
static const char *R_AuroraShaderVersion(void)
{
    if (USING_GLES3) {
        return "#version 300 es\n";
    }

#if !defined(__ANDROID__)
    if (USING_GL) {
        return "#version 330 core\n";
    }
#endif

    return NULL;
}

/*
====================
R_AuroraCompileShader
====================
*/
static uint32_t R_AuroraCompileShader(GLenum type, const char *source)
{
    const char *version = R_AuroraShaderVersion();

    if (!version) {
        return 0;
    }

    idStr text;
    text = version;
    text += "precision highp float;\n";
    text += "\n";
    text += source;

    uint32_t shader = qglCreateShader(type);

    if (!shader) {
        common->Warning("[Aurora FBO]: qglCreateShader failed");
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
        common->Warning("[Aurora FBO]: %s shader compile failed: %s",
                        type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        qglDeleteShader(shader);
        return 0;
    }

    return shader;
}

idAuroraFramebuffer::idAuroraFramebuffer()
    : active(false),
    width(0),
    height(0),
    windowWidth(0),
    windowHeight(0),
    rotation(AURORA_TRANSFORM_NORMAL),
    scale(1.0f),
    currentRotationMatrix(NULL),
    fb(NULL),
    colorTexture(0),
    program(0),
    vertexShader(0),
    fragmentShader(0),
    textureUniform(-1),
    rotationUniform(-1),
    vertexAttrib(-1),
    texCoordAttrib(-1),
    vertexBuffer(0)
{
    memset(rotationMatrices, 0, sizeof(rotationMatrices));
}

/*
====================
idAuroraFramebuffer::BuildRotationMatrices

One 2x2 matrix per wl_output_transform value, in column major order, built
once. The quad is a fullscreen square in normalized device coordinates and
the matrix is applied to its vertex positions only, so the rotation the
viewer sees equals the transform, which is exactly what is handed to
wl_surface.set_buffer_transform. The texture coordinates are never touched.
====================
*/
void idAuroraFramebuffer::BuildRotationMatrices(void)
{
    // counter-clockwise by 0, 90, 180 and 270 degrees
    static const float angles[AURORA_TRANSFORM_COUNT][2] = {
        //  cos   sin
        {  1.0f,  0.0f },
        {  0.0f,  1.0f },
        { -1.0f,  0.0f },
        {  0.0f, -1.0f },
    };

    for (int i = 0; i < AURORA_TRANSFORM_COUNT; i++) {
        const float c = angles[i][0];
        const float s = angles[i][1];

        rotationMatrices[i][0] = c;
        rotationMatrices[i][1] = s;
        rotationMatrices[i][2] = -s;
        rotationMatrices[i][3] = c;
    }
}

/*
====================
idAuroraFramebuffer::GetFramebuffer

Returns 0 while the module is inactive, so the callers that replaced their
hardcoded 0 keep binding the real backbuffer.
====================
*/
uint32_t idAuroraFramebuffer::GetFramebuffer(void) const
{
    if (!active || !fb) {
        return 0;
    }

    return fb->GetFramebuffer();
}

/*
====================
idAuroraFramebuffer::CreateColorTexture

The color attachment has to be sampled by the quad, so it is a texture rather
than a renderbuffer. It is created directly instead of through idImage because
idImage::GenerateImage() rounds the size up to a power of two, which would
waste memory and force the quad to rescale its texture coordinates.
====================
*/
bool idAuroraFramebuffer::CreateColorTexture(void)
{
    qglGenTextures(1, &colorTexture);

    if (!colorTexture) {
        common->Warning("[Aurora FBO]: qglGenTextures failed");
        return false;
    }

    qglBindTexture(GL_TEXTURE_2D, colorTexture);
    qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    qglBindTexture(GL_TEXTURE_2D, 0);

    GL_CheckErrors();

    return true;
}

/*
====================
idAuroraFramebuffer::CreateProgram
====================
*/
bool idAuroraFramebuffer::CreateProgram(void)
{
    if (program) {
        return true;
    }

    vertexShader = R_AuroraCompileShader(GL_VERTEX_SHADER, auroraVertexShaderSource);

    if (!vertexShader) {
        return false;
    }

    fragmentShader = R_AuroraCompileShader(GL_FRAGMENT_SHADER, auroraFragmentShaderSource);

    if (!fragmentShader) {
        return false;
    }

    program = qglCreateProgram();

    if (!program) {
        common->Warning("[Aurora FBO]: qglCreateProgram failed");
        return false;
    }

    qglAttachShader(program, vertexShader);
    qglAttachShader(program, fragmentShader);
    qglLinkProgram(program);

    GLint linked = 0;
    qglGetProgramiv(program, GL_LINK_STATUS, &linked);

    if (!linked) {
        char log[1024];
        GLsizei length = 0;
        qglGetProgramInfoLog(program, sizeof(log), &length, log);
        log[sizeof(log) - 1] = '\0';
        common->Warning("[Aurora FBO]: program link failed: %s", log);
        return false;
    }

    textureUniform = qglGetUniformLocation(program, "u_texture");
    rotationUniform = qglGetUniformLocation(program, "u_rotation");
    vertexAttrib = qglGetAttribLocation(program, "attr_Position");
    texCoordAttrib = qglGetAttribLocation(program, "attr_TexCoord");

    if (textureUniform < 0 || rotationUniform < 0 || vertexAttrib < 0 || texCoordAttrib < 0) {
        common->Warning("[Aurora FBO]: program is missing u_texture(%d), u_rotation(%d), attr_Position(%d) or attr_TexCoord(%d)",
                        textureUniform, rotationUniform, vertexAttrib, texCoordAttrib);
        return false;
    }

    GL_CheckErrors();

    return true;
}

/*
====================
idAuroraFramebuffer::CreateGeometry
====================
*/
bool idAuroraFramebuffer::CreateGeometry(void)
{
    if (vertexBuffer) {
        return true;
    }

    qglGenBuffers(1, &vertexBuffer);

    if (!vertexBuffer) {
        common->Warning("[Aurora FBO]: qglGenBuffers failed");
        return false;
    }

    qglBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    qglBufferData(GL_ARRAY_BUFFER, sizeof(auroraQuadVertices), auroraQuadVertices, GL_STATIC_DRAW);
    qglBindBuffer(GL_ARRAY_BUFFER, 0);

    GL_CheckErrors();

    return true;
}

/*
====================
idAuroraFramebuffer::DestroyBuffer

Drops the framebuffer and its color texture, leaving the quad and the program
alone: those do not depend on the size and survive every rebuild.
====================
*/
void idAuroraFramebuffer::DestroyBuffer(void)
{
    if (colorTexture) {
        qglDeleteTextures(1, &colorTexture);
        colorTexture = 0;
    }

    if (fb) {
        fb->Purge();
        Framebuffer::framebuffers.Remove(fb);
        delete fb;
        fb = NULL;
    }
}

/*
====================
idAuroraFramebuffer::PublishSize

Tells the engine the screen is the size of the buffer. glConfig.vidWidth and
vidHeight are the single source every other place reads from, and the engine
sets its viewport from them every frame, so the scene lands in the buffer on
its own. The screen sized helper framebuffers are rebuilt to match, because
nothing else would tell them the size changed.
====================
*/
void idAuroraFramebuffer::PublishSize(void)
{
    glConfig.vidWidth = width;
    glConfig.vidHeight = height;

    if (!active) {
        // first build: Framebuffer::Init() creates the helpers right after us,
        // already at the size just published
        return;
    }

    if (idStencilTexture::IsAvailable()) {
        stencilTexture.Init(width, height);
    }

    if (idDepthStencilRenderer::IsAvailable()) {
        depthStencilRenderer.Init(width, height);
    }
}

/*
====================
idAuroraFramebuffer::Build

Builds the buffer for a window size, a transform and a scale. When the content
is turned onto its side the buffer takes the sides of the window swapped, so a
landscape game on a portrait panel renders landscape and the quad turns it.
====================
*/
bool idAuroraFramebuffer::Build(int newWindowWidth, int newWindowHeight, auroraTransform_t newRotation, float newScale)
{
    if (newWindowWidth <= 0 || newWindowHeight <= 0) {
        common->Warning("[Aurora FBO]: bad window size %d x %d", newWindowWidth, newWindowHeight);
        return false;
    }

    int newWidth = newWindowWidth;
    int newHeight = newWindowHeight;

    if (TransformIsSideways(newRotation)) {
        newWidth = newWindowHeight;
        newHeight = newWindowWidth;
    }

    newWidth = (int)(newWidth * newScale);
    newHeight = (int)(newHeight * newScale);

    if (newWidth < 1) {
        newWidth = 1;
    }

    if (newHeight < 1) {
        newHeight = 1;
    }

    const bool sizeChanged = (newWidth != width || newHeight != height);

    windowWidth = newWindowWidth;
    windowHeight = newWindowHeight;
    rotation = newRotation;
    scale = newScale;
    currentRotationMatrix = rotationMatrices[newRotation];

    if (!sizeChanged && fb) {
        // a turn between two sideways transforms keeps the size, and then only
        // the matrix changes: nothing to rebuild
        return true;
    }

    DestroyBuffer();

    width = newWidth;
    height = newHeight;

    fb = Framebuffer::Alloc(AURORA_FBO_NAME, width, height);

    if (!fb) {
        common->Warning("[Aurora FBO]: Framebuffer::Alloc failed");
        return false;
    }

    if (!CreateColorTexture()) {
        DestroyBuffer();
        return false;
    }

    fb->Bind();
    // idFramebuffer only attaches color through idImage, and the texture is a
    // plain GL object here, so it is attached the same way AttachImage2D() does
    qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
    fb->AddDepthStencilBuffer(GL_DEPTH24_STENCIL8);
    fb->Check();
    fb->Unbind();

    PublishSize();

    return true;
}

/*
====================
idAuroraFramebuffer::Init

Called from Framebuffer::Init() before anything binds the screen, so every
"back to the screen" path already sees the framebuffer it has to bind.
====================
*/
bool idAuroraFramebuffer::Init(int w, int h)
{
    if (active) {
        return Resize(w, h);
    }

    if (!r_auroraFramebuffer.GetBool()) {
        common->Printf("[Aurora FBO]: disabled by r_auroraFramebuffer\n");
        return false;
    }

    if (!R_AuroraShaderVersion()) {
        common->Warning("[Aurora FBO]: unsupported shader dialect, rendering straight to the backbuffer");
        return false;
    }

    BuildRotationMatrices();

    if (!CreateProgram() || !CreateGeometry()) {
        Shutdown();
        return false;
    }

    float initialScale = r_auroraScale.GetFloat();

    if (initialScale < AURORA_SCALE_MIN) {
        initialScale = AURORA_SCALE_MIN;
    } else if (initialScale > AURORA_SCALE_MAX) {
        initialScale = AURORA_SCALE_MAX;
    }

    // no rotation until the platform layer reports the display orientation
    if (!Build(w, h, AURORA_TRANSFORM_NORMAL, initialScale)) {
        Shutdown();
        return false;
    }

    active = true;

    common->Printf("[Aurora FBO]: %d x %d into window %d x %d, transform %d, scale %.2f, handle %d\n",
                   width, height, windowWidth, windowHeight, rotation, scale, fb->GetFramebuffer());

    return true;
}

/*
====================
idAuroraFramebuffer::SetRotation
====================
*/
bool idAuroraFramebuffer::SetRotation(auroraTransform_t transform)
{
    if (!active) {
        return false;
    }

    if (transform < 0 || transform >= AURORA_TRANSFORM_COUNT) {
        common->Warning("[Aurora FBO]: bad transform %d", transform);
        return false;
    }

    if (transform == rotation) {
        return true;
    }

    const int oldWidth = width;
    const int oldHeight = height;

    if (!Build(windowWidth, windowHeight, transform, scale)) {
        return false;
    }

    common->Printf("[Aurora FBO]: transform %d, buffer %d x %d\n", rotation, width, height);

    if (width != oldWidth || height != oldHeight) {
        common->Printf("[Aurora FBO]: buffer resized %d x %d -> %d x %d\n", oldWidth, oldHeight, width, height);
    }

    return true;
}

/*
====================
idAuroraFramebuffer::SetScale
====================
*/
bool idAuroraFramebuffer::SetScale(float newScale)
{
    if (!active) {
        return false;
    }

    if (newScale < AURORA_SCALE_MIN) {
        newScale = AURORA_SCALE_MIN;
    } else if (newScale > AURORA_SCALE_MAX) {
        newScale = AURORA_SCALE_MAX;
    }

    if (newScale == scale) {
        return true;
    }

    if (!Build(windowWidth, windowHeight, rotation, newScale)) {
        return false;
    }

    common->Printf("[Aurora FBO]: scale %.2f, buffer %d x %d\n", scale, width, height);

    return true;
}

/*
====================
idAuroraFramebuffer::Resize
====================
*/
bool idAuroraFramebuffer::Resize(int w, int h)
{
    if (!active) {
        return false;
    }

    if (w == windowWidth && h == windowHeight) {
        return true;
    }

    if (!Build(w, h, rotation, scale)) {
        return false;
    }

    common->Printf("[Aurora FBO]: window %d x %d, buffer %d x %d\n", windowWidth, windowHeight, width, height);

    return true;
}

/*
====================
idAuroraFramebuffer::Shutdown
====================
*/
void idAuroraFramebuffer::Shutdown(void)
{
    active = false;

    DestroyBuffer();

    if (vertexBuffer) {
        qglDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }

    if (program) {
        qglDeleteProgram(program);
        program = 0;
    }

    if (vertexShader) {
        qglDeleteShader(vertexShader);
        vertexShader = 0;
    }

    if (fragmentShader) {
        qglDeleteShader(fragmentShader);
        fragmentShader = 0;
    }

    textureUniform = -1;
    rotationUniform = -1;
    vertexAttrib = -1;
    texCoordAttrib = -1;
    currentRotationMatrix = NULL;

    width = height = 0;
    windowWidth = windowHeight = 0;
    rotation = AURORA_TRANSFORM_NORMAL;
    scale = 1.0f;
}

/*
====================
idAuroraFramebuffer::InvalidateEngineGLState

The quad is drawn with raw GLES3 calls, so everything the engine caches and
we touched has to be marked dirty. RB_SetDefaultGLState() resets the whole
cache at the start of the next backend frame, this only covers the rest of
the current one.
====================
*/
void idAuroraFramebuffer::InvalidateEngineGLState(void)
{
    backEnd.glState.forceGlState = true;
    backEnd.glState.currentProgram = NULL;

    // NULL keeps meaning "the screen" to the engine, it just happens to be our
    // framebuffer now, so the cache stays consistent with what is really bound
    backEnd.glState.currentFramebuffer = NULL;

    backEnd.glState.tmu[backEnd.glState.currenttmu].current2DMap = -1;
}

/*
====================
idAuroraFramebuffer::Draw

Draws the color texture onto the real backbuffer, rotated by the current
transform. Called from RB_SwapBuffers() after the engine has finished the
frame, including its UI and ImGui, and right before the buffers are swapped.

Nothing is computed here: the matrix was picked in SetRotation() and the quad
was built once.
====================
*/
void idAuroraFramebuffer::Draw(void)
{
    if (!active || !fb) {
        return;
    }

    // the real backbuffer, not the one the engine believes in
    qglBindFramebuffer(GL_FRAMEBUFFER, 0);
    backEnd.glState.currentFramebuffer = NULL;

    qglViewport(0, 0, windowWidth, windowHeight);
    qglDisable(GL_SCISSOR_TEST);
    qglDisable(GL_DEPTH_TEST);
    qglDisable(GL_STENCIL_TEST);
    qglDisable(GL_BLEND);
    qglDisable(GL_CULL_FACE);
    qglDepthMask(GL_FALSE);
    qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    qglUseProgram(program);

    qglUniformMatrix2fv(rotationUniform, 1, GL_FALSE, currentRotationMatrix);

    qglActiveTexture(GL_TEXTURE0);
    qglBindTexture(GL_TEXTURE_2D, colorTexture);
    qglUniform1i(textureUniform, 0);

    qglBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);

    qglEnableVertexAttribArray(vertexAttrib);
    qglVertexAttribPointer(vertexAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);

    qglEnableVertexAttribArray(texCoordAttrib);
    qglVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));

    qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    qglDisableVertexAttribArray(vertexAttrib);
    qglDisableVertexAttribArray(texCoordAttrib);

    qglBindBuffer(GL_ARRAY_BUFFER, 0);
    qglBindTexture(GL_TEXTURE_2D, 0);
    qglUseProgram(0);

    // back to the buffer the engine renders into
    qglBindFramebuffer(GL_FRAMEBUFFER, fb->GetFramebuffer());

    InvalidateEngineGLState();

    GL_CheckErrors();
}

#endif
