// Loads GLAD function pointers without Qt's OpenGL headers in the same TU
// (macOS <OpenGL/glext.h> collides with glad's ARB redefinitions).
#include <glad/glad.h>

bool jefecheck_loadGladGL() {
    return gladLoadGL() != 0;
}
