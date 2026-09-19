#pragma once
#include <QLabel>
#include <QCache>
#include <QHash>
#include <functional>

class AvatarLoader : public QObject {
    Q_OBJECT
public:
    static AvatarLoader& instance();
    void bind(QLabel* label,int uid,const QString& fallback,int size);
    void chooseAndUpload(QWidget* parent,int uid);
signals:
    void changed(int uid);
private:
    friend class ResourceBoundaryTests;
    AvatarLoader();
    void load(QLabel* label,int uid,const QString& fallback,int size);
    QCache<QString,QPixmap> cache_{128};
    QHash<int,quint64> versions_;
};
