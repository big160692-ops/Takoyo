#include "dialogs.h"

#include "config.h"
#include "icons.h"
#include "store.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <functional>

namespace {

const char *kDialogStyle = R"(
QDialog { background: #FFFFFF; }
QLabel { color: #0B0B0D; background: transparent; }
QLineEdit, QPlainTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #FFFFFF;
    border: 1.3px solid #0B0B0D;
    border-radius: 10px;
    padding: 7px 10px;
    color: #0B0B0D;
    selection-background-color: #DEDEDF;
}
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1.6px solid #0B0B0D; }
QComboBox { padding-right: 30px; }
QComboBox::drop-down { border: none; width: 28px; }
QComboBox QAbstractItemView {
    background: #FFFFFF;
    border: 1px solid #E7E7E9;
    selection-background-color: #EDEDEF;
    selection-color: #0B0B0D;
    outline: none;
}
QListWidget {
    background: #FFFFFF;
    border: 1.2px solid #E7E7E9;
    border-radius: 12px;
    outline: none;
    padding: 4px;
}
QListWidget::item { padding: 6px 8px; border-radius: 8px; color: #3A3B3E; }
QListWidget::item:hover { background: #F4F4F5; }
QListWidget::item:selected { background: #EDEDEF; color: #0B0B0D; }
QCheckBox { color: #0B0B0D; spacing: 8px; background: transparent; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1.3px solid #0B0B0D; border-radius: 4px; background: #FFFFFF; }
QCheckBox::indicator:checked { background: #0B0B0D; }
QCheckBox::indicator:disabled { border-color: #B9B9BD; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { background: #DEDEDF; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)";

class Combo : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF arrow(width() - 25, (height() - 12) / 2.0, 12, 12);
        icons::paint(painter, QStringLiteral("chevron-down"), arrow, theme::map(theme::MUTED));
    }
};

QLabel *textLabel(const QString &text, int size, const QString &weight, const QColor &color,
                  QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme::font(size, weight));
    theme::style(label, QStringLiteral("color:%1;background:transparent;").arg(color.name()));
    return label;
}

void styleDialog(QDialog *dialog)
{
    theme::style(dialog, QString::fromLatin1(kDialogStyle));
    dialog->setFont(theme::font(12));
    dialog->setModal(true);
}

} // namespace

// ---------------------------------------------------------------------------
// ProjectFilesDialog
// ---------------------------------------------------------------------------
ProjectFilesDialog::ProjectFilesDialog(ProjectStore *store, const QString &projectId,
                                       const QString &projectName, QWidget *parent)
    : QDialog(parent)
    , store_(store)
    , projectId_(projectId)
{
    setWindowTitle(QStringLiteral("Project files — %1").arg(projectName));
    resize(900, 620);
    styleDialog(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(22, 20, 22, 18);
    outer->setSpacing(12);

    auto *head = new QHBoxLayout();
    head->setSpacing(10);
    auto *heading = textLabel(projectName, 17, QStringLiteral("semibold"), theme::INK, this);
    head->addWidget(heading);
    head->addStretch(1);
    status_ = textLabel(QString(), 10, QStringLiteral("regular"), theme::MUTED, this);
    head->addWidget(status_);
    outer->addLayout(head);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    list_ = new QListWidget(split);
    list_->setMinimumWidth(240);
    list_->setMaximumWidth(330);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    split->addWidget(list_);

    auto *right = new QWidget(split);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    title_ = textLabel(QStringLiteral("Pick a file to read or edit"), 11, QStringLiteral("medium"),
                       theme::MUTED, right);
    rightLayout->addWidget(title_);

    editor_ = new QPlainTextEdit(right);
    editor_->setFont(theme::mono(11));
    editor_->setPlaceholderText(QStringLiteral("Choose a file on the left to open it here."));
    editor_->setTabChangesFocus(false);
    rightLayout->addWidget(editor_, 1);

    auto *actions = new QHBoxLayout();
    actions->setSpacing(8);
    auto *newButton = new TextButton(QStringLiteral("New file"), TextButton::Variant::Secondary, 32, right);
    newButton->setFontSize(10);
    auto *deleteButton = new TextButton(QStringLiteral("Delete"), TextButton::Variant::Danger, 32, right);
    deleteButton->setFontSize(10);
    auto *saveButton = new TextButton(QStringLiteral("Save"), TextButton::Variant::Primary, 32, right);
    saveButton->setFontSize(10);
    actions->addWidget(newButton);
    actions->addWidget(deleteButton);
    actions->addStretch(1);
    actions->addWidget(saveButton);
    rightLayout->addLayout(actions);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    auto *bottom = new QHBoxLayout();
    auto *hint = textLabel(QStringLiteral("Files are saved straight into the project folder."), 9,
                           QStringLiteral("regular"), theme::FAINT, this);
    bottom->addWidget(hint);
    bottom->addStretch(1);
    auto *closeButton = new TextButton(QStringLiteral("Close"), TextButton::Variant::Secondary, 34, this);
    closeButton->setFontSize(10);
    bottom->addWidget(closeButton);
    outer->addLayout(bottom);

    connect(closeButton, &QAbstractButton::clicked, this, &QDialog::accept);
    connect(saveButton, &QAbstractButton::clicked, this, [this]() {
        if (saveCurrent())
            reload();
    });
    connect(newButton, &QAbstractButton::clicked, this, &ProjectFilesDialog::createFile);
    connect(deleteButton, &QAbstractButton::clicked, this, &ProjectFilesDialog::deleteCurrent);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { openSelected(); });

    reload();
}

void ProjectFilesDialog::reload()
{
    loading_ = true;
    const QString keep = currentPath_;
    list_->clear();
    const QStringList paths = store_->filePaths(projectId_);
    for (const QString &path : paths)
        list_->addItem(path);
    if (!keep.isEmpty()) {
        const auto matches = list_->findItems(keep, Qt::MatchExactly);
        if (!matches.isEmpty())
            list_->setCurrentItem(matches.first());
    }
    if (list_->currentRow() < 0 && list_->count() > 0)
        list_->setCurrentRow(0);
    loading_ = false;
    if (list_->count() == 0) {
        editor_->clear();
        title_->setText(QStringLiteral("This project is empty"));
        status_->setText(QStringLiteral("Use “New file” to add one."));
    } else {
        openSelected();
    }
}

bool ProjectFilesDialog::maybeSave()
{
    if (!editor_->document()->isModified() || currentPath_.isEmpty())
        return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Save changes?"),
                                              QStringLiteral("Save your changes to “%1” first?").arg(currentPath_),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                              QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveCurrent();
    return true;
}

void ProjectFilesDialog::openSelected()
{
    if (loading_)
        return;
    QListWidgetItem *item = list_->currentItem();
    if (!item)
        return;
    const QString path = item->text();
    if (path == currentPath_)
        return;
    if (!maybeSave()) {
        loading_ = true;
        const auto matches = list_->findItems(currentPath_, Qt::MatchExactly);
        if (!matches.isEmpty())
            list_->setCurrentItem(matches.first());
        loading_ = false;
        return;
    }
    QString error;
    const QString text = store_->readText(projectId_, path, &error);
    editor_->document()->setModified(false);
    if (!error.isEmpty()) {
        currentPath_ = path;
        editor_->clear();
        title_->setText(path);
        status_->setText(error);
        return;
    }
    currentPath_ = path;
    title_->setText(path);
    editor_->setPlainText(text);
    editor_->document()->setModified(false);
    status_->setText(QStringLiteral("%1 · %2 bytes").arg(QFileInfo(path).suffix().toUpper()).arg(text.toUtf8().size()));
}

bool ProjectFilesDialog::saveCurrent()
{
    if (currentPath_.isEmpty())
        return false;
    QString error;
    if (!store_->writeText(projectId_, currentPath_, editor_->toPlainText(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not save"), error);
        return false;
    }
    editor_->document()->setModified(false);
    status_->setText(QStringLiteral("Saved %1").arg(currentPath_));
    return true;
}

void ProjectFilesDialog::createFile()
{
    bool ok = false;
    const QString path = QInputDialog::getText(this, QStringLiteral("New file"),
                                               QStringLiteral("Path (for example src/main.py)"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || path.trimmed().isEmpty())
        return;
    if (store_->fileExists(projectId_, path)) {
        QMessageBox::information(this, QStringLiteral("Already there"),
                                 QStringLiteral("“%1” already exists — open it from the list.").arg(path));
        return;
    }
    QString error;
    if (!store_->writeText(projectId_, path, QString(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not create"), error);
        return;
    }
    currentPath_ = path.trimmed();
    reload();
    editor_->setFocus();
    status_->setText(QStringLiteral("Created %1").arg(currentPath_));
}

void ProjectFilesDialog::deleteCurrent()
{
    if (currentPath_.isEmpty())
        return;
    const auto answer = QMessageBox::question(this, QStringLiteral("Delete file?"),
                                              QStringLiteral("“%1” will move to Takoyo's trash.").arg(currentPath_),
                                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!store_->deleteFile(projectId_, currentPath_, &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not delete"), error);
        return;
    }
    currentPath_.clear();
    editor_->document()->setModified(false);
    reload();
    status_->setText(QStringLiteral("Moved to trash"));
}

// ---------------------------------------------------------------------------
// NewProjectDialog
// ---------------------------------------------------------------------------
NewProjectDialog::NewProjectDialog(ProjectStore *store, QWidget *parent)
    : QDialog(parent)
    , store_(store)
{
    setWindowTitle(QStringLiteral("New project"));
    setFixedSize(480, 268);
    styleDialog(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 22, 24, 20);
    outer->setSpacing(14);

    outer->addWidget(textLabel(QStringLiteral("New project"), 20, QStringLiteral("semibold"), theme::INK, this));
    outer->addWidget(textLabel(QStringLiteral("A real folder Takoyo can read, edit and build in."), 10,
                               QStringLiteral("regular"), theme::MUTED, this));

    auto *form = new QFormLayout();
    form->setSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(textLabel(QStringLiteral("Name"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 name_ = new QLineEdit(this));
    form->addRow(textLabel(QStringLiteral("Description"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 description_ = new QLineEdit(this));
    name_->setPlaceholderText(QStringLiteral("My next idea"));
    description_->setPlaceholderText(QStringLiteral("Optional — what is it about?"));
    outer->addLayout(form);
    outer->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new TextButton(QStringLiteral("Cancel"), TextButton::Variant::Secondary, 34, this);
    cancel->setFontSize(10);
    auto *create = new TextButton(QStringLiteral("Create project"), TextButton::Variant::Primary, 34, this);
    create->setFontSize(10);
    buttons->addWidget(cancel);
    buttons->addWidget(create);
    outer->addLayout(buttons);

    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(create, &QAbstractButton::clicked, this, &NewProjectDialog::create);
    connect(name_, &QLineEdit::returnPressed, this, &NewProjectDialog::create);
    name_->setFocus();
}

void NewProjectDialog::create()
{
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        name_->setFocus();
        return;
    }
    const ProjectSummary summary = store_->create(name, description_->text().trimmed());
    if (summary.id.isEmpty())
        return;
    createdId_ = summary.id;
    accept();
}

// ---------------------------------------------------------------------------
// ModelPickerDialog
// ---------------------------------------------------------------------------
namespace {

struct LocalModelEntry {
    const char *key;
    const char *name;
    const char *size;
    const char *filename;
    const char *description;
};

const LocalModelEntry kLocalModels[] = {
    {"qwen2.5-0.5b", "Qwen2.5 0.5B", "0.5 GB", "qwen2.5-0.5b-instruct-q4_k_m.gguf",
     "Tiny and fast. Best for quick questions on modest hardware."},
    {"qwen2.5-1.5b", "Qwen2.5 1.5B", "1.1 GB", "qwen2.5-1.5b-instruct-q4_k_m.gguf",
     "The light default: quick answers, easy on memory."},
    {"qwen2.5-coder-1.5b", "Qwen2.5 Coder 1.5B", "1.1 GB", "qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
     "Tuned for code, refactors and project edits."},
    {"qwen2.5-3b", "Qwen2.5 3B", "2.1 GB", "qwen2.5-3b-instruct-q4_k_m.gguf",
     "Better reasoning and writing. Needs about 4 GB of free memory."},
};

struct CloudEntry {
    const char *key;
    const char *label;
    const char *hint;
};

const CloudEntry kCloudModels[] = {
    {"openai", "OpenAI", "Cloud · needs an API key"},
    {"anthropic", "Anthropic", "Cloud · needs an API key"},
    {"google", "Google Gemini", "Cloud · needs an API key"},
    {"ollama", "Ollama", "Your own local server"},
};

bool modelInstalled(const QString &filename)
{
    return QFileInfo::exists(QDir(util::takoyoHome()).filePath(QStringLiteral("models/") + filename));
}

double presetTemperature(const QString &key)
{
    if (key == QLatin1String("precise"))
        return 0.4;
    if (key == QLatin1String("creative"))
        return 0.85;
    if (key == QLatin1String("balanced"))
        return 0.6;
    return -1;
}

int presetTokens(const QString &key)
{
    if (key == QLatin1String("precise"))
        return 1024;
    if (key == QLatin1String("creative"))
        return 3072;
    return 2048;
}

} // namespace

ModelPickerDialog::ModelPickerDialog(Config *config, QWidget *parent)
    : QDialog(parent)
    , config_(config)
{
    setWindowTitle(QStringLiteral("Choose a model"));
    resize(540, 660);
    styleDialog(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 22, 24, 18);
    outer->setSpacing(10);

    outer->addWidget(textLabel(QStringLiteral("Choose a model"), 20, QStringLiteral("semibold"), theme::INK, this));
    auto *subtitle = textLabel(QStringLiteral("This is Takoyo's brain. Small models are fast; bigger ones reason "
                                              "better if your PC can handle them."),
                               11, QStringLiteral("regular"), theme::MUTED_STRONG, this);
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);
    outer->addSpacing(4);

    const QString currentProvider = config_->str(QStringLiteral("provider"));
    const QString currentModel = config_->modelFor(QStringLiteral("local"));

    outer->addWidget(textLabel(QStringLiteral("ON THIS DEVICE"), 10, QStringLiteral("semibold"), theme::INK, this));
    localList_ = new QListWidget(this);
    localList_->setMinimumHeight(188);
    localList_->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const LocalModelEntry &entry : kLocalModels) {
        const QString key = QString::fromLatin1(entry.key);
        const bool installed = modelInstalled(QString::fromLatin1(entry.filename));
        const bool current = currentProvider == QLatin1String("local") && currentModel == key;
        QString state = current ? QStringLiteral("Current") : (installed ? QStringLiteral("Ready") : QStringLiteral("Not downloaded"));
        auto *item = new QListWidgetItem(QStringLiteral("%1   ·   %2   ·   %3")
                                             .arg(QString::fromLatin1(entry.name), QString::fromLatin1(entry.size), state),
                                         localList_);
        item->setData(Qt::UserRole, key);
        item->setData(Qt::UserRole + 1, QString::fromLatin1(entry.description));
        item->setData(Qt::UserRole + 2, installed);
    }
    outer->addWidget(localList_);
    localDetail_ = textLabel(QString(), 11, QStringLiteral("regular"), theme::MUTED_STRONG, this);
    localDetail_->setWordWrap(true);
    outer->addWidget(localDetail_);

    outer->addSpacing(4);
    outer->addWidget(textLabel(QStringLiteral("CLOUD & OTHER SERVERS"), 10, QStringLiteral("semibold"), theme::INK, this));
    cloudList_ = new QListWidget(this);
    cloudList_->setMinimumHeight(146);
    cloudList_->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const CloudEntry &entry : kCloudModels) {
        const QString key = QString::fromLatin1(entry.key);
        auto *item = new QListWidgetItem(QStringLiteral("%1   ·   %2")
                                             .arg(QString::fromUtf8(entry.label), QString::fromUtf8(entry.hint)),
                                         cloudList_);
        item->setData(Qt::UserRole, key);
        if (currentProvider == key)
            item->setText(item->text() + QStringLiteral("   ·   Current"));
    }
    outer->addWidget(cloudList_);
    cloudDetail_ = textLabel(QString(), 11, QStringLiteral("regular"), theme::MUTED_STRONG, this);
    cloudDetail_->setWordWrap(true);
    outer->addWidget(cloudDetail_);

    auto *privacy = textLabel(QStringLiteral("Built-in models run offline on this device — nothing leaves your machine."),
                              10, QStringLiteral("regular"), theme::MUTED_STRONG, this);
    privacy->setWordWrap(true);
    outer->addWidget(privacy);
    outer->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto *settings = new TextButton(QStringLiteral("Open Settings"), TextButton::Variant::Secondary, 34, this);
    settings->setFontSize(10);
    buttons->addWidget(settings);
    buttons->addStretch(1);
    auto *cancel = new TextButton(QStringLiteral("Cancel"), TextButton::Variant::Secondary, 34, this);
    cancel->setFontSize(10);
    useButton_ = new TextButton(QStringLiteral("Use this model"), TextButton::Variant::Primary, 34, this);
    useButton_->setFontSize(10);
    useButton_->setEnabled(false);
    buttons->addWidget(cancel);
    buttons->addWidget(useButton_);
    outer->addLayout(buttons);

    connect(localList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (syncing_ || row < 0)
            return;
        syncing_ = true;
        cloudList_->setCurrentRow(-1);
        syncing_ = false;
        updateSelection();
    });
    connect(cloudList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (syncing_ || row < 0)
            return;
        syncing_ = true;
        localList_->setCurrentRow(-1);
        syncing_ = false;
        updateSelection();
    });
    connect(localList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (localList_->currentItem())
            apply();
    });
    connect(cloudList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (cloudList_->currentItem())
            apply();
    });
    connect(useButton_, &QAbstractButton::clicked, this, &ModelPickerDialog::apply);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(settings, &QAbstractButton::clicked, this, [this]() {
        reject();
        emit settingsRequested();
    });

    syncing_ = true;
    if (currentProvider == QLatin1String("local")) {
        for (int i = 0; i < localList_->count(); ++i) {
            if (localList_->item(i)->data(Qt::UserRole).toString() == currentModel) {
                localList_->setCurrentRow(i);
                break;
            }
        }
    } else {
        for (int i = 0; i < cloudList_->count(); ++i) {
            if (cloudList_->item(i)->data(Qt::UserRole).toString() == currentProvider) {
                cloudList_->setCurrentRow(i);
                break;
            }
        }
    }
    syncing_ = false;
    updateSelection();
}

void ModelPickerDialog::updateSelection()
{
    QListWidgetItem *local = localList_->currentItem();
    QListWidgetItem *cloud = cloudList_->currentItem();
    if (local) {
        const bool installed = local->data(Qt::UserRole + 2).toBool();
        localDetail_->setText(local->data(Qt::UserRole + 1).toString()
                              + (installed ? QStringLiteral("  Ready to use.")
                                           : QStringLiteral("  Takoyo downloads it in the background when you pick it.")));
        cloudDetail_->clear();
        useButton_->setText(installed ? QStringLiteral("Use this model") : QStringLiteral("Download & use"));
        useButton_->setEnabled(true);
    } else if (cloud) {
        const QString key = cloud->data(Qt::UserRole).toString();
        localDetail_->clear();
        cloudDetail_->setText(key == QLatin1String("ollama")
                                  ? QStringLiteral("Talks to the Ollama server on your machine. Start Ollama first.")
                                  : QStringLiteral("Uses the %1 API key you save in Settings.")
                                        .arg(cloud->text().section(QStringLiteral("   ·"), 0, 0)));
        useButton_->setText(QStringLiteral("Use this model"));
        useButton_->setEnabled(true);
    } else {
        localDetail_->clear();
        cloudDetail_->clear();
        useButton_->setEnabled(false);
    }
}

void ModelPickerDialog::apply()
{
    if (QListWidgetItem *local = localList_->currentItem()) {
        provider_ = QStringLiteral("local");
        model_ = local->data(Qt::UserRole).toString();
        download_ = !local->data(Qt::UserRole + 2).toBool();
    } else if (QListWidgetItem *cloud = cloudList_->currentItem()) {
        provider_ = cloud->data(Qt::UserRole).toString();
        model_.clear();
        download_ = false;
    } else {
        return;
    }
    accept();
}

// ---------------------------------------------------------------------------
// SettingsDialog
// ---------------------------------------------------------------------------
SettingsDialog::SettingsDialog(Config *config, QWidget *parent)
    : QDialog(parent)
    , config_(config)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(620, 740);
    styleDialog(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 22, 24, 20);
    outer->setSpacing(12);

    outer->addWidget(textLabel(QStringLiteral("Settings"), 20, QStringLiteral("semibold"), theme::INK, this));
    outer->addWidget(textLabel(QStringLiteral("Choose Takoyo's brain, how it paints, and local API access."), 10,
                               QStringLiteral("regular"), theme::MUTED, this));

    auto *form = new QFormLayout();
    form->setSpacing(9);
    form->setLabelAlignment(Qt::AlignLeft);

    provider_ = new Combo(this);
    provider_->addItem(QStringLiteral("Built-in AI"), QStringLiteral("local"));
    provider_->addItem(QStringLiteral("OpenAI"), QStringLiteral("openai"));
    provider_->addItem(QStringLiteral("Anthropic"), QStringLiteral("anthropic"));
    provider_->addItem(QStringLiteral("Google Gemini"), QStringLiteral("google"));
    provider_->addItem(QStringLiteral("Ollama"), QStringLiteral("ollama"));
    form->addRow(textLabel(QStringLiteral("Provider"), 10, QStringLiteral("medium"), theme::INK_SOFT, this), provider_);

    model_ = new QLineEdit(this);
    form->addRow(textLabel(QStringLiteral("Model"), 10, QStringLiteral("medium"), theme::INK_SOFT, this), model_);

    keyLabel_ = textLabel(QStringLiteral("API key"), 10, QStringLiteral("medium"), theme::INK_SOFT, this);
    key_ = new QLineEdit(this);
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText(QStringLiteral("Paste your key"));
    keyRow_ = new QWidget(this);
    auto *keyLayout = new QHBoxLayout(keyRow_);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->setSpacing(8);
    keyLayout->addWidget(key_, 1);
    replaceKey_ = new TextButton(QStringLiteral("Replace key"), TextButton::Variant::Secondary, 32, keyRow_);
    replaceKey_->setFontSize(9);
    keyLayout->addWidget(replaceKey_);
    form->addRow(keyLabel_, keyRow_);

    imageProvider_ = new Combo(this);
    imageProvider_->addItem(QStringLiteral("Takoyo Art"), QStringLiteral("takoyo"));
    imageProvider_->addItem(QStringLiteral("OpenAI DALL·E 3"), QStringLiteral("openai"));
    form->addRow(textLabel(QStringLiteral("Image engine"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 imageProvider_);

    imageSize_ = new Combo(this);
    for (const QString &size : {QStringLiteral("1024x1280"), QStringLiteral("1024x1024"), QStringLiteral("1024x1536"),
                                QStringLiteral("1536x1024"), QStringLiteral("1280x720"), QStringLiteral("1024x1792"),
                                QStringLiteral("1792x1024")})
        imageSize_->addItem(size);
    form->addRow(textLabel(QStringLiteral("Image size"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 imageSize_);

    apiPort_ = new QSpinBox(this);
    apiPort_->setRange(1024, 65535);
    apiPort_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    apiPort_->setValue(config_->integer(QStringLiteral("api_port")));
    form->addRow(textLabel(QStringLiteral("Local API port"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 apiPort_);

    outer->addLayout(form);

    autoApply_ = new QCheckBox(QStringLiteral("Apply project edits automatically"), this);
    autoApply_->setChecked(config_->flag(QStringLiteral("auto_apply_edits")));
    outer->addWidget(autoApply_);

    allowCodeExecution_ = new QCheckBox(QStringLiteral("Allow code execution via the local API"), this);
    allowCodeExecution_->setChecked(config_->flag(QStringLiteral("allow_code_execution")));
    allowCodeExecution_->setToolTip(QStringLiteral(
        "When enabled, the local API's /run endpoint can execute Python or pip commands in the workspace."));
    outer->addWidget(allowCodeExecution_);

    outer->addSpacing(6);
    outer->addWidget(textLabel(QStringLiteral("ADVANCED"), 10, QStringLiteral("semibold"), theme::INK, this));
    auto *advanced = new QFormLayout();
    advanced->setSpacing(9);
    advanced->setLabelAlignment(Qt::AlignLeft);

    preset_ = new Combo(this);
    preset_->addItem(QStringLiteral("Precise"), QStringLiteral("precise"));
    preset_->addItem(QStringLiteral("Balanced"), QStringLiteral("balanced"));
    preset_->addItem(QStringLiteral("Creative"), QStringLiteral("creative"));
    preset_->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    advanced->addRow(textLabel(QStringLiteral("Preset"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                     preset_);

    temperature_ = new QDoubleSpinBox(this);
    temperature_->setRange(0.0, 1.5);
    temperature_->setSingleStep(0.05);
    temperature_->setDecimals(2);
    temperature_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    temperature_->setToolTip(QStringLiteral("Lower is steadier and more literal; higher is more varied."));
    advanced->addRow(textLabel(QStringLiteral("Temperature"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                     temperature_);

    maxTokens_ = new QSpinBox(this);
    maxTokens_->setRange(256, 8192);
    maxTokens_->setSingleStep(128);
    maxTokens_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    maxTokens_->setToolTip(QStringLiteral("Upper bound on the reply length."));
    advanced->addRow(textLabel(QStringLiteral("Max reply length"), 10, QStringLiteral("medium"), theme::INK_SOFT,
                               this),
                     maxTokens_);
    outer->addLayout(advanced);

    auto *advancedNote = textLabel(
        QStringLiteral("Precise is calm and factual, Balanced is the everyday default, Creative ranges further. "
                       "Lower the max length if replies ramble."),
        9, QStringLiteral("regular"), theme::FAINT, this);
    advancedNote->setWordWrap(true);
    outer->addWidget(advancedNote);

    auto *note = textLabel(QStringLiteral(
                               "The local API has no login; keep it on this computer. Code execution is disabled by default. "
                               "A new API port takes effect the next time Takoyo starts."),
                           9, QStringLiteral("regular"), theme::FAINT, this);
    note->setWordWrap(true);
    outer->addWidget(note);
    outer->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    auto *folder = new TextButton(QStringLiteral("Open Takoyo folder"), TextButton::Variant::Secondary, 34, this);
    folder->setFontSize(9);
    buttons->addWidget(folder);
    buttons->addStretch(1);
    auto *cancel = new TextButton(QStringLiteral("Cancel"), TextButton::Variant::Secondary, 34, this);
    cancel->setFontSize(10);
    auto *save = new TextButton(QStringLiteral("Save changes"), TextButton::Variant::Primary, 34, this);
    save->setFontSize(10);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    outer->addLayout(buttons);

    connect(folder, &QAbstractButton::clicked, this, []() { util::openPath(util::takoyoHome()); });
    connect(replaceKey_, &QAbstractButton::clicked, this, [this]() {
        key_->clear();
        key_->setReadOnly(false);
        key_->setFocus();
    });
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(save, &QAbstractButton::clicked, this, &SettingsDialog::save);
    connect(provider_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { loadProvider(); });
    connect(preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (syncingPreset_)
            return;
        const QString key = preset_->currentData().toString();
        if (presetTemperature(key) < 0)
            return;
        syncingPreset_ = true;
        temperature_->setValue(presetTemperature(key));
        maxTokens_->setValue(presetTokens(key));
        syncingPreset_ = false;
    });
    const auto markCustom = [this]() {
        if (syncingPreset_)
            return;
        syncingPreset_ = true;
        preset_->setCurrentIndex(preset_->findData(QStringLiteral("custom")));
        syncingPreset_ = false;
    };
    connect(temperature_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [markCustom](double) { markCustom(); });
    connect(maxTokens_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [markCustom](int) { markCustom(); });

    const QString provider = config_->str(QStringLiteral("provider"));
    const int index = provider_->findData(provider);
    provider_->setCurrentIndex(index >= 0 ? index : 0);

    const int imageIndex = imageProvider_->findData(config_->str(QStringLiteral("image_provider")));
    imageProvider_->setCurrentIndex(imageIndex >= 0 ? imageIndex : 0);
    const QString size = config_->str(QStringLiteral("image_size"));
    const int sizeIndex = imageSize_->findText(size);
    if (sizeIndex >= 0)
        imageSize_->setCurrentIndex(sizeIndex);

    const double storedTemp = config_->number(QStringLiteral("temperature"));
    QString savedPreset = config_->str(QStringLiteral("generation_preset"));
    if (savedPreset != QLatin1String("custom") && presetTemperature(savedPreset) >= 0
        && qAbs(storedTemp - presetTemperature(savedPreset)) > 0.001)
        savedPreset = QStringLiteral("custom");
    const int presetIndex = preset_->findData(savedPreset);
    syncingPreset_ = true;
    preset_->setCurrentIndex(presetIndex >= 0 ? presetIndex : 1);
    temperature_->setValue(storedTemp);
    maxTokens_->setValue(config_->integer(QStringLiteral("max_tokens")));
    syncingPreset_ = false;

    loadProvider();
}

void SettingsDialog::loadProvider()
{
    const QString provider = provider_->currentData().toString();
    model_->setText(config_->modelFor(provider));
    model_->setPlaceholderText(provider == QLatin1String("local") ? QStringLiteral("qwen2.5-1.5b")
                                                                  : QStringLiteral("Model name"));
    const bool needsKey = provider != QLatin1String("local") && provider != QLatin1String("ollama");
    const bool stored = needsKey && !config_->apiKey(provider).isEmpty();
    keyLabel_->setVisible(needsKey);
    keyRow_->setVisible(needsKey);
    replaceKey_->setVisible(stored);
    // Never load the saved key into the field: it stays hidden on screen and in
    // screenshots, and Replace key is needed to type a new one.
    key_->clear();
    key_->setReadOnly(stored);
    key_->setPlaceholderText(stored ? QStringLiteral("Saved key — press Replace key")
                                    : QStringLiteral("Paste your key"));
}

void SettingsDialog::save()
{
    const QString provider = provider_->currentData().toString();
    const QString model = model_->text().trimmed();
    config_->set(QStringLiteral("provider"), provider, false);
    if (!model.isEmpty())
        config_->set(provider + QStringLiteral("_model"), model, false);
    // An empty field means "keep the saved key"; only a typed value replaces it.
    if (provider != QLatin1String("local") && provider != QLatin1String("ollama")) {
        const QString typed = key_->text().trimmed();
        if (!typed.isEmpty())
            config_->set(provider + QStringLiteral("_api_key"), typed, false);
    }
    config_->set(QStringLiteral("image_provider"), imageProvider_->currentData().toString(), false);
    config_->set(QStringLiteral("image_size"), imageSize_->currentText(), false);
    config_->set(QStringLiteral("auto_apply_edits"), autoApply_->isChecked(), false);
    config_->set(QStringLiteral("generation_preset"), preset_->currentData().toString(), false);
    config_->set(QStringLiteral("temperature"), temperature_->value(), false);
    config_->set(QStringLiteral("max_tokens"), maxTokens_->value(), false);
    config_->set(QStringLiteral("api_port"), apiPort_->value(), false);
    config_->set(QStringLiteral("allow_code_execution"), allowCodeExecution_->isChecked(), false);
    config_->save();
    accept();
}

// ---------------------------------------------------------------------------
// PaletteSwatch + PalettePickerDialog
// ---------------------------------------------------------------------------
namespace {

// A small widget that paints a miniature preview of a palette: a background
// fill, a couple of "text" bars in the ink color, and accent dots in the
// danger/success/code colors. Used inside the palette picker grid.
class PaletteSwatch : public QWidget {
public:
    PaletteSwatch(const theme::Palette &palette, bool selected, QWidget *parent)
        : QWidget(parent)
        , palette_(palette)
        , selected_(selected)
    {
        setFixedSize(132, 74);
        setCursor(Qt::PointingHandCursor);
        setToolTip(palette.name);
    }

    void setSelected(bool selected)
    {
        if (selected_ == selected)
            return;
        selected_ = selected;
        update();
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(palette_.white);
        p.drawRoundedRect(r, 10, 10);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(selected_ ? palette_.ink : palette_.lineSoft, selected_ ? 2.2 : 1.2));
        p.drawRoundedRect(r.adjusted(0.6, 0.6, -0.6, -0.6), 10, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(palette_.ink);
        p.drawRoundedRect(QRectF(r.left() + 12, r.top() + 16, r.width() * 0.55, 4.5), 2.25, 2.25);
        p.setBrush(palette_.muted);
        p.drawRoundedRect(QRectF(r.left() + 12, r.top() + 26, r.width() * 0.38, 3.5), 1.75, 1.75);
        const double dotY = r.bottom() - 18;
        p.setBrush(palette_.danger);
        p.drawEllipse(QRectF(r.left() + 12, dotY - 5, 10, 10));
        p.setBrush(palette_.success);
        p.drawEllipse(QRectF(r.left() + 28, dotY - 5, 10, 10));
        p.setBrush(palette_.codeBlue);
        p.drawEllipse(QRectF(r.left() + 44, dotY - 5, 10, 10));
        p.setBrush(palette_.codeRed);
        p.drawEllipse(QRectF(r.left() + 60, dotY - 5, 10, 10));
        p.setBrush(palette_.chip);
        p.drawEllipse(QRectF(r.right() - 22, dotY - 5, 10, 10));
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && onClick)
            onClick();
        QWidget::mouseReleaseEvent(event);
    }

private:
    theme::Palette palette_;
    bool selected_;
};

} // namespace

PalettePickerDialog::PalettePickerDialog(Config *config, QWidget *parent)
    : QDialog(parent)
    , config_(config)
{
    setWindowTitle(QStringLiteral("Choose a color palette"));
    resize(620, 560);
    styleDialog(this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 22, 24, 18);
    outer->setSpacing(10);

    outer->addWidget(textLabel(QStringLiteral("Color palette"), 20, QStringLiteral("semibold"), theme::INK, this));
    auto *subtitle = textLabel(QStringLiteral("Pick the look for Takoyo. The light/dark toggle in the header "
                                              "still works with any palette."),
                               11, QStringLiteral("regular"), theme::MUTED_STRONG, this);
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);
    outer->addSpacing(6);

    palettes_ = theme::availablePalettes();
    selected_ = config->str(QStringLiteral("palette"));
    bool found = false;
    for (const theme::PaletteInfo &info : palettes_) {
        if (info.id == selected_) { found = true; break; }
    }
    if (!found)
        selected_ = palettes_.isEmpty() ? QString() : palettes_.first().id;

    grid_ = new QGridLayout();
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(10);
    outer->addLayout(grid_, 1);
    buildGrid();

    outer->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = new TextButton(QStringLiteral("Cancel"), TextButton::Variant::Secondary, 34, this);
    cancel->setFontSize(10);
    useButton_ = new TextButton(QStringLiteral("Use this palette"), TextButton::Variant::Primary, 34, this);
    useButton_->setFontSize(10);
    useButton_->setEnabled(!selected_.isEmpty());
    buttons->addWidget(cancel);
    buttons->addWidget(useButton_);
    outer->addLayout(buttons);

    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(useButton_, &QAbstractButton::clicked, this, &PalettePickerDialog::applySelection);
}

void PalettePickerDialog::buildGrid()
{
    const int columns = 3;
    for (int i = 0; i < palettes_.size(); ++i) {
        const theme::PaletteInfo &info = palettes_[i];
        theme::Palette pal;
        if (!theme::readPalette(info.id, pal))
            continue;
        auto *swatch = new PaletteSwatch(pal, info.id == selected_, this);
        swatches_.append(swatch);
        swatch->onClick = [this, info]() { onSelectionChanged(info.id); };
        grid_->addWidget(swatch, i / columns, i % columns);
    }
}

void PalettePickerDialog::onSelectionChanged(const QString &id)
{
    selected_ = id;
    for (int i = 0; i < palettes_.size() && i < swatches_.size(); ++i) {
        auto *swatch = static_cast<PaletteSwatch *>(swatches_[i]);
        swatch->setSelected(palettes_[i].id == id);
    }
    if (useButton_)
        useButton_->setEnabled(!selected_.isEmpty());
    emit palettePreviewRequested(id);
}

void PalettePickerDialog::applySelection()
{
    if (selected_.isEmpty())
        return;
    theme::loadPalette(selected_);
    config_->set(QStringLiteral("palette"), selected_, false);
    config_->save();
    accept();
}
