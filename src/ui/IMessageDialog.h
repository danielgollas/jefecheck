// Abstract message dialog. Replaces fl_alert / fl_choice / fl_message / fl_input.
#ifndef JEFECHECK_UI_IMESSAGEDIALOG_H
#define JEFECHECK_UI_IMESSAGEDIALOG_H

#include <optional>
#include <string>

namespace jefe::ui {

enum class DialogIcon {
    Info,
    Warning,
    Error,
    Question,
};

class IMessageDialog {
public:
    virtual ~IMessageDialog() = default;

    // Modal, single-button. Drop-in for fl_alert / fl_message.
    virtual void show(DialogIcon icon, const std::string& message) = 0;

    // Modal, up to 3 buttons. Returns the index of the clicked button (0..n-1),
    // or -1 if the dialog was dismissed. Drop-in for fl_choice.
    //   buttonCancel — leftmost button, often "Cancel"
    //   buttonOk     — middle button (required)
    //   buttonAlt    — optional rightmost button; pass empty string to omit
    virtual int choose(DialogIcon icon,
                       const std::string& message,
                       const std::string& buttonCancel,
                       const std::string& buttonOk,
                       const std::string& buttonAlt = "") = 0;

    // Modal text input. Returns the entered string, or std::nullopt if cancelled.
    virtual std::optional<std::string> prompt(const std::string& message,
                                              const std::string& defaultValue = "") = 0;

    static IMessageDialog& instance();
    static void setInstance(IMessageDialog* impl);
};

}  // namespace jefe::ui

#endif
