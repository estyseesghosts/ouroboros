/*
 * engine/platform/plat_gl_int.h -- the GPU presenter's private state, shared
 * by plat_gl.c (screens) and plat_gl3d.c (the 3D rasteriser).
 */
#ifndef EIDOS_PLAT_GL_INT_H
#define EIDOS_PLAT_GL_INT_H

#if defined(__wasm__)
/* The wasm32 build: every GL entry point is an import the host program
 * answers (tools/wasm_gl_imports.py), so the prototypes are declared and
 * the loader (plat_gl.c) takes their addresses. */
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "plat_gl.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* SDL_opengl.h leaves the buffer sub-update entry to the extension header;
 * the steady-state vertex upload needs it on every platform, through the
 * table like the rest. */
#ifndef PFNGLBUFFERSUBDATAPROC
typedef void (APIENTRYP PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset,
                                                GLsizeiptr size, const void *data);
#endif

#define GL_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM2IPROC, glUniform2i) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORM4IPROC, glUniform4i) \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv) \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
    X(PFNGLUNIFORM1FVPROC, glUniform1fv) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLMAPBUFFERPROC, glMapBuffer) \
    X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)     X(PFNGLBLENDCOLORPROC, glBlendColor)     X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)     X(PFNGLCOLORMASKIPROC, glColorMaski)     X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv)     X(PFNGLCLEARBUFFERUIVPROC, glClearBufferuiv)     X(PFNGLUNIFORM1UIPROC, glUniform1ui)

/* Readbacks in flight per stream (3D frame, each 2D screen).  The oldest is
 * collected when a new one starts: with 3 the CPU maps a buffer the GPU
 * filled two frames ago, which on nouveau (Switch) is long finished. */
#define EIDOS_GL_PBO_RING 3

/* A per-frame error check: debug builds ask the driver, release builds
 * return success without touching it (no per-frame glGetError).  The init
 * checks call glGetError directly and stay as they are. */
#ifdef NDEBUG
#define EIDOS_GL_CHECK() (1)
#else
#define EIDOS_GL_CHECK() (glGetError() == GL_NO_ERROR)
#endif

typedef struct GlFb {
    GLuint fbo, color, depth;     /* depth: a DEPTH24_STENCIL8 texture       */
    GLuint pid;                   /* R8UI: 0x80 | polygon ID of an opaque pixel,
                                     else the clear plane's ID (edge marking) */
    GLuint post_fbo, color2;      /* the edge-marking pass writes here      */
    int w, h;
    GLuint ms_fbo, ms_color, ms_depth, ms_id;   /* the multisampled target (anti-aliasing) */
    int ms_w, ms_h;
} GlFb;

struct EidosGl {
    SDL_Window *window;
    SDL_GLContext ctx;
#define X(T, N) T N;
    GL_FUNCS(X)
#undef X

    /* the screen presenter */
    GLuint prog;
    GLuint vao;
    GLint u_rect, u_tex, u_texsize, u_dstsize;
    GLuint tex[2];
    int tw[2], th[2];

    /* the 3D composite (a screen whose 3D layer the GPU draws) */
    GLuint prog3;
    GLint u3_rect, u3_tex, u3_behind, u3_g3d, u3_texsize, u3_dstsize,
          u3_bmode, u3_bfac, u3_dil, u3_zero, u3_hasz, u3_sent;
    int s3d;                       /* screen index, -1 none                */
    int s3d2;                      /* the screen whose 3D the 2D shader
                                      composes (phase 3), -1 none          */
    int s3d2_w, s3d2_h;            /* ... and the size it is shown at      */
    int s3d_from2d;                /* s3d's screens come from the 2D shader */
    int edge_mode;                 /* outlines: 0 off, 1 thin, 2 DS pixel  */
    GLuint behind_tex;
    GLuint zero_tex;               /* the screen with a black 3D layer, or 0 */
    int has_zero;                  /* zero_tex holds this frame's          */
    int behind_w, behind_h;
    int bmode;                     /* master brightness: 0 off, 1 up, 2 down */
    float bfac;                    /* 0..1                                  */
    int dilate;                    /* mask dilated by a pixel (1) or exact (0) */

    /* the high-resolution display capture (eidos_gl_capture) */
    GLuint progc;
    GLint uc_rect, uc_a, uc_b, uc_eva, uc_evb, uc_bflip, uc_mode, uc_bmode,
          uc_bfac;
    GLuint cap_fbo, capa_tex, cap_tex[2], cap_src[3];
    int cap_w, cap_h, cap_cur, cap_valid;
    int cap_screen;                /* screen that shows the capture, -1 none */
    int cap_bmode;
    float cap_bfac;

    /* the 3D rasteriser (plat_gl3d.c) */
    int have3d;
    GLuint p3d;
    GLint u_texture, u_alpharef, u_highlight, u_toontab, u_wbuf, u_pass;
    GLuint pedge;                  /* the edge-marking pass (0: unavailable) */
    GLint ue_col, ue_id, ue_dep, ue_step, ue_cid, ue_cdep, ue_edge, ue_variant;
    GLuint pfog;                   /* the fog pass (0: unavailable)          */
    GLint uf_col, uf_id, uf_dep, uf_fog, uf_table, uf_offset, uf_step,
          uf_aonly, uf_clear;
    GlFb fb[3];                    /* display, DS readback, AA fallback */
    GLint integer_samples;
    int es;                        /* OpenGL ES (a phone): no 2x fallback AA */
    unsigned read_serial;          /* snapshot serial last read back         */
    GLuint pbo[EIDOS_GL_PBO_RING]; /* async readback targets, a ring: each
                                      is allocated once and read back only
                                      EIDOS_GL_PBO_RING - 1 calls after its
                                      glReadPixels, so mapping it does not
                                      wait for the GPU                       */
    int pbo_head;                  /* the next one glReadPixels fills        */
    int pbo_pending;               /* how many hold a read not yet collected */
    int read_valid;
    unsigned fb0_serial;           /* snapshot eidos_gl_read3d_1x drew into
                                      fb[0] at DS size, so the display
                                      render of it at that size is skipped */
    int fb0_valid;
    GLuint vbo3, vao3;
    GLuint vao_edge;             /* the edge/fog pass's VAO: empty, no attribs */
    int vao3_ready;              /* vao3's attrib pointers are set (once)       */
    size_t vbo3_cap;             /* bytes the vbo3 store holds                  */
    GLuint *gtex;
    unsigned n_gtex, cap_gtex;
    unsigned *gtex_key;            /* per GL texture: what it holds (w, h and
                                      a hash of the texels), so an unchanged
                                      texture is not uploaded again          */
    unsigned *gtex_w, *gtex_h;      /* per GL texture: the size on the GPU, so a
                                       same-size change is a SubImage and only
                                       a size change re-specifies             */
    unsigned long long tex_up_bytes; /* texel bytes uploaded since init
                                       (texup; Stream G buckets sample it)   */
    unsigned tex_up_n;               /* uploads since init (texupn)          */
    unsigned tex_respec;             /* size-change re-specs since init: the
                                       TexImage path below; steady frames
                                       add 0 (Stream G buckets sample it)    */
    int edge_broken;             /* an edge/fog fault turned outlines off for
                                   the session (the frame stays unmarked) */
    unsigned tex_serial;           /* snapshot whose textures are uploaded   */
    int tex_serial_valid;
    float *vtx;
    size_t cap_vtx;
};

/* plat_gl3d.c */
int eidos_gl3d_init(EidosGl *g);
void eidos_gl3d_destroy(EidosGl *g);
GLuint eidos_gl_compile_program(EidosGl *g, const char *vs, const char *fs);

#endif
