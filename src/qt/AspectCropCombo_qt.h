// AspectCropCombo_Qt — an editable aspect-ratio combo box whose drop-down
// popup folds in a "Crop" checkbox at the top. It consolidates what used to
// be two separate plate-card controls (a QComboBox of aspect ratios and a
// standalone "Crop" toggle button) into a single, tighter widget while
// keeping aspect and crop as orthogonal operations.
//
// Why a custom popup instead of an item in the model? QComboBox's default
// view is a list model; injecting a live checkbox into it (and keeping the
// popup open after a click on that one row) fights the model/view machinery.
// Instead we override showPopup() to display our own Qt::Popup QFrame that
// owns a QCheckBox + a QListWidget of the ratio presets. Qt::Popup gives us
// click-outside-to-dismiss for free and lets us decide per-widget whether a
// click closes the popup (a ratio does; the checkbox does not).
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

#include <QComboBox>
#include <QString>
#include <QStringList>

class QCheckBox;
class QFrame;
class QListWidget;

class AspectCropCombo_Qt : public QComboBox {
    Q_OBJECT
public:
    explicit AspectCropCombo_Qt(QWidget* parent = nullptr);
    ~AspectCropCombo_Qt() override;

    // Populate the ratio preset list (e.g. "original", "16:9", ...). These
    // are the EXACT strings gfcPlateGUI_Qt::setAspectChoice parses via
    // aspectFromString, so they must round-trip unchanged.
    void setPresets(const QStringList& presets);

    // The current ratio string, e.g. "2.39:1". This is the raw stored
    // aspect — never the decorated button-face text (the crop indicator
    // glyph is presentation-only and is stripped from this value).
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

    // Show our custom popup frame instead of QComboBox's default view.
    void showPopup() override;
    void hidePopup() override;

signals:
    void aspectChanged(const QString& aspect);
    void cropToggled(bool on);

private:
    // Re-renders the button face: the current ratio text, prefixed with a
    // subtle glyph when crop is active. Never mutates the stored aspect.
    void updateFace();

    // Commit the editable line-edit's typed value as a custom ratio.
    void onEditingFinished();

    QString      aspect_;                 // raw stored ratio string
    bool         crop_       = false;
    bool         updatingFace_ = false;   // guards updateFace's setEditText

    QFrame*      popupFrame_ = nullptr;
    QCheckBox*   cropCheck_  = nullptr;
    QListWidget* ratioList_  = nullptr;
};

#endif  // JEFECHECK_QT_ASPECT_CROP_COMBO_H
