// Export Review Package dialog: output path, "Include media" with its size,
// progress and cancel (docs/superpowers/specs/2026-09-14-review-package-design.md).
#ifndef JEFECHECK_QT_REVIEW_PACKAGE_DIALOG_H
#define JEFECHECK_QT_REVIEW_PACKAGE_DIALOG_H

#include <QDialog>
#include <QString>

#include <functional>

#include "ReviewPackage_qt.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class ReviewPackageDialog_Qt : public QDialog {
    Q_OBJECT
public:
    /** Fills the export input for @a outPath (MainWindow_Qt::gatherPackageInput). */
    using Gather = std::function<bool(const QString& outPath, bool includeMedia,
                                      jefe::qt::package::ExportInput& input, QString* message)>;

    ReviewPackageDialog_Qt(Gather gather, qint64 mediaBytes, QWidget* parent = nullptr);
    ~ReviewPackageDialog_Qt() override;

    void setOutputPath(const QString& path);
    void setIncludeMedia(bool on);
    /** Starts exporting, as the Export button does. */
    void startExport();
    /** Stops a running export, as Cancel does while exporting. */
    void cancelExport();
    bool isRunning() const { return running_; }
    int progressPercent() const;
    QString lastMessage() const { return lastMessage_; }

    /** "0 B", "1.5 KB", "5.0 GB". */
    static QString formatBytes(qint64 bytes);

protected:
    void reject() override;

private:
    void stepOnce();
    void finishWith(bool ok, const QString& message);
    void setInputsEnabled(bool enabled);
    void updateSizeLabel();

    Gather gather_;
    qint64 mediaBytes_ = 0;
    QLineEdit* pathEdit_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QCheckBox* includeMediaCheck_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    jefe::qt::package::Exporter exporter_;
    QString outPath_;
    bool running_ = false;
    QString lastMessage_;
};

#endif
