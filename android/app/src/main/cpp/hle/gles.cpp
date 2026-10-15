// OpenGL ES exports + EAGLContext.
//
// The game renders into its own framebuffer object whose colour renderbuffer
// comes from -[EAGLContext renderbufferStorage:fromDrawable:]. We swap that
// colour attachment for a texture ("drawable texture") so the platform layer
// can draw it rotated/scaled to the Android window on -presentRenderbuffer:.
#include "hle/gles.h"

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_RENDERBUFFER_BINDING
#define GL_RENDERBUFFER_BINDING 0x8CA7
#endif

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <cstring>
#include <string>
#include <vector>

#include "hle/config.h"
#include "hle/foundation.h"
#include "hle/nsstring.h"
#include "hle/pvrtc.h"
#include "objc/runtime.h"
#include "platform/platform.h"

#ifdef TS3A_GL_TRACE
#define GLTRACE(name) LOGD("gl: %s", name)
#else
#define GLTRACE(name) do {} while (0)
#endif

namespace gl {

Fns fn;
bool g_es1 = true;

static void* g_lib_v1 = nullptr;
static void* g_lib_v3 = nullptr;

static void* lookup(const char* name) {
    void* p = nullptr;
    if (g_es1 && g_lib_v1) p = dlsym(g_lib_v1, name);
    if (!p && g_lib_v3) p = dlsym(g_lib_v3, name);
    if (!p && g_lib_v1) p = dlsym(g_lib_v1, name);
    return p;
}

static void missing_fn() { LOGW("call to GL function not available in this context"); }

bool load(bool es1) {
    g_es1 = es1;
    if (!g_lib_v1) g_lib_v1 = dlopen("libGLESv1_CM.so", RTLD_NOW);
    if (!g_lib_v3) g_lib_v3 = dlopen("libGLESv3.so", RTLD_NOW);
    if (!g_lib_v3) g_lib_v3 = dlopen("libGLESv2.so", RTLD_NOW);
    int missing = 0;
#define X(name, ret, params)                          \
    fn.name = (ret(*) params)lookup(#name);           \
    if (!fn.name) {                                   \
        fn.name = (ret(*) params)(void*)missing_fn;   \
        missing++;                                    \
        LOGW("GL function %s not found", #name);      \
    }
    GL_FUNCS(X)
#undef X
    LOGI("GL functions loaded for %s (%d missing)", es1 ? "ES1.1" : "ES2/3", missing);
    return true;
}

// --- drawable state --------------------------------------------------------------
static GLuint g_drawable_rb = 0;   // renderbuffer the game thinks is the screen
static GLuint g_drawable_tex = 0;  // texture we attach instead
static GLuint g_game_fbo = 0;
static int g_drawable_w = 0, g_drawable_h = 0;

GLuint drawable_texture() { return g_drawable_tex; }
int drawable_width() { return g_drawable_w; }
int drawable_height() { return g_drawable_h; }

}  // namespace gl

using gl::fn;

// --- intercepted functions ----------------------------------------------------------
static GLenum h_glGetError() {
    GLenum e = fn.glGetError();
    if (e) LOGW("glGetError -> %#x", e);
    return e;
}

static bool is_pvrtc(GLenum f, bool* two) {
    switch (f) {
        case 0x8C00: case 0x8C02: *two = false; return true;  // 4bpp
        case 0x8C01: case 0x8C03: *two = true; return true;   // 2bpp
    }
    return false;
}

static void h_glCompressedTexImage2D(GLenum target, GLint level, GLenum fmt, GLsizei w, GLsizei h, GLint border,
                                     GLsizei size, const void* data) {
    bool two;
    if (is_pvrtc(fmt, &two)) {
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        if (data && w > 0 && h > 0) pvrtc_decode((const uint8_t*)data, w, h, two, rgba.data());
        GLint align;
        fn.glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
        fn.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        fn.glTexImage2D(target, level, GL_RGBA, w, h, border, GL_RGBA, GL_UNSIGNED_BYTE, data ? rgba.data() : nullptr);
        fn.glPixelStorei(GL_UNPACK_ALIGNMENT, align);
        return;
    }
    fn.glCompressedTexImage2D(target, level, fmt, w, h, border, size, data);
}

static void h_glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                        GLenum fmt, GLsizei size, const void* data) {
    bool two;
    if (is_pvrtc(fmt, &two)) {
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        pvrtc_decode((const uint8_t*)data, w, h, two, rgba.data());
        fn.glTexSubImage2D(target, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        return;
    }
    fn.glCompressedTexSubImage2D(target, level, x, y, w, h, fmt, size, data);
}

static void framebuffer_renderbuffer(bool oes, GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb) {
    if (rb && rb == gl::g_drawable_rb && attachment == GL_COLOR_ATTACHMENT0) {
        GLint cur = 0;
        fn.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &cur);
        gl::g_game_fbo = cur;
        LOGI("attaching drawable texture %u to game FBO %d", gl::g_drawable_tex, cur);
        if (oes) fn.glFramebufferTexture2DOES(target, attachment, GL_TEXTURE_2D, gl::g_drawable_tex, 0);
        else fn.glFramebufferTexture2D(target, attachment, GL_TEXTURE_2D, gl::g_drawable_tex, 0);
        return;
    }
    if (oes) fn.glFramebufferRenderbufferOES(target, attachment, rbtarget, rb);
    else fn.glFramebufferRenderbuffer(target, attachment, rbtarget, rb);
}
static void h_glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rt, GLuint rb) {
    framebuffer_renderbuffer(false, t, a, rt, rb);
}
static void h_glFramebufferRenderbufferOES(GLenum t, GLenum a, GLenum rt, GLuint rb) {
    framebuffer_renderbuffer(true, t, a, rt, rb);
}

// The game's ES2 skinning shader uses HLSL-style int4/float4 attributes, which
// GLSL ES rejects; rewrite them into valid GLSL.
static void replace_all(std::string& s, const std::string& a, const std::string& b) {
    for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
}
static void h_glShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < count; i++) src.append(strings[i], lengths && lengths[i] >= 0 ? lengths[i] : strlen(strings[i]));
    if (src.find("int4") != std::string::npos || src.find("float4") != std::string::npos) {
        replace_all(src, "highp int4 vs_SkinIndices", "highp vec4 vs_SkinIndices");
        replace_all(src, "float4", "vec4");
        for (const char* c : {"x", "y", "z", "w"}) {
            replace_all(src, std::string("g_SkinPalette[vs_SkinIndices.") + c + "]",
                        std::string("g_SkinPalette[int(vs_SkinIndices.") + c + ")]");
        }
    }
    const GLchar* p = src.c_str();
    fn.glShaderSource(shader, 1, &p, nullptr);
}

// Pass-through wrappers and the HLE_EXPORT table.
#include "hle/gl_exports.inc"

// =====================================================================================
// EAGLContext
// =====================================================================================
namespace {

struct EAGLContextObj {
    Class isa;
    int api;
    id sharegroup;
};

// EAGL keeps a current context per thread; EGL lets a context be current on
// only one thread at a time, so ownership of the single game context moves to
// whichever thread asks for it (the main thread is asked to let go if needed).
thread_local id t_current_context = nil;
std::mutex g_owner_mutex;
std::condition_variable g_owner_cv;
bool g_owned = true;  // platform makes the context current on the main thread at start
pthread_t g_owner = 0;
bool g_owner_init = false;

void acquire_context() {
    pthread_t self = pthread_self();
    std::unique_lock<std::mutex> lk(g_owner_mutex);
    if (!g_owner_init) {
        g_owner_init = true;
        g_owner = self;  // first caller is the thread that created the context
    }
    if (g_owned && pthread_equal(g_owner, self)) return;
    if (g_owned) {
        pthread_t owner = g_owner;
        lk.unlock();
        LOGI("GL context requested by thread %ld; asking owner to release", (long)gettid());
        if (foundation::is_main_thread()) {
            LOGE("main thread wants the GL context but a game thread holds it");
        } else {
            foundation::run_on_main(
                [owner]() {
                    std::lock_guard<std::mutex> g(g_owner_mutex);
                    if (g_owned && pthread_equal(g_owner, pthread_self())) {
                        platform::gl_make_current(false);
                        g_owned = false;
                        LOGI("main thread released the GL context");
                    }
                },
                true);
        }
        lk.lock();
        g_owner_cv.wait_for(lk, std::chrono::seconds(5), [] { return !g_owned; });
        if (g_owned) LOGE("timed out waiting for the GL context");
    }
    platform::gl_make_current(true);
    g_owned = true;
    g_owner = self;
}

void release_context() {
    std::lock_guard<std::mutex> lk(g_owner_mutex);
    if (g_owned && pthread_equal(g_owner, pthread_self())) {
        platform::gl_make_current(false);
        g_owned = false;
        g_owner_cv.notify_all();
    }
}

id EAGL_initWithAPI(id self, SEL, int api) {
    bool want_es1 = hle::config().gl_api == "OpenGLES1";
    if ((api == 1) != want_es1) {
        LOGW("EAGLContext initWithAPI:%d refused (platform context is %s)", api, want_es1 ? "ES1" : "ES2");
        objc::release(self);
        return nil;
    }
    ((EAGLContextObj*)self)->api = api;
    LOGI("EAGLContext created with API %d", api);
    return self;
}
id EAGL_initWithAPI_sharegroup(id self, SEL s, int api, id) { return EAGL_initWithAPI(self, s, api); }
int EAGL_API(id self, SEL) { return ((EAGLContextObj*)self)->api; }
id EAGL_sharegroup(id, SEL) { return nil; }
BOOL EAGL_setCurrentContext(Class, SEL, id ctx) {
    LOGI("setCurrentContext:%p on thread %ld", ctx, (long)gettid());
    if (ctx) acquire_context();
    else release_context();
    if (ctx != t_current_context) {
        objc::retain(ctx);
        objc::release(t_current_context);
        t_current_context = ctx;
    }
    return YES;
}
id EAGL_currentContext(Class, SEL) { return t_current_context; }

BOOL EAGL_renderbufferStorage_fromDrawable(id, SEL, GLenum target, id layer) {
    int w = 0, h = 0;
    platform::drawable_size(layer, &w, &h);
    GLint rb = 0;
    fn.glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
    bool es1 = gl::g_es1;
    LOGI("renderbufferStorage:fromDrawable: rb=%d size %dx%d", rb, w, h);
    if (es1) fn.glRenderbufferStorageOES(target, GL_RGBA8_OES, w, h);
    else fn.glRenderbufferStorage(target, GL_RGBA8, w, h);
    gl::g_drawable_rb = rb;
    gl::g_drawable_w = w;
    gl::g_drawable_h = h;
    // Texture we render into instead of the renderbuffer.
    GLint prev_tex = 0;
    fn.glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
    if (!gl::g_drawable_tex) fn.glGenTextures(1, &gl::g_drawable_tex);
    fn.glBindTexture(GL_TEXTURE_2D, gl::g_drawable_tex);
    fn.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    fn.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    fn.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    fn.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    fn.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    fn.glBindTexture(GL_TEXTURE_2D, prev_tex);
    return YES;
}

BOOL EAGL_presentRenderbuffer(id, SEL, GLenum) {
    platform::present_frame(gl::g_drawable_tex, gl::g_drawable_w, gl::g_drawable_h);
    return YES;
}

}  // namespace

namespace gl {

static id g_kEAGLColorFormatRGB565 = nil, g_kEAGLColorFormatRGBA8 = nil,
          g_kEAGLDrawablePropertyColorFormat = nil, g_kEAGLDrawablePropertyRetainedBacking = nil;

void init_classes() {
#include "gles_abi.inc"
    g_kEAGLColorFormatRGB565 = ns::make_constant("EAGLColorFormatRGB565");
    g_kEAGLColorFormatRGBA8 = ns::make_constant("EAGLColorFormatRGBA8");
    g_kEAGLDrawablePropertyColorFormat = ns::make_constant("EAGLDrawablePropertyColorFormat");
    g_kEAGLDrawablePropertyRetainedBacking = ns::make_constant("EAGLDrawablePropertyRetainedBacking");
    hle::register_symbol("_kEAGLColorFormatRGB565", &g_kEAGLColorFormatRGB565);
    hle::register_symbol("_kEAGLColorFormatRGBA8", &g_kEAGLColorFormatRGBA8);
    hle::register_symbol("_kEAGLDrawablePropertyColorFormat", &g_kEAGLDrawablePropertyColorFormat);
    hle::register_symbol("_kEAGLDrawablePropertyRetainedBacking", &g_kEAGLDrawablePropertyRetainedBacking);

    Class C = objc::define_class("EAGLContext", "NSObject", sizeof(EAGLContextObj));
    objc::add_method(C, "initWithAPI:", (void*)EAGL_initWithAPI);
    objc::add_method(C, "initWithAPI:sharegroup:", (void*)EAGL_initWithAPI_sharegroup);
    objc::add_method(C, "API", (void*)EAGL_API);
    objc::add_method(C, "sharegroup", (void*)EAGL_sharegroup);
    objc::add_method(C, "renderbufferStorage:fromDrawable:", (void*)EAGL_renderbufferStorage_fromDrawable);
    objc::add_method(C, "presentRenderbuffer:", (void*)EAGL_presentRenderbuffer);
    objc::add_class_method(C, "setCurrentContext:", (void*)EAGL_setCurrentContext);
    objc::add_class_method(C, "currentContext", (void*)EAGL_currentContext);
}

}  // namespace gl
