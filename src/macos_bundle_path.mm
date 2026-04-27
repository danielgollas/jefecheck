// Cocoa-backed implementation of the bundle Resources lookup.
//
// Two strategies, in order:
//
//   1. NSBundle.mainBundle.resourcePath — works for the canonical
//      "open Foo.app" launch and for direct exec of the binary inside
//      the bundle, but NOT when the binary is launched via a symlink
//      (NSBundle uses argv[0] for its bundle-wrapper detection).
//
//   2. _NSGetExecutablePath + canonicalize — returns the real on-disk
//      path of the running binary regardless of how it was invoked.
//      Walk up looking for `<X>.app/Contents/MacOS/<binary>`; if the
//      shape matches, derive Resources from the .app root.
//
// Returns an empty string when the executable isn't a .app at all
// (raw-binary dev builds), so callers can fall back.
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>

#include <filesystem>
#include <string>

namespace {

std::string fromNSBundle() {
    @autoreleasepool {
        NSBundle* main = [NSBundle mainBundle];
        if (!main) return std::string();
        NSString* bundlePath = [main bundlePath];
        if (!bundlePath || ![bundlePath hasSuffix:@".app"]) return std::string();
        NSString* resourcePath = [main resourcePath];
        if (!resourcePath) return std::string();
        std::string s([resourcePath UTF8String]);
        if (s.empty() || s.back() != '/') s.push_back('/');
        return s;
    }
}

std::string fromExecutablePath() {
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return std::string();

    std::filesystem::path p(buf);
    try {
        p = std::filesystem::canonical(p);
    } catch (...) {
        return std::string();
    }

    // Expected shape: <something>.app/Contents/MacOS/<binary>
    auto contents = p.parent_path().parent_path();
    auto appDir   = contents.parent_path();
    if (p.parent_path().filename() != "MacOS") return std::string();
    if (contents.filename() != "Contents")     return std::string();
    if (appDir.extension() != ".app")          return std::string();

    return (contents / "Resources").string() + "/";
}

}  // namespace

std::string getMacBundleResourcePath() {
    std::string s = fromNSBundle();
    if (!s.empty()) return s;
    return fromExecutablePath();
}
