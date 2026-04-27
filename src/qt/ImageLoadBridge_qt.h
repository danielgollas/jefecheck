// Thin bridge from a file path to a BGRA8 byte buffer, using
// gfcImageLoaderOIIO under the hood. Lives in its own TU so callers don't
// have to pull glad (via gfcimageloader.h → gfcglframeinfo.h) into the
// same translation unit as Qt's QOpenGLWidget headers.
#ifndef JEFECHECK_QT_IMAGE_LOAD_BRIDGE_H
#define JEFECHECK_QT_IMAGE_LOAD_BRIDGE_H

#include <string>
#include <vector>

namespace jefe::qt {

// Loads an image file via OIIO and returns it as 8-bit BGRA pixels
// (row 0 = top of image). Returns true on success; on failure leaves
// outBytes empty and outErr populated.
bool loadImageBGRA8(const std::string& path,
                    std::vector<unsigned char>& outBytes,
                    int& outW, int& outH,
                    std::string& outErr);

}  // namespace jefe::qt

#endif
