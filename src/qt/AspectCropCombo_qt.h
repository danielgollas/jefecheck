// AspectCropCombo_Qt — a click-to-open aspect-ratio control whose drop-down
// popup folds in a "Crop" checkbox at the top. It consolidates what used to
// be two separate plate-card controls (a QComboBox of aspect ratios and a
// standalone "Crop" toggle button) into a single, tighter widget while
// keeping aspect and crop as orthogonal operations.
//
// Why a QToolButton (not a QComboBox)? An editable QComboBox only opens its
// popup when the drop-down ARROW is clicked — a click on the line-edit body
// just places a text cursor. Users (and the Appium tests) click the widget's
// body/center, so the popup never opened: the feature was effectively dead.
// You also cannot make one surface both "click to open popup" AND "click to
// type a custom ratio" — they conflict. So the face is now a plain button
// that shows the current aspect text (plus a painted crop-marks icon when
// crop is on) and opens the popup on click; custom-ratio entry moved OFF the
// face and INTO the popup (a QLineEdit at the bottom).
//
// Why a custom popup instead of an item in a model? Injecting a live checkbox
// into a QComboBox's list view (and keeping the popup open after a click on
// that one row) fights the model/view machinery. Instead we own a Qt::Popup
// QFrame holding a QCheckBox + a QListWidget of ratio presets + a custom-ratio
// QLineEdit. Qt::Popup gives us click-outside-to-dismiss for free and lets us
// decide per-widget whether a click closes the popup (a ratio does; the
// checkbox does not).
//
// Renderer semantics this widget exposes (see gfcPlate::calculatePolySizesCropEtc):
//   - aspect set, crop OFF → quad is RESHAPED to that ratio (anamorphic), no bars.
//   - aspect set, crop ON  → image stays native, black letterbox bars mask it.
//   - "original" aspect    → native, no bars.
// So crop is NOT redundant with aspect — it selects which behavior a chosen
// ratio produces. Both must stay independently reachable.
//
// TU separation: this is a PURE Qt widget. It must NOT include any rendering-
// chain header (gfcPlate.h, gfcplatemanager.h, gfcplategui_qt.h, glad/glad.h,
// system OpenGL, etc.) — glad and Qt's GL headers refuse to share a TU on
// macOS, so the project enforces a strict bridge boundary. PlateCard_qt.cpp
// owns all rendering-chain wiring and connects to this widget's signals.
#ifndef JEFECHECK_QT_ASPECT_CROP_COMBO_H
#define JEFECHECK_QT_ASPECT_CROP_COMBO_H

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QToolButton>

class QCheckBox;
class QFrame;
class QLineEdit;
class QListWidget;

class AspectCropCombo_Qt : public QToolButton {
    Q_OBJECT
public:
    explicit AspectCropCombo_Qt(QWidget* parent = nullptr);
    ~AspectCropCombo_Qt() override;

    // Populate the ratio preset list (e.g. "original", "16:9", ...). These
    // are the EXACT strings gfcPlateGUI_Qt::setAspectChoice parses via
    // aspectFromString, so they must round-trip unchanged.
    void setPresets(const QStringList& presets);

    // The current ratio string, e.g. "2.39:1". This is the raw stored
    // aspect — the face text is always exactly this string (never decorated;
    // the crop indicator is a painted icon, not part of the text).
    QString currentAspect() const;
    bool cropChecked() const;

    // Signal-free setters — PlateCard's refreshFromState() pushes plate
    // state in without looping back through aspectChanged/cropToggled.
    // Mirrors the QSignalBlocker discipline used elsewhere in PlateCard.
    void setCurrentAspect(const QString& aspect);
    void setCropChecked(bool on);

    // PlateCard sets these on the inner checkbox so UI-test locators keep
    // resolving `plate.<idx>.crop.button` / accessibleName "Crop" once the
    // popup is open.
    void setCropObjectName(const QString& name);
    void setCropAccessibleName(const QString& name);
    void setCropToolTip(const QString& tip);

signals:
    void aspectChanged(const QString& aspect);
    void cropToggled(bool on);

protected:
    // Stamps the reopen-flicker guard whenever the popup frame hides —
    // including the Qt::Popup outside-click dismissal, which we never route
    // through our own hide() calls.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Open the custom popup frame (positioned below — or above, if it would
    // spill off the screen — the button, clamped to the screen edges).
    void openPopup();

    // Click handler: toggles the popup. Guarded against the reopen-flicker
    // race (see .cpp) so clicking the button while the popup is open closes
    // it and does NOT immediately reopen.
    void onClicked();

    // Re-render the face: text is always the raw aspect string; the icon is
    // a painted crop-marks pixmap when crop is on, cleared otherwise. Never
    // mutates the stored aspect.
    void updateFace();

    QString      aspect_;               // raw stored ratio string (== face text)
    bool         crop_       = false;

    QFrame*      popupFrame_ = nullptr;
    QCheckBox*   cropCheck_  = nullptr;
    QListWidget* ratioList_  = nullptr;
    QLineEdit*   customEdit_ = nullptr;

    // Reopen-flicker guard: a click on the button while a Qt::Popup is shown
    // is first delivered as an outside-click that dismisses the popup, then a
    // second click event reaches the button and would reopen it. We stamp
    // this timer when the popup hides and ignore opens that arrive within a
    // short window after a dismissal.
    QElapsedTimer justClosed_;
};

#endif  // JEFECHECK_QT_ASPECT_CROP_COMBO_H
