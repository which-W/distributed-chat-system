#include "FileBubble.h"
#include "ThemeManager.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QImageReader>
#include <QDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QPainterPath>

// Decode only validated local files and bound image dimensions and allocation.
static QImage readPreview(const QString& path, const QSize& bounds) {
    if (!QFileInfo(path).isFile()) return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) return {};
    reader.setScaledSize(size.scaled(bounds, Qt::KeepAspectRatio));
    return reader.read();
}
FileBubble::FileBubble(const QJsonObject& metadata, ChatRole role, bool incoming, QWidget* parent)
    : BubbleFrame(role, parent), metadata_(metadata), incoming_(incoming), self_(role == ChatRole::Self) {
    setMinimumWidth(230);
    setMaximumWidth(360);
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 3, 0, 3);
    layout->setSpacing(9);
    preview_ = new QPushButton(body);
    preview_->setObjectName("imageThumbnail");
    preview_->setToolTip(tr("点击查看大图"));
    preview_->setCursor(Qt::PointingHandCursor);
    preview_->hide();
    layout->addWidget(preview_);
    card_ = new QWidget(body);
    card_->setObjectName("attachmentCard");
    auto* row = new QHBoxLayout(card_);
    row->setContentsMargins(14, 14, 14, 14);
    row->setSpacing(12);
    QString suffix = QFileInfo(metadata["name"].toString()).suffix().toUpper().left(4);
    auto* icon = new QLabel(suffix.isEmpty() ? tr("文件") : suffix, card_);
    icon->setObjectName("fileType");
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(44, 52);
    row->addWidget(icon);
    auto* details = new QVBoxLayout;
    auto* title = new QLabel(metadata["name"].toString(), card_);
    title->setObjectName("fileName");
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    title->setMinimumWidth(0);
    const auto bytes = metadata["total_size"].toVariant().toLongLong();
    QString size = bytes < 1024 ? QString::number(bytes) + " B" :
        bytes < 1024 * 1024 ? QString::number(bytes / 1024.0, 'f', 1) + " KB" :
        QString::number(bytes / 1048576.0, 'f', 1) + " MB";
    auto* sizeLabel = new QLabel(size, card_);
    sizeLabel->setObjectName("fileSize");
    details->addWidget(title);
    details->addWidget(sizeLabel);
    row->addLayout(details, 1);
    action_ = new QPushButton(incoming ? QString::fromUtf8("↓") : QString::fromUtf8("×"), card_);
    action_->setObjectName("fileAction");
    action_->setFixedSize(36, 36);
    action_->setToolTip(incoming ? tr("下载文件，图片下载后可预览") : tr("取消发送"));
    action_->setAccessibleName(action_->toolTip());
    row->addWidget(action_);
    layout->addWidget(card_);
    progress_ = new QProgressBar(body);
    progress_->setObjectName("fileProgress");
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(4);
    progress_->setVisible(!incoming);
    layout->addWidget(progress_);
    status_ = new QLabel(incoming ? tr("点击下载 · 图片支持预览") : tr("准备发送…"), body);
    status_->setObjectName("fileStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setWidget(body);
    connect(action_, &QPushButton::clicked, this, [this]() {
        if (finished_ && !localPath_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(localPath_).absolutePath()));
        else if (incoming_) emit downloadRequested(metadata_);
        else emit cancelRequested(transferId());
    });
    connect(preview_, &QPushButton::clicked, this, &FileBubble::showPreview);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() { applyTheme(); });
    applyTheme();
}
void FileBubble::applyTheme() {
    const bool dark = ThemeManager::instance().themeMode() == ElaThemeType::Dark;
    setStyleSheet(QString(
        "QWidget#attachmentCard { background:%1; border-radius:16px; }"
        "QLabel#fileName { color:%2; font-size:14px; font-weight:600; background:transparent; }"
        "QLabel#fileSize { color:%3; font-size:12px; background:transparent; }"
        "QLabel#fileType { color:#ef476f; background:%4; border-radius:9px; font-size:11px; font-weight:700; }"
        "QPushButton#fileAction { color:%2; background:%4; border:0; border-radius:18px; font-size:22px; padding:0; }"
        "QPushButton#fileAction:hover { background:#b8d1ff; color:#174682; }"
        "QLabel#fileStatus { color:%5; font-size:11px; background:transparent; }"
        "QProgressBar#fileProgress { background:%4; border:0; border-radius:2px; }"
        "QProgressBar#fileProgress::chunk { background:%5; border-radius:2px; }"
        "QPushButton#imageThumbnail { border:0; padding:0; background:transparent; }")
        .arg(dark ? "#252a34" : "#f8faff", dark ? "#f1f4fa" : "#19263b",
             dark ? "#aeb9cb" : "#7b879b", dark ? "#384152" : "#e8eef8",
             self_ ? "#e0ebff" : dark ? "#b4bed0" : "#788599"));
}
QString FileBubble::transferId() const { return metadata_["id"].toString(); }
void FileBubble::setTransferId(const QString& id) { metadata_["id"] = id; }
void FileBubble::setProgress(qint64 current, qint64 total) {
    progress_->show();
    progress_->setValue(total > 0 ? int(qBound(qint64(0), current * 100 / total, qint64(100))) : 0);
    status_->setText(tr("正在%1 · %2%").arg(incoming_ ? tr("下载") : tr("发送")).arg(progress_->value()));
}
void FileBubble::setLocalPreview(const QString& path) {
    localPath_ = path;
    const auto image = readPreview(path, QSize(560, 360));
    imageReady_ = !image.isNull();
    preview_->setVisible(imageReady_);
    if (!imageReady_) return;
    QPixmap thumbnail(image.size());
    thumbnail.fill(Qt::transparent);
    QPainter painter(&thumbnail);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(thumbnail.rect()), 20, 20);
    painter.setClipPath(clip);
    painter.drawImage(0, 0, image);
    painter.end();
    preview_->setIcon(QIcon(thumbnail));
    preview_->setIconSize(image.size().scaled(QSize(280, 180), Qt::KeepAspectRatio));
    preview_->setMinimumHeight(preview_->iconSize().height());
    updateGeometry();
}
void FileBubble::setFinished(const QString& localPath) {
    finished_ = true;
    if (!localPath.isEmpty()) setLocalPreview(localPath);
    progress_->hide();
    status_->setText(imageReady_ ? tr("已完成 · 点击图片查看大图") : incoming_ ? tr("已下载") : tr("已发送"));
    status_->setToolTip(localPath_);
    action_->setText(QString::fromUtf8("↗"));
    action_->setToolTip(tr("打开所在文件夹"));
    action_->setAccessibleName(action_->toolTip());
    action_->setVisible(!localPath_.isEmpty());
}
void FileBubble::setFailed(const QString& reason) {
    progress_->hide();
    status_->setText(reason);
    action_->setEnabled(incoming_);
    action_->setToolTip(incoming_ ? tr("重新下载") : tr("发送失败"));
}
void FileBubble::showPreview() {
    const auto image = readPreview(localPath_, QSize(1600, 1000));
    if (image.isNull()) {
        status_->setText(tr("图片已移动或无法读取，请重新下载。"));
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("imagePreviewDialog");
    dialog->setWindowTitle(metadata_["name"].toString());
    dialog->resize(800, 600);
    auto* layout = new QVBoxLayout(dialog);
    auto* picture = new QLabel(dialog);
    picture->setAlignment(Qt::AlignCenter);
    picture->setPixmap(QPixmap::fromImage(image).scaled(760, 520, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(picture, 1);
    auto* close = new QPushButton(tr("关闭预览"), dialog);
    layout->addWidget(close);
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    dialog->show();
}
