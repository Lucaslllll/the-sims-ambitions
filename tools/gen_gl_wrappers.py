#!/usr/bin/env python3
"""Generate android/app/src/main/cpp/hle/gl_exports.inc: pass-through wrappers
for every OpenGL ES function the game imports.

Functions listed in INTERCEPTED are implemented by hand in gles.cpp; for those
we only emit the HLE_EXPORT line. Every wrapper calls through a function
pointer resolved at context creation (ES1 from libGLESv1_CM, ES2+ from
libGLESv3), so the same exports work for both of the game's render paths.
"""
import os
import sys

# name: (return type, "params")
SIGS = {
    'glActiveTexture': ('void', 'GLenum texture'),
    'glAlphaFunc': ('void', 'GLenum func, GLfloat ref'),
    'glAttachShader': ('void', 'GLuint program, GLuint shader'),
    'glBindAttribLocation': ('void', 'GLuint program, GLuint index, const GLchar* name'),
    'glBindFramebuffer': ('void', 'GLenum target, GLuint framebuffer'),
    'glBindFramebufferOES': ('void', 'GLenum target, GLuint framebuffer'),
    'glBindRenderbuffer': ('void', 'GLenum target, GLuint renderbuffer'),
    'glBindRenderbufferOES': ('void', 'GLenum target, GLuint renderbuffer'),
    'glBindTexture': ('void', 'GLenum target, GLuint texture'),
    'glBlendFunc': ('void', 'GLenum sfactor, GLenum dfactor'),
    'glCheckFramebufferStatus': ('GLenum', 'GLenum target'),
    'glCheckFramebufferStatusOES': ('GLenum', 'GLenum target'),
    'glClear': ('void', 'GLbitfield mask'),
    'glClearColor': ('void', 'GLfloat r, GLfloat g, GLfloat b, GLfloat a'),
    'glClearColorx': ('void', 'GLfixed r, GLfixed g, GLfixed b, GLfixed a'),
    'glClientActiveTexture': ('void', 'GLenum texture'),
    'glColor4f': ('void', 'GLfloat r, GLfloat g, GLfloat b, GLfloat a'),
    'glColor4ub': ('void', 'GLubyte r, GLubyte g, GLubyte b, GLubyte a'),
    'glColorMask': ('void', 'GLboolean r, GLboolean g, GLboolean b, GLboolean a'),
    'glColorPointer': ('void', 'GLint size, GLenum type, GLsizei stride, const void* ptr'),
    'glCompileShader': ('void', 'GLuint shader'),
    'glCompressedTexImage2D': ('void', 'GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void* data'),
    'glCompressedTexSubImage2D': ('void', 'GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void* data'),
    'glCreateProgram': ('GLuint', 'void'),
    'glCreateShader': ('GLuint', 'GLenum type'),
    'glCullFace': ('void', 'GLenum mode'),
    'glCurrentPaletteMatrixOES': ('void', 'GLuint index'),
    'glDeleteFramebuffers': ('void', 'GLsizei n, const GLuint* framebuffers'),
    'glDeleteFramebuffersOES': ('void', 'GLsizei n, const GLuint* framebuffers'),
    'glDeleteProgram': ('void', 'GLuint program'),
    'glDeleteRenderbuffers': ('void', 'GLsizei n, const GLuint* renderbuffers'),
    'glDeleteRenderbuffersOES': ('void', 'GLsizei n, const GLuint* renderbuffers'),
    'glDeleteShader': ('void', 'GLuint shader'),
    'glDeleteTextures': ('void', 'GLsizei n, const GLuint* textures'),
    'glDepthFunc': ('void', 'GLenum func'),
    'glDepthMask': ('void', 'GLboolean flag'),
    'glDisable': ('void', 'GLenum cap'),
    'glDisableClientState': ('void', 'GLenum array'),
    'glDisableVertexAttribArray': ('void', 'GLuint index'),
    'glDrawArrays': ('void', 'GLenum mode, GLint first, GLsizei count'),
    'glDrawElements': ('void', 'GLenum mode, GLsizei count, GLenum type, const void* indices'),
    'glEnable': ('void', 'GLenum cap'),
    'glEnableClientState': ('void', 'GLenum array'),
    'glEnableVertexAttribArray': ('void', 'GLuint index'),
    'glFogf': ('void', 'GLenum pname, GLfloat param'),
    'glFogfv': ('void', 'GLenum pname, const GLfloat* params'),
    'glFogx': ('void', 'GLenum pname, GLfixed param'),
    'glFramebufferRenderbuffer': ('void', 'GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb'),
    'glFramebufferRenderbufferOES': ('void', 'GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb'),
    'glFramebufferTexture2D': ('void', 'GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level'),
    'glFramebufferTexture2DOES': ('void', 'GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level'),
    'glFrontFace': ('void', 'GLenum mode'),
    'glGenFramebuffers': ('void', 'GLsizei n, GLuint* ids'),
    'glGenFramebuffersOES': ('void', 'GLsizei n, GLuint* ids'),
    'glGenRenderbuffers': ('void', 'GLsizei n, GLuint* ids'),
    'glGenRenderbuffersOES': ('void', 'GLsizei n, GLuint* ids'),
    'glGenTextures': ('void', 'GLsizei n, GLuint* ids'),
    'glGetError': ('GLenum', 'void'),
    'glGetFloatv': ('void', 'GLenum pname, GLfloat* data'),
    'glGetIntegerv': ('void', 'GLenum pname, GLint* data'),
    'glGetProgramInfoLog': ('void', 'GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog'),
    'glGetProgramiv': ('void', 'GLuint program, GLenum pname, GLint* params'),
    'glGetRenderbufferParameteriv': ('void', 'GLenum target, GLenum pname, GLint* params'),
    'glGetRenderbufferParameterivOES': ('void', 'GLenum target, GLenum pname, GLint* params'),
    'glGetShaderInfoLog': ('void', 'GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog'),
    'glGetShaderSource': ('void', 'GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* source'),
    'glGetShaderiv': ('void', 'GLuint shader, GLenum pname, GLint* params'),
    'glGetUniformLocation': ('GLint', 'GLuint program, const GLchar* name'),
    'glIsEnabled': ('GLboolean', 'GLenum cap'),
    'glLinkProgram': ('void', 'GLuint program'),
    'glLoadIdentity': ('void', 'void'),
    'glLoadMatrixf': ('void', 'const GLfloat* m'),
    'glMatrixIndexPointerOES': ('void', 'GLint size, GLenum type, GLsizei stride, const void* ptr'),
    'glMatrixMode': ('void', 'GLenum mode'),
    'glMultMatrixf': ('void', 'const GLfloat* m'),
    'glOrthof': ('void', 'GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f'),
    'glPixelStorei': ('void', 'GLenum pname, GLint param'),
    'glPolygonOffset': ('void', 'GLfloat factor, GLfloat units'),
    'glPopMatrix': ('void', 'void'),
    'glPushMatrix': ('void', 'void'),
    'glRenderbufferStorage': ('void', 'GLenum target, GLenum internalformat, GLsizei width, GLsizei height'),
    'glRenderbufferStorageOES': ('void', 'GLenum target, GLenum internalformat, GLsizei width, GLsizei height'),
    'glRotatef': ('void', 'GLfloat angle, GLfloat x, GLfloat y, GLfloat z'),
    'glScalef': ('void', 'GLfloat x, GLfloat y, GLfloat z'),
    'glScissor': ('void', 'GLint x, GLint y, GLsizei width, GLsizei height'),
    'glShadeModel': ('void', 'GLenum mode'),
    'glShaderSource': ('void', 'GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length'),
    'glTexCoordPointer': ('void', 'GLint size, GLenum type, GLsizei stride, const void* ptr'),
    'glTexEnvf': ('void', 'GLenum target, GLenum pname, GLfloat param'),
    'glTexEnvfv': ('void', 'GLenum target, GLenum pname, const GLfloat* params'),
    'glTexEnvi': ('void', 'GLenum target, GLenum pname, GLint param'),
    'glTexEnvx': ('void', 'GLenum target, GLenum pname, GLfixed param'),
    'glTexImage2D': ('void', 'GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels'),
    'glTexParameteri': ('void', 'GLenum target, GLenum pname, GLint param'),
    'glTexParameterx': ('void', 'GLenum target, GLenum pname, GLfixed param'),
    'glTexSubImage2D': ('void', 'GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels'),
    'glTranslatef': ('void', 'GLfloat x, GLfloat y, GLfloat z'),
    'glUniform1f': ('void', 'GLint location, GLfloat v0'),
    'glUniform1fv': ('void', 'GLint location, GLsizei count, const GLfloat* value'),
    'glUniform1i': ('void', 'GLint location, GLint v0'),
    'glUniform2f': ('void', 'GLint location, GLfloat v0, GLfloat v1'),
    'glUniform2fv': ('void', 'GLint location, GLsizei count, const GLfloat* value'),
    'glUniform3f': ('void', 'GLint location, GLfloat v0, GLfloat v1, GLfloat v2'),
    'glUniform3fv': ('void', 'GLint location, GLsizei count, const GLfloat* value'),
    'glUniform4f': ('void', 'GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3'),
    'glUniform4fv': ('void', 'GLint location, GLsizei count, const GLfloat* value'),
    'glUniformMatrix4fv': ('void', 'GLint location, GLsizei count, GLboolean transpose, const GLfloat* value'),
    'glUseProgram': ('void', 'GLuint program'),
    'glValidateProgram': ('void', 'GLuint program'),
    'glVertexAttribPointer': ('void', 'GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* ptr'),
    'glVertexPointer': ('void', 'GLint size, GLenum type, GLsizei stride, const void* ptr'),
    'glViewport': ('void', 'GLint x, GLint y, GLsizei width, GLsizei height'),
    'glWeightPointerOES': ('void', 'GLint size, GLenum type, GLsizei stride, const void* ptr'),
}

# Implemented by hand in gles.cpp (they still get a function pointer slot).
INTERCEPTED = {
    'glCompressedTexImage2D', 'glCompressedTexSubImage2D',
    'glFramebufferRenderbuffer', 'glFramebufferRenderbufferOES',
    'glShaderSource', 'glGetError',
}


def arg_names(params):
    if params.strip() == 'void':
        return ''
    names = []
    for p in params.split(','):
        p = p.strip()
        name = p.replace('*', ' ').split()[-1]
        names.append(name)
    return ', '.join(names)


def main(out_dir):
    hdr = ['// Generated by tools/gen_gl_wrappers.py - do not edit.', '']
    hdr.append('#define GL_FUNCS(X) \\')
    for n in sorted(SIGS):
        ret, params = SIGS[n]
        hdr.append(f'    X({n}, {ret}, ({params})) \\')
    hdr.append('')
    open(os.path.join(out_dir, 'gl_funcs.inc'), 'w').write('\n'.join(hdr) + '\n')

    lines = ['// Generated by tools/gen_gl_wrappers.py - do not edit.', '']
    for n in sorted(SIGS):
        ret, params = SIGS[n]
        if n in INTERCEPTED:
            continue
        call = f'gl::fn.{n}({arg_names(params)})'
        body = f'return {call};' if ret != 'void' else f'{call};'
        lines.append(f'static {ret} w_{n}({params}) {{ GLTRACE("{n}"); {body} }}')
    lines.append('')
    for n in sorted(SIGS):
        target = f'h_{n}' if n in INTERCEPTED else f'w_{n}'
        lines.append(f'HLE_EXPORT({n}, {target});')
    open(os.path.join(out_dir, 'gl_exports.inc'), 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main(sys.argv[1])
