#include "dialogs.h"

#include "config.h"
#include "icons.h"
#include "store.h"
#include "theme.h"
#include "util.h"
#include "widgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace {

QString dialogStyle()
{
    return QStringLiteral(R"(
QDialog { background: %1; }
QLabel { color: %2; background: transparent; }
QLineEdit, QPlainTextEdit, QComboBox, QSpinBox {
    background: %1;
    border: 1.3px solid %2;
    border-radius: 10px;
    padding: 7px 10px;
    color: %2;
    selection-background-color: %3;
}
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1.6px solid %2; }
QComboBox { padding-right: 30px; }
QComboBox::drop-down { border: none; width: 28px; }
QComboBox QAbstractItemView {
    background: %1;
    border: 1px solid %4;
    selection-background-color: %3;
    selection-color: %2;
    outline: none;
}
QListWidget {
    background: %1;
    border: 1.2px solid %4;
    border-radius: 12px;
    outline: none;
    padding: 4px;
}
QListWidget::item { padding: 6px 8px; border-radius: 8px; color: %5; }
QListWidget::item:hover { background: %6; }
QListWidget::item:selected { background: %3; color: %2; }
QCheckBox { color: %2; spacing: 8px; background: transparent; }
QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; }
QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)")
        .arg(theme::WHITE.name())
        .arg(theme::INK.name())
        .arg(theme::ACTIVE.name())
        .arg(theme::LINE_SOFT.name())
        .arg(theme::INK_SOFT.name())
        .arg(theme::HOVER.name());
}

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
        icons::paint(painter, QStringLiteral("chevron-down"), arrow, theme::MUTED);
    }
};

QLabel *textLabel(const QString &text, int size, const QString &weight, const QColor &color,
                  QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setFont(theme::font(size, weight));
    label->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(color.name()));
    return label;
}

void styleDialog(QDialog *dialog)
{
    dialog->setStyleSheet(dialogStyle());
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
// SettingsDialog
// ---------------------------------------------------------------------------
SettingsDialog::SettingsDialog(Config *config, QWidget *parent)
    : QDialog(parent)
    , config_(config)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(560, 560);
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

    palette_ = new Combo(this);
    for (const theme::PaletteInfo &info : theme::availablePalettes())
        palette_->addItem(info.name, info.id);
    form->addRow(textLabel(QStringLiteral("Colour theme"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 palette_);

    apiPort_ = new QSpinBox(this);
    apiPort_->setRange(1024, 65535);
    apiPort_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    apiPort_->setValue(config_->integer(QStringLiteral("api_port")));
    form->addRow(textLabel(QStringLiteral("Local API port"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 apiPort_);
    apiHost_ = new QLineEdit(config_->str(QStringLiteral("api_host")), this);
    apiHost_->setPlaceholderText(QStringLiteral("127.0.0.1"));
    form->addRow(textLabel(QStringLiteral("Local API host"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 apiHost_);
    apiToken_ = new QLineEdit(this);
    apiToken_->setEchoMode(QLineEdit::Password);
    apiToken_->setPlaceholderText(QStringLiteral("Saved token — type a replacement"));
    auto *tokenRow = new QWidget(this);
    auto *tokenLayout = new QHBoxLayout(tokenRow);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenLayout->setSpacing(8);
    tokenLayout->addWidget(apiToken_, 1);
    auto *copyToken = new TextButton(QStringLiteral("Copy"), TextButton::Variant::Secondary, 34, tokenRow);
    copyToken->setFontSize(9);
    tokenLayout->addWidget(copyToken);
    connect(copyToken, &QAbstractButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(config_->str(QStringLiteral("api_token")));
        QMessageBox::information(this, QStringLiteral("API token copied"),
                                 QStringLiteral("The bearer token is on the clipboard. Keep it private."));
    });
    form->addRow(textLabel(QStringLiteral("Local API bearer token"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 tokenRow);
    QStringList origins;
    for (const QJsonValue &origin : config_->value(QStringLiteral("api_allowed_origins")).toArray())
        if (origin.isString())
            origins << origin.toString();
    apiOrigins_ = new QLineEdit(origins.join(QStringLiteral(", ")), this);
    apiOrigins_->setPlaceholderText(QStringLiteral("Leave blank to deny browser origins"));
    form->addRow(textLabel(QStringLiteral("Allowed browser origins"), 10, QStringLiteral("medium"), theme::INK_SOFT, this),
                 apiOrigins_);

    outer->addLayout(form);

    autoApply_ = new QCheckBox(QStringLiteral("Apply project edits automatically"), this);
    autoApply_->setChecked(config_->flag(QStringLiteral("auto_apply_edits")));
    outer->addWidget(autoApply_);

    auto *note = textLabel(QStringLiteral("Changes apply to your next message. A new Local API host or port takes effect the next time Takoyo starts. Network hosts require a bearer token; browser access is denied unless its exact origin is listed."),
                           9, QStringLiteral("regular"), theme::FAINT, this);
    note->setWordWrap(true);
    outer->addWidget(note);
    auto *historyActions = new QHBoxLayout();
    auto *exportHistory = new TextButton(QStringLiteral("Export / back up chats"), TextButton::Variant::Secondary, 32, this);
    auto *restoreHistory = new TextButton(QStringLiteral("Restore chats"), TextButton::Variant::Secondary, 32, this);
    exportHistory->setFontSize(9);
    restoreHistory->setFontSize(9);
    historyActions->addWidget(exportHistory);
    historyActions->addWidget(restoreHistory);
    historyActions->addStretch(1);
    outer->addLayout(historyActions);
    connect(exportHistory, &QAbstractButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export conversation backup"),
            QDir::home().filePath(QStringLiteral("takoyo-conversations.json")),
            QStringLiteral("Takoyo conversation backup (*.json)"));
        if (path.isEmpty())
            return;
        ConversationStore store(util::conversationsDir());
        QString error;
        if (!store.exportAll(path, &error))
            QMessageBox::warning(this, QStringLiteral("Export failed"), error);
        else
            QMessageBox::information(this, QStringLiteral("Chats exported"),
                                     QStringLiteral("Your conversations were exported to the selected file."));
    });
    connect(restoreHistory, &QAbstractButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Restore conversation backup"), QDir::homePath(),
            QStringLiteral("Takoyo conversation backup (*.json)"));
        if (path.isEmpty())
            return;
        ConversationStore store(util::conversationsDir());
        QString error;
        const int count = store.restoreFrom(path, &error);
        if (count <= 0)
            QMessageBox::warning(this, QStringLiteral("Restore failed"), error);
        else
            QMessageBox::information(this, QStringLiteral("Chats restored"),
                                     QStringLiteral("Restored %1 conversation(s). Existing chats were kept.")
                                         .arg(count));
    });
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

    const QString provider = config_->str(QStringLiteral("provider"));
    const int index = provider_->findData(provider);
    provider_->setCurrentIndex(index >= 0 ? index : 0);

    const int imageIndex = imageProvider_->findData(config_->str(QStringLiteral("image_provider")));
    imageProvider_->setCurrentIndex(imageIndex >= 0 ? imageIndex : 0);
    const QString size = config_->str(QStringLiteral("image_size"));
    const int sizeIndex = imageSize_->findText(size);
    if (sizeIndex >= 0)
        imageSize_->setCurrentIndex(sizeIndex);

    const QString palette = config_->str(QStringLiteral("palette"));
    const int paletteIndex = palette_->findData(palette);
    palette_->setCurrentIndex(paletteIndex >= 0 ? paletteIndex : 0);
    connect(palette_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit palettePreviewRequested(palette_->currentData().toString()); });

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
    config_->set(QStringLiteral("palette"), palette_->currentData().toString(), false);
    config_->set(QStringLiteral("auto_apply_edits"), autoApply_->isChecked(), false);
    config_->set(QStringLiteral("api_port"), apiPort_->value(), false);
    const QString apiHost = apiHost_->text().trimmed();
    config_->set(QStringLiteral("api_host"), apiHost.isEmpty() ? QStringLiteral("127.0.0.1") : apiHost, false);
    const QString typedToken = apiToken_->text().trimmed();
    if (!typedToken.isEmpty())
        config_->set(QStringLiteral("api_token"), typedToken, false);
    QJsonArray allowedOrigins;
    for (const QString &origin : apiOrigins_->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString value = origin.trimmed();
        if (!value.isEmpty() && !allowedOrigins.contains(value))
            allowedOrigins.append(value);
    }
    config_->set(QStringLiteral("api_allowed_origins"), allowedOrigins, false);
    config_->save();
    accept();
}

// ---------------------------------------------------------------------------
// PalettePickerDialog
// ---------------------------------------------------------------------------
namespace {
struct PaletteColors {
    QColor ground, ink, accent1, accent2, accent3;
};

PaletteColors readPaletteColors(const QString &id)
{
    PaletteColors c{QColor(QStringLiteral("#0B0B0D")), QColor(QStringLiteral("#FFFFFF")),
                    QColor(QStringLiteral("#79B8FF")), QColor(QStringLiteral("#2FBF71")),
                    QColor(QStringLiteral("#FF6B60"))};
    if (id == QLatin1String("dark"))
        return c;
    QFile file(QDir(util::assetsDir()).filePath(QStringLiteral("palettes/") + id + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly))
        return c;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return c;
    const QJsonObject o = doc.object();
    const auto get = [&o](const char *key, QColor fallback) {
        const QColor col(o.value(QLatin1String(key)).toString());
        return col.isValid() ? col : fallback;
    };
    c.ground = get("WHITE", c.ground);
    c.ink = get("INK", c.ink);
    c.accent1 = get("CODE_BLUE", c.accent1);
    c.accent2 = get("SUCCESS", c.accent2);
    c.accent3 = get("DANGER", c.accent3);
    return c;
}
} // namespace

class PaletteSwatch : public QWidget {
public:
    explicit PaletteSwatch(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(46, 30); }
    void setColors(const PaletteColors &c) { colors_ = c; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(colors_.ground);
        p.drawRoundedRect(rect(), 6, 6);
        p.setPen(QPen(colors_.ink, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        const int d = 8;
        const QColor dots[3] = {colors_.accent1, colors_.accent2, colors_.accent3};
        for (int i = 0; i < 3; ++i) {
            p.setPen(Qt::NoPen);
            p.setBrush(dots[i]);
            p.drawEllipse(6 + i * 12, 11, d, d);
        }
    }
private:
    PaletteColors colors_{};
};

PalettePickerDialog::PalettePickerDialog(Config *config, QWidget *parent)
    : QDialog(parent)
    , config_(config)
{
    setWindowTitle(QStringLiteral("Colour theme"));
    resize(360, 520);
    styleDialog(this);

    previousId_ = config_->str(QStringLiteral("palette"));
    selectedId_ = previousId_;

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(20, 18, 20, 16);
    outer->setSpacing(10);
    outer->addWidget(textLabel(QStringLiteral("Colour theme"), 18, QStringLiteral("semibold"), theme::INK, this));
    outer->addWidget(textLabel(QStringLiteral("Pick a palette. It applies instantly; Cancel reverts."),
                              10, QStringLiteral("regular"), theme::MUTED, this));

    list_ = new QListWidget(this);
    list_->setSpacing(4);
    for (const theme::PaletteInfo &info : theme::availablePalettes()) {
        auto *item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, info.id);
        auto *row = new QWidget(list_);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(10);
        auto *swatch = new PaletteSwatch(row);
        swatch->setColors(readPaletteColors(info.id));
        rowLayout->addWidget(swatch);
        auto *name = new QLabel(info.name, row);
        name->setFont(theme::font(12, QStringLiteral("medium")));
        name->setStyleSheet(QStringLiteral("color:%1;background:transparent;").arg(theme::INK.name()));
        rowLayout->addWidget(name);
        rowLayout->addStretch(1);
        item->setSizeHint(QSize(0, 42));
        list_->setItemWidget(item, row);
    }
    outer->addWidget(list_, 1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new TextButton(QStringLiteral("Cancel"), TextButton::Variant::Secondary, 32, this);
    auto *apply = new TextButton(QStringLiteral("Apply"), TextButton::Variant::Primary, 32, this);
    buttons->addWidget(cancel);
    buttons->addWidget(apply);
    outer->addLayout(buttons);

    // Select the current palette.
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString() == previousId_) {
            list_->setCurrentRow(i);
            break;
        }
    }

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(apply, &QAbstractButton::clicked, this, [this]() { applySelection(); });

    // Preview the starting palette once (no-op if already applied).
    onSelectionChanged();
}

void PalettePickerDialog::onSelectionChanged()
{
    if (!list_->currentItem())
        return;
    selectedId_ = list_->currentItem()->data(Qt::UserRole).toString();
    emit palettePreviewRequested(selectedId_);
}

void PalettePickerDialog::reject()
{
    emit palettePreviewRequested(previousId_);
    QDialog::reject();
}

void PalettePickerDialog::applySelection()
{
    config_->set(QStringLiteral("palette"), selectedId_, false);
    config_->save();
    accept();
}
