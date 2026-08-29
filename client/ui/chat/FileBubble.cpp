#include "FileBubble.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>

FileBubble::FileBubble(const QJsonObject& metadata, ChatRole role, bool incoming, QWidget* parent)
    : BubbleFrame(role,parent),metadata_(metadata),incoming_(incoming)
{
    // 图片与普通文件共用同一种附件气泡，下载前不解析不可信文件内容。
    auto* body=new QWidget(this);auto* layout=new QVBoxLayout(body);
    auto* title=new QLabel(metadata["name"].toString(),body);
    const auto bytes=metadata["total_size"].toVariant().toLongLong();
    auto* size=new QLabel(QString::number(bytes/1024.0/1024.0,'f',2)+" MB",body);
    progress_=new QProgressBar(body);progress_->setRange(0,100);progress_->setValue(0);
    status_=new QLabel(incoming?tr("等待下载"):tr("准备上传"),body);
    action_=new QPushButton(incoming?tr("另存为"):tr("取消"),body);
    layout->addWidget(title);layout->addWidget(size);layout->addWidget(progress_);
    auto* row=new QHBoxLayout();row->addWidget(status_);row->addWidget(action_);layout->addLayout(row);setWidget(body);
    connect(action_,&QPushButton::clicked,this,[this](){
        if(incoming_) emit downloadRequested(metadata_); else emit cancelRequested(transferId());
    });
}

QString FileBubble::transferId() const{return metadata_["id"].toString();}
void FileBubble::setTransferId(const QString& id){metadata_["id"]=id;}
void FileBubble::setProgress(qint64 current,qint64 total){progress_->setValue(total>0?static_cast<int>(current*100/total):100);status_->setText(tr("传输中 %1%").arg(progress_->value()));}
void FileBubble::setFinished(const QString& localPath){progress_->setValue(100);status_->setText(localPath.isEmpty()?tr("发送完成"):tr("已保存到 %1").arg(localPath));action_->setEnabled(false);}
void FileBubble::setFailed(const QString& reason){status_->setText(reason);action_->setEnabled(false);}
