// Included into the same unit as AuroraFramebuffer.cpp, whose shader compiler
// it uses.

#ifdef _AURORA_FBO

// Both passes draw one triangle that covers the target, made from gl_VertexID,
// so they need no vertex buffer.
static const char *auroraFsrVertexShaderSource =
    "void main(void)\n"
    "{\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// FsrEasuF() of ffx_fsr1.h in 32 bit floats. The 12 taps around the 2x2
// quad the output pixel falls into:
//
//      b c
//    e f g h
//    i j k l
//      n o
static const char *auroraFsrEasuShaderSource =
    "precision highp int;\n"
    "uniform sampler2D u_input;\n"
    "uniform vec4 u_con0;     // xy: input pixels per output pixel, zw: offset to the input pixel centre\n"
    "uniform vec2 u_inputMax; // the last input pixel\n"
    "out vec4 fragColor;\n"
    "\n"
    "vec3 Tap(ivec2 fp, ivec2 o)\n"
    "{\n"
    "    return texelFetch(u_input, clamp(fp + o, ivec2(0), ivec2(u_inputMax)), 0).rgb;\n"
    "}\n"
    "\n"
    "float Luma(vec3 c)\n"
    "{\n"
    "    return c.g + 0.5 * (c.r + c.b);\n"
    "}\n"
    "\n"
    "// direction and length of the edge, accumulated over the bilinear corners\n"
    "void SetF(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE)\n"
    "{\n"
    "    float dc = lD - lC;\n"
    "    float cb = lC - lB;\n"
    "    float lenX = 1.0 / max(max(abs(dc), abs(cb)), 1.0e-8);\n"
    "    float dirX = lD - lB;\n"
    "    dir.x += dirX * w;\n"
    "    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);\n"
    "    lenX *= lenX;\n"
    "    len += lenX * w;\n"
    "    float ec = lE - lC;\n"
    "    float ca = lC - lA;\n"
    "    float lenY = 1.0 / max(max(abs(ec), abs(ca)), 1.0e-8);\n"
    "    float dirY = lE - lA;\n"
    "    dir.y += dirY * w;\n"
    "    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);\n"
    "    lenY *= lenY;\n"
    "    len += lenY * w;\n"
    "}\n"
    "\n"
    "// one tap of the lanczos-like kernel, stretched along the edge\n"
    "void TapF(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)\n"
    "{\n"
    "    vec2 v;\n"
    "    v.x = off.x * dir.x + off.y * dir.y;\n"
    "    v.y = off.x * -dir.y + off.y * dir.x;\n"
    "    v *= len;\n"
    "    float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
    "    float wB = 2.0 / 5.0 * d2 - 1.0;\n"
    "    float wA = lob * d2 - 1.0;\n"
    "    wB *= wB;\n"
    "    wA *= wA;\n"
    "    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);\n"
    "    float w = wB * wA;\n"
    "    aC += c * w;\n"
    "    aW += w;\n"
    "}\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    vec2 pp = floor(gl_FragCoord.xy) * u_con0.xy + u_con0.zw;\n"
    "    vec2 fpf = floor(pp);\n"
    "    pp -= fpf;\n"
    "    ivec2 fp = ivec2(fpf);\n"
    "\n"
    "    vec3 b = Tap(fp, ivec2( 0, -1));\n"
    "    vec3 c = Tap(fp, ivec2( 1, -1));\n"
    "    vec3 e = Tap(fp, ivec2(-1,  0));\n"
    "    vec3 f = Tap(fp, ivec2( 0,  0));\n"
    "    vec3 g = Tap(fp, ivec2( 1,  0));\n"
    "    vec3 h = Tap(fp, ivec2( 2,  0));\n"
    "    vec3 i = Tap(fp, ivec2(-1,  1));\n"
    "    vec3 j = Tap(fp, ivec2( 0,  1));\n"
    "    vec3 k = Tap(fp, ivec2( 1,  1));\n"
    "    vec3 l = Tap(fp, ivec2( 2,  1));\n"
    "    vec3 n = Tap(fp, ivec2( 0,  2));\n"
    "    vec3 o = Tap(fp, ivec2( 1,  2));\n"
    "\n"
    "    float bL = Luma(b), cL = Luma(c), eL = Luma(e), fL = Luma(f);\n"
    "    float gL = Luma(g), hL = Luma(h), iL = Luma(i), jL = Luma(j);\n"
    "    float kL = Luma(k), lL = Luma(l), nL = Luma(n), oL = Luma(o);\n"
    "\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    SetF(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);\n"
    "    SetF(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);\n"
    "    SetF(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);\n"
    "    SetF(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);\n"
    "\n"
    "    // normalize the direction, no direction falls back to horizontal\n"
    "    vec2 dir2 = dir * dir;\n"
    "    float dirR = dir2.x + dir2.y;\n"
    "    bool zro = dirR < 1.0 / 32768.0;\n"
    "    dirR = zro ? 1.0 : inversesqrt(dirR);\n"
    "    dir.x = zro ? 1.0 : dir.x;\n"
    "    dir *= dirR;\n"
    "\n"
    "    // shape the kernel: stretched along an edge, narrower across it\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;\n"
    "    float clp = 1.0 / lob;\n"
    "\n"
    "    vec3 aC = vec3(0.0);\n"
    "    float aW = 0.0;\n"
    "    TapF(aC, aW, vec2( 0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    TapF(aC, aW, vec2( 1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    TapF(aC, aW, vec2(-1.0,  1.0) - pp, dir, len2, lob, clp, i);\n"
    "    TapF(aC, aW, vec2( 0.0,  1.0) - pp, dir, len2, lob, clp, j);\n"
    "    TapF(aC, aW, vec2( 0.0,  0.0) - pp, dir, len2, lob, clp, f);\n"
    "    TapF(aC, aW, vec2(-1.0,  0.0) - pp, dir, len2, lob, clp, e);\n"
    "    TapF(aC, aW, vec2( 1.0,  1.0) - pp, dir, len2, lob, clp, k);\n"
    "    TapF(aC, aW, vec2( 2.0,  1.0) - pp, dir, len2, lob, clp, l);\n"
    "    TapF(aC, aW, vec2( 2.0,  0.0) - pp, dir, len2, lob, clp, h);\n"
    "    TapF(aC, aW, vec2( 1.0,  0.0) - pp, dir, len2, lob, clp, g);\n"
    "    TapF(aC, aW, vec2( 1.0,  2.0) - pp, dir, len2, lob, clp, o);\n"
    "    TapF(aC, aW, vec2( 0.0,  2.0) - pp, dir, len2, lob, clp, n);\n"
    "\n"
    "    // no ringing: keep inside the range of the nearest 2x2\n"
    "    vec3 min4 = min(min(f, g), min(j, k));\n"
    "    vec3 max4 = max(max(f, g), max(j, k));\n"
    "    fragColor = vec4(min(max4, max(min4, aC / aW)), 1.0);\n"
    "}\n";

// FsrRcasF() of ffx_fsr1.h with its denoise, in 32 bit floats
static const char *auroraFsrRcasShaderSource =
    "precision highp int;\n"
    "uniform sampler2D u_input;\n"
    "uniform float u_con; // exp2(-sharpness)\n"
    "out vec4 fragColor;\n"
    "\n"
    "#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))\n"
    "\n"
    "void main(void)\n"
    "{\n"
    "    ivec2 last = textureSize(u_input, 0) - 1;\n"
    "    ivec2 ip = ivec2(gl_FragCoord.xy);\n"
    "    //    b\n"
    "    //  d e f\n"
    "    //    h\n"
    "    vec3 b = texelFetch(u_input, clamp(ip + ivec2( 0, -1), ivec2(0), last), 0).rgb;\n"
    "    vec3 d = texelFetch(u_input, clamp(ip + ivec2(-1,  0), ivec2(0), last), 0).rgb;\n"
    "    vec3 e = texelFetch(u_input, ip, 0).rgb;\n"
    "    vec3 f = texelFetch(u_input, clamp(ip + ivec2( 1,  0), ivec2(0), last), 0).rgb;\n"
    "    vec3 h = texelFetch(u_input, clamp(ip + ivec2( 0,  1), ivec2(0), last), 0).rgb;\n"
    "\n"
    "    // luma times 2, for the noise detection\n"
    "    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);\n"
    "    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);\n"
    "    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);\n"
    "    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);\n"
    "    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);\n"
    "    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;\n"
    "    float range = max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL);\n"
    "    nz = clamp(abs(nz) / max(range, 1.0e-6), 0.0, 1.0);\n"
    "    nz = -0.5 * nz + 1.0;\n"
    "\n"
    "    // the most negative lobe that does not clip the ring\n"
    "    vec3 mn4 = min(min(b, d), min(f, h));\n"
    "    vec3 mx4 = max(max(b, d), max(f, h));\n"
    "    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1.0e-6));\n"
    "    vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, vec3(-1.0e-6));\n"
    "    vec3 lobeRGB = max(-hitMin, hitMax);\n"
    "    float lobe = max(-FSR_RCAS_LIMIT, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * u_con;\n"
    "    lobe *= nz;\n"
    "\n"
    "    vec3 pix = (lobe * (b + d + h + f) + e) / (4.0 * lobe + 1.0);\n"
    "    fragColor = vec4(pix, 1.0);\n"
    "}\n";

/*
====================
R_AuroraLinkProgram
====================
*/
static uint32_t R_AuroraLinkProgram(const char *vertexSource, const char *fragmentSource, const char *name)
{
    const uint32_t vs = R_AuroraCompileShader(GL_VERTEX_SHADER, vertexSource);
    const uint32_t fs = R_AuroraCompileShader(GL_FRAGMENT_SHADER, fragmentSource);

    if (!vs || !fs) {
        return 0;
    }

    uint32_t program = qglCreateProgram();
    qglAttachShader(program, vs);
    qglAttachShader(program, fs);
    qglLinkProgram(program);
    qglDeleteShader(vs);
    qglDeleteShader(fs);

    GLint linked = 0;
    qglGetProgramiv(program, GL_LINK_STATUS, &linked);

    if (!linked) {
        char log[1024];
        GLsizei length = 0;
        qglGetProgramInfoLog(program, sizeof(log), &length, log);
        log[sizeof(log) - 1] = '\0';
        common->Warning("[Aurora FSR]: %s program link failed: %s", name, log);
        qglDeleteProgram(program);
        return 0;
    }

    return program;
}

/*
====================
R_AuroraCreateTarget
====================
*/
static bool R_AuroraCreateTarget(int width, int height, uint32_t *texture, uint32_t *framebuffer, uint32_t *depthBuffer)
{
    qglGenTextures(1, texture);
    qglBindTexture(GL_TEXTURE_2D, *texture);
    qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    qglBindTexture(GL_TEXTURE_2D, 0);

    qglGenFramebuffers(1, framebuffer);
    qglBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
    qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);

    if (depthBuffer) {
        qglGenRenderbuffers(1, depthBuffer);
        qglBindRenderbuffer(GL_RENDERBUFFER, *depthBuffer);
        qglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        qglBindRenderbuffer(GL_RENDERBUFFER, 0);
        qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, *depthBuffer);
    }

    const GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        common->Warning("[Aurora FSR]: framebuffer %d x %d incomplete: 0x%x", width, height, status);
        return false;
    }

    return true;
}

idAuroraFsr::idAuroraFsr()
    : inputWidth(0),
    inputHeight(0),
    outputWidth(0),
    outputHeight(0),
    easuProgram(0),
    easuInputUniform(-1),
    easuCon0Uniform(-1),
    easuInputMaxUniform(-1),
    rcasProgram(0),
    rcasInputUniform(-1),
    rcasConUniform(-1),
    easuTexture(0),
    easuFramebuffer(0),
    outputTexture(0),
    outputDepthBuffer(0),
    outputFramebuffer(0)
{
}

/*
====================
idAuroraFsr::CreatePrograms
====================
*/
bool idAuroraFsr::CreatePrograms(void)
{
    if (easuProgram && rcasProgram) {
        return true;
    }

    easuProgram = R_AuroraLinkProgram(auroraFsrVertexShaderSource, auroraFsrEasuShaderSource, "EASU");
    rcasProgram = R_AuroraLinkProgram(auroraFsrVertexShaderSource, auroraFsrRcasShaderSource, "RCAS");

    if (!easuProgram || !rcasProgram) {
        return false;
    }

    easuInputUniform = qglGetUniformLocation(easuProgram, "u_input");
    easuCon0Uniform = qglGetUniformLocation(easuProgram, "u_con0");
    easuInputMaxUniform = qglGetUniformLocation(easuProgram, "u_inputMax");
    rcasInputUniform = qglGetUniformLocation(rcasProgram, "u_input");
    rcasConUniform = qglGetUniformLocation(rcasProgram, "u_con");

    return true;
}

/*
====================
idAuroraFsr::Build
====================
*/
bool idAuroraFsr::Build(int newInputWidth, int newInputHeight, int newOutputWidth, int newOutputHeight)
{
    if (IsBuilt() && newInputWidth == inputWidth && newInputHeight == inputHeight
        && newOutputWidth == outputWidth && newOutputHeight == outputHeight) {
        return true;
    }

    Destroy();

    if (!CreatePrograms()) {
        return false;
    }

    inputWidth = newInputWidth;
    inputHeight = newInputHeight;
    outputWidth = newOutputWidth;
    outputHeight = newOutputHeight;

    const bool built = R_AuroraCreateTarget(outputWidth, outputHeight, &easuTexture, &easuFramebuffer, NULL)
                       && R_AuroraCreateTarget(outputWidth, outputHeight, &outputTexture, &outputFramebuffer, &outputDepthBuffer);

    qglBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!built) {
        Destroy();
        return false;
    }

    GL_CheckErrors();

    common->Printf("[Aurora FSR]: %d x %d up to %d x %d\n", inputWidth, inputHeight, outputWidth, outputHeight);

    return true;
}

/*
====================
idAuroraFsr::Destroy

Drops the targets, the programs stay.
====================
*/
void idAuroraFsr::Destroy(void)
{
    if (easuFramebuffer) {
        qglDeleteFramebuffers(1, &easuFramebuffer);
    }

    if (easuTexture) {
        qglDeleteTextures(1, &easuTexture);
    }

    if (outputFramebuffer) {
        qglDeleteFramebuffers(1, &outputFramebuffer);
    }

    if (outputTexture) {
        qglDeleteTextures(1, &outputTexture);
    }

    if (outputDepthBuffer) {
        qglDeleteRenderbuffers(1, &outputDepthBuffer);
    }

    easuFramebuffer = easuTexture = 0;
    outputFramebuffer = outputTexture = outputDepthBuffer = 0;
    inputWidth = inputHeight = outputWidth = outputHeight = 0;
}

/*
====================
idAuroraFsr::Shutdown
====================
*/
void idAuroraFsr::Shutdown(void)
{
    Destroy();

    if (easuProgram) {
        qglDeleteProgram(easuProgram);
    }

    if (rcasProgram) {
        qglDeleteProgram(rcasProgram);
    }

    easuProgram = rcasProgram = 0;
}

/*
====================
idAuroraFsr::Upscale

The caller has the GL state set for a fullscreen pass: no depth, stencil,
blend, cull or scissor, texture unit 0 active.
====================
*/
void idAuroraFsr::Upscale(uint32_t sceneTexture, float sharpness)
{
    qglViewport(0, 0, outputWidth, outputHeight);

    // EASU: the scene up to the output size
    qglBindFramebuffer(GL_FRAMEBUFFER, easuFramebuffer);
    qglUseProgram(easuProgram);
    qglBindTexture(GL_TEXTURE_2D, sceneTexture);
    qglUniform1i(easuInputUniform, 0);
    qglUniform4f(easuCon0Uniform,
                 (float)inputWidth / outputWidth,
                 (float)inputHeight / outputHeight,
                 0.5f * inputWidth / outputWidth - 0.5f,
                 0.5f * inputHeight / outputHeight - 0.5f);
    qglUniform2f(easuInputMaxUniform, (float)(inputWidth - 1), (float)(inputHeight - 1));
    qglDrawArrays(GL_TRIANGLES, 0, 3);

    // RCAS: sharpened into the output buffer
    qglBindFramebuffer(GL_FRAMEBUFFER, outputFramebuffer);
    qglUseProgram(rcasProgram);
    qglBindTexture(GL_TEXTURE_2D, easuTexture);
    qglUniform1i(rcasInputUniform, 0);
    qglUniform1f(rcasConUniform, idMath::Pow(2.0f, -sharpness));
    qglDrawArrays(GL_TRIANGLES, 0, 3);

    qglBindTexture(GL_TEXTURE_2D, 0);
    qglUseProgram(0);
}

#endif
