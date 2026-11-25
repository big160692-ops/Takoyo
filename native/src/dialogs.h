#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include "theme.h"

class Config;
class ProjectStore;
class TextButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QWidget;

class PalettePickerDialog : public QDialog {
    Q_OBJECT
public:
    PalettePickerDialog(Config *config, QWidget *parent = nullptr);

    QString selectedPalette() const { return selected_; }

signals:
    void palettePreviewRequested(const QString &id);

private:
    void buildGrid();
    void onSelectionChanged(const QString &id);
    void applySelection();

    Config *config_;
    QList<theme::PaletteInfo> palettes_;
    QList<QWidget *> swatches_;
    QGridLayout *grid_ = nullptr;
    TextButton *useButton_ = nullptr;
    QString selected_;
};

class ProjectFilesDialog : public QDialog {
    Q_OBJECT
public:
    ProjectFilesDialog(ProjectStore *store, const QString &projectId, const QString &projectName,
                       QWidget *parent = nullptr);

    void reload();

private:
    void openSelected();
    bool saveCurrent();
    void createFile();
    void deleteCurrent();
    bool maybeSave();

    ProjectStore *store_;
    QString projectId_;
    QListWidget *list_ = nullptr;
    QPlainTextEdit *editor_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *title_ = nullptr;
    QString currentPath_;
    bool loading_ = false;
};

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    NewProjectDialog(ProjectStore *store, QWidget *parent = nullptr);
    QString createdId() const { return createdId_; }

private:
    void create();

    ProjectStore *store_;
    QLineEdit *name_ = nullptr;
    QLineEdit *description_ = nullptr;
    QString createdId_;
};

class ModelPickerDialog : public QDialog {
    Q_OBJECT
public:
    ModelPickerDialog(Config *config, QWidget *parent = nullptr);

    QString provider() const { return provider_; }
    QString model() const { return model_; }
    bool wantsDownload() const { return download_; }

signals:
    void settingsRequested();

private:
    void updateSelection();
    void apply();

    Config *config_;
    QListWidget *localList_ = nullptr;
    QListWidget *cloudList_ = nullptr;
    QLabel *localDetail_ = nullptr;
    QLabel *cloudDetail_ = nullptr;
    TextButton *useButton_ = nullptr;
    QString provider_;
    QString model_;
    bool download_ = false;
    bool syncing_ = false;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Config *config, QWidget *parent = nullptr);

private:
    void loadProvider();
    void save();

    Config *config_;
    QComboBox *provider_ = nullptr;
    QLineEdit *model_ = nullptr;
    QLabel *keyLabel_ = nullptr;
    QLineEdit *key_ = nullptr;
    QWidget *keyRow_ = nullptr;
    TextButton *replaceKey_ = nullptr;
    QComboBox *imageProvider_ = nullptr;
    QComboBox *imageSize_ = nullptr;
    QComboBox *preset_ = nullptr;
    QDoubleSpinBox *temperature_ = nullptr;
    QSpinBox *maxTokens_ = nullptr;
    QCheckBox *autoApply_ = nullptr;
    QCheckBox *allowCodeExecution_ = nullptr;
    QSpinBox *apiPort_ = nullptr;
    bool syncingPreset_ = false;
};
