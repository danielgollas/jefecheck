// Loads GLAD function pointers without dragging Qt's OpenGL headers into the
// same translation unit. macOS's OpenGL.framework + Qt's QOpenGLWidget chain
// pull in <OpenGL/glext.h>, which collides with glad's redefinition of the
// same `glClampColorARB` / `glDrawBuffersARB` / etc. symbols. Keeping glad
// confined to this file (no Qt headers) sidesteps the conflict.
#include <glad/glad.h>

bool jefecheck_loadGladGL() {
    return gladLoadGL() != 0;
}
