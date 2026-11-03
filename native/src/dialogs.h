#pragma once

#include <QDialog>
#include <QString>

class Config;
class ProjectStore;
class TextButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QWidget;

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
    QCheckBox *autoApply_ = nullptr;
    QSpinBox *apiPort_ = nullptr;
};
