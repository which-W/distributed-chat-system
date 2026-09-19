#include "AvatarLoader.h"
#include "ResourceHttp.h"
#include "TcpMgr.h"
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>

namespace {
QPixmap rounded(const QPixmap& source,int size) {
    QPixmap result(size,size); result.fill(Qt::transparent); QPainter p(&result); p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip; clip.addEllipse(0,0,size,size); p.setClipPath(clip);
    if (source.isNull()) p.fillRect(result.rect(),QColor(140,150,165));
    else p.drawPixmap(result.rect(),source.scaled(size,size,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation)); return result;
}
}
AvatarLoader& AvatarLoader::instance() { static AvatarLoader loader; return loader; }
AvatarLoader::AvatarLoader() {
    // 退出后清除当前账号的内存图片及版本，磁盘缓存由服务器与账号组成的目录隔离。
    connect(&ResourceHttp::instance(),&ResourceHttp::resetAccount,this,[this] { cache_.clear(); versions_.clear(); });
    connect(TcpMgr::Getinstance().get(),&TcpMgr::sig_avatar_changed,this,[this](const QJsonObject& v) { versions_[v["uid"].toInt()]=v["version"].toVariant().toULongLong(); emit changed(v["uid"].toInt()); });
}
void AvatarLoader::bind(QLabel* label,int uid,const QString& fallback,int size) {
    label->setProperty("resourceAvatarUid",uid);
    if (!label->property("resourceAvatarBound").toBool()) {
        label->setProperty("resourceAvatarBound",true);
        connect(this,&AvatarLoader::changed,label,[this,label](int changedUid) {
            const auto uid=label->property("resourceAvatarUid").toInt();
            if (uid==changedUid) load(label,uid,label->property("resourceAvatarFallback").toString(),label->property("resourceAvatarSize").toInt());
        });
    }
    label->setProperty("resourceAvatarFallback",fallback); label->setProperty("resourceAvatarSize",size); load(label,uid,fallback,size);
}
void AvatarLoader::load(QLabel* label,int uid,const QString& fallback,int size) {
    label->setPixmap(rounded(QPixmap(fallback),size)); if (uid<=0) return;
    QPointer<QLabel> target(label); const auto account=ResourceHttp::instance().accountKey();
    // 标签可能被复用于其他联系人，响应落地前还要确认标签、账号和头像版本仍然匹配。
    ResourceHttp::instance().request("GET","/users/"+QString::number(uid)+"/avatar",{},[=](int code,const QByteArray& bytes) {
        if (!target || target->property("resourceAvatarUid").toInt()!=uid || account!=ResourceHttp::instance().accountKey() || code!=200) return;
        const auto v=QJsonDocument::fromJson(bytes).object(); const auto version=v["version"].toVariant().toULongLong();
        if (version<versions_.value(uid)) return;
        versions_[uid]=version;
        const auto id=v["avatar_id"].toString();
        static const QRegularExpression uuid("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
        if (!uuid.match(id).hasMatch()) return;
        const auto variant=size<=64 ? 64 : (size<=128 ? 128 : 256); const auto key=account+"/"+id+"-"+QString::number(variant);
        if (auto* cached=cache_.object(key)) { target->setPixmap(rounded(*cached,size)); return; }
        const auto directory=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/avatars/"+account;
        QDir().mkpath(directory); const auto path=directory+"/"+id+"-"+QString::number(variant)+".png";
        QPixmap cached(path);
        if (!cached.isNull()) { cache_.insert(key,new QPixmap(cached)); target->setPixmap(rounded(cached,size)); return; }
        ResourceHttp::instance().request("GET","/avatars/"+id+"/"+QString::number(variant),{},[=](int status,const QByteArray& data) {
            if (!target || target->property("resourceAvatarUid").toInt()!=uid || account!=ResourceHttp::instance().accountKey() || status!=200 || version<versions_.value(uid)) return;
            QPixmap pixmap; if (!pixmap.loadFromData(data,"PNG")) return;
            QSaveFile file(path); if (file.open(QIODevice::WriteOnly) && file.write(data)==data.size()) file.commit();
            cache_.insert(key,new QPixmap(pixmap)); target->setPixmap(rounded(pixmap,size));
        });
    });
}
void AvatarLoader::chooseAndUpload(QWidget* parent,int uid) {
    const auto path=QFileDialog::getOpenFileName(parent,tr("选择头像"),{},tr("图片 (*.png *.jpg *.jpeg)")); if (path.isEmpty()) return;
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size()>5*1024*1024) { QMessageBox::warning(parent,tr("头像"),tr("请选择不超过 5 MiB 的图片")); return; }
    const auto bytes=file.readAll(); QPointer<QWidget> target(parent);
    ResourceHttp::instance().request("GET","/users/"+QString::number(uid)+"/avatar",{},[=](int code,const QByteArray& metadata) {
        if (!target) return;
        if (code!=200) { QMessageBox::warning(target,tr("头像"),tr("无法读取头像版本")); return; }
        const auto version=QJsonDocument::fromJson(metadata).object()["version"].toVariant().toULongLong();
        ResourceHttp::instance().request("PUT","/users/me/avatar",bytes,[=](int status,const QByteArray&) {
            if (!target) return;
            if (status==200) emit changed(uid); else QMessageBox::warning(target,tr("头像"),tr("头像更新失败 (%1)，请重试").arg(status));
        },{{"If-Match",QByteArray::number(version)}});
    });
}
