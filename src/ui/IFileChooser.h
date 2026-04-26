// Abstract native file chooser. Replaces NativeFileChooser (FLTK) directly.
#ifndef JEFECHECK_UI_IFILECHOOSER_H
#define JEFECHECK_UI_IFILECHOOSER_H

#include <memory>
#include <string>
#include <vector>

namespace jefe::ui {

enum class FileChooserMode {
    OpenFile,
    OpenFiles,    // multi-select
    SaveFile,
    OpenDirectory,
};

struct FileChooserOptions {
    FileChooserMode mode = FileChooserMode::OpenFile;
    std::string title;
    std::string startDirectory;
    std::string defaultFilename;     // SaveFile only
    // Filter format: "Description (*.ext1,*.ext2)" — multiple allowed.
    std::vector<std::string> filters;
};

class IFileChooser {
public:
    virtual ~IFileChooser() = default;

    // Block until the user picks (or cancels). Returns picked paths,
    // or empty vector if cancelled. For single-select modes, vector
    // has 0 or 1 entry.
    virtual std::vector<std::string> show(const FileChooserOptions& opts) = 0;

    static std::unique_ptr<IFileChooser> create();
};

}  // namespace jefe::ui

#endif
