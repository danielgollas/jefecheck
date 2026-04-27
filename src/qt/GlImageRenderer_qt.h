// Aspect-fit textured-quad renderer. Header is intentionally Qt-free and
// glad-free so it can be included from GlViewport_qt.cpp without dragging
// glad/glext.h into the same TU as Qt's QOpenGLWidget on macOS (they
// collide on `glClampColorARB` / `glDrawBuffersARB` / etc.).
#ifndef JEFECHECK_QT_GL_IMAGE_RENDERER_H
#define JEFECHECK_QT_GL_IMAGE_RENDERER_H

class GlImageRenderer {
public:
    GlImageRenderer();
    ~GlImageRenderer();

    // Upload a BGRA8 pixel buffer (row 0 = top of image) to a
    // GL_TEXTURE_RECTANGLE_ARB texture. Must be called with a current GL
    // context. The GLuint handle is stored as unsigned to keep the header
    // free of GL types.
    void uploadBGRA8(const void* pixels, int w, int h);

    // Clears the viewport and, if a texture is uploaded, draws it
    // aspect-fit-centered into a (vpW x vpH) viewport.
    void render(int vpW, int vpH);

    bool hasImage() const { return texture_ != 0; }
    int  imageWidth()  const { return imgW_; }
    int  imageHeight() const { return imgH_; }

    // Releases the texture. Must be called with the same GL context active.
    void releaseGL();

private:
    unsigned int texture_ = 0;
    int imgW_ = 0;
    int imgH_ = 0;
};

#endif
