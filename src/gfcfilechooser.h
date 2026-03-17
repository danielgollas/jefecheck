#ifndef GFCFILECHOOSER_H
#define GFCFILECHOOSER_H

#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_File_Chooser.H>
#include <string>
#include <cstring>
#include <cstdio>

// Global filename buffer used by the old save_input_file callback
extern char gFilename[2048];

// Drop-in replacement for the global Fl_File_Chooser that uses native dialogs.
// Keeps the same API (show/shown/value/count/type/filter/label/callback/preview)
// so existing call sites don't need to change.

class NativeFileChooser {
public:
    NativeFileChooser(const char *dir = ".", const char *pat = nullptr, int type = 0, const char *title = "Choose a file") {
        _type = Fl_Native_File_Chooser::BROWSE_FILE;
        _title = title ? title : "";
        _filter = pat ? pat : "";
        _directory = dir ? dir : ".";
        _shown = false;
        _value = "";
        _count = 0;
        _callback = nullptr;
        _userdata = nullptr;
    }

    // Fl_File_Chooser compatibility API
    void type(int t) {
        switch (t) {
            case Fl_File_Chooser::SINGLE:
                _type = Fl_Native_File_Chooser::BROWSE_FILE; break;
            case Fl_File_Chooser::MULTI:
                _type = Fl_Native_File_Chooser::BROWSE_MULTI_FILE; break;
            case Fl_File_Chooser::CREATE:
                _type = Fl_Native_File_Chooser::BROWSE_SAVE_FILE; break;
            case Fl_File_Chooser::DIRECTORY:
                _type = Fl_Native_File_Chooser::BROWSE_DIRECTORY; break;
            default:
                _type = Fl_Native_File_Chooser::BROWSE_FILE; break;
        }
    }

    int type() const {
        switch (_type) {
            case Fl_Native_File_Chooser::BROWSE_FILE: return Fl_File_Chooser::SINGLE;
            case Fl_Native_File_Chooser::BROWSE_MULTI_FILE: return Fl_File_Chooser::MULTI;
            case Fl_Native_File_Chooser::BROWSE_SAVE_FILE: return Fl_File_Chooser::CREATE;
            case Fl_Native_File_Chooser::BROWSE_DIRECTORY: return Fl_File_Chooser::DIRECTORY;
            default: return Fl_File_Chooser::SINGLE;
        }
    }

    void filter(const char *f) {
        _origFilter = f ? f : "";
        _filter = convertFilter(_origFilter);
    }
    const char *filter() const { return _origFilter.c_str(); }
    void label(const char *l) { _title = l ? l : ""; }
    void preview(int) {} // no-op, native dialogs have their own preview
    void directory(const char *d) { _directory = d ? d : "."; }
    const char *directory() const { return _directory.c_str(); }

    void callback(void (*cb)(Fl_File_Chooser*, void*), void *data = nullptr) {
        _callback = cb;
        _userdata = data;
    }

    void show() {
        Fl_Native_File_Chooser fc;
        fc.title(_title.c_str());
        fc.type(_type);
        if (_type == Fl_Native_File_Chooser::BROWSE_SAVE_FILE)
            fc.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
        if (!_filter.empty()) fc.filter(_filter.c_str());
        if (!_directory.empty()) fc.directory(_directory.c_str());

        _values.clear();
        _count = 0;
        _value = "";

        int result = fc.show();
        printf("NativeFileChooser: show() returned %d\n", result);
        if (result == 0) {
            _count = fc.count();
            for (int i = 0; i < _count; i++)
                _values.push_back(fc.filename(i));
            if (_count > 0)
                _value = _values[0];
            printf("NativeFileChooser: selected %d file(s): %s\n", _count, _value.c_str());
            // Update global gFilename for legacy code that reads it
            strncpy(gFilename, _value.c_str(), sizeof(gFilename) - 1);
            gFilename[sizeof(gFilename) - 1] = '\0';
        } else {
            printf("NativeFileChooser: cancelled or error (result=%d)\n", result);
        }

        _shown = false; // native dialog is modal, already done
    }

    int shown() const { return _shown ? 1 : 0; }
    int count() const { return _count; }

    const char *value(int index = 0) const {
        if (index < (int)_values.size())
            return _values[index].c_str();
        return _value.c_str();
    }

private:
    int _type;
    std::string _title;
    std::string _filter;     // converted for Fl_Native_File_Chooser
    std::string _origFilter; // original FLTK-style filter
    std::string _directory;
    bool _shown;
    std::string _value;
    std::vector<std::string> _values;
    int _count;
    void (*_callback)(Fl_File_Chooser*, void*);
    void *_userdata;

    // Convert FLTK filter format to Fl_Native_File_Chooser format.
    // FLTK Fl_File_Chooser: "Description (*.{ext1,ext2})\tDesc2 (*.ext)"
    // Fl_Native_File_Chooser: "Description\t*.{ext1,ext2}\nDesc2\t*.ext"
    // The native chooser supports *.{ext1,ext2} directly, no expansion needed.
    static std::string convertFilter(const std::string &fltkFilter) {
        if (fltkFilter.empty()) return "";

        std::string result;
        // Split on \t for multiple filter groups
        size_t pos = 0;
        while (pos < fltkFilter.size()) {
            size_t tabPos = fltkFilter.find('\t', pos);
            std::string segment = (tabPos == std::string::npos)
                ? fltkFilter.substr(pos)
                : fltkFilter.substr(pos, tabPos - pos);

            // Trim whitespace
            size_t start = segment.find_first_not_of(" \t");
            if (start != std::string::npos) segment = segment.substr(start);
            size_t end = segment.find_last_not_of(" \t");
            if (end != std::string::npos) segment = segment.substr(0, end + 1);

            if (!segment.empty()) {
                // Extract pattern from "Description (pattern)" format
                size_t parenOpen = segment.find('(');
                size_t parenClose = segment.rfind(')');

                if (parenOpen != std::string::npos && parenClose != std::string::npos) {
                    std::string desc = segment.substr(0, parenOpen);
                    size_t de = desc.find_last_not_of(" \t");
                    if (de != std::string::npos) desc = desc.substr(0, de + 1);
                    std::string pattern = segment.substr(parenOpen + 1, parenClose - parenOpen - 1);

                    if (!result.empty()) result += "\n";
                    result += desc + "\t" + pattern;
                } else {
                    if (!result.empty()) result += "\n";
                    result += segment;
                }
            }

            pos = (tabPos == std::string::npos) ? fltkFilter.size() : tabPos + 1;
        }

        if (!result.empty()) result += "\n";
        result += "All Files\t*";

        return result;
    }
};

#endif
