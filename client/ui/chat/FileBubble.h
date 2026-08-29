#pragma once

#include "BubbleFrame.h"

#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

class FileBubble : public BubbleFrame {
    Q_OBJECT
public:
    FileBubble(const QJsonObject& metadata, ChatRole role, bool incoming, QWidget* parent=nullptr);
    QString transferId() const;
    void setTransferId(const QString& id);
    void setProgress(qint64 current, qint64 total);
    void setFinished(const QString& localPath);
    void setFailed(const QString& reason);
signals:
    void downloadRequested(const QJsonObject& metadata);
    void cancelRequested(const QString& id);
private:
    QJsonObject metadata_;
    QLabel* status_;
    QProgressBar* progress_;
    QPushButton* action_;
    bool incoming_;
};
