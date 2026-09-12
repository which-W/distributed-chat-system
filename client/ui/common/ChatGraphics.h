#pragma once
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

inline QPixmap roundAvatar(const QPixmap& source, int size = 48) {
    QPixmap result(size * 2, size * 2);
    result.setDevicePixelRatio(2);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clip);
    painter.fillRect(QRect(0, 0, size, size), QColor("#dbe7ff"));
    if (!source.isNull()) {
        const auto scaled = source.scaled(size * 2, size * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int side = qMin(scaled.width(), scaled.height());
        painter.drawPixmap(QRect(0, 0, size, size), scaled,
                           QRect((scaled.width() - side) / 2, (scaled.height() - side) / 2, side, side));
    }
    return result;
}

inline QPixmap chatGlyph(const QString& name, const QColor& color, int size = 24) {
    QPixmap result(size * 2, size * 2);
    result.setDevicePixelRatio(2);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "smile") {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawPoint(QPointF(8, 9)); p.drawPoint(QPointF(16, 9));
        p.drawArc(QRectF(7, 9, 10, 8), 200 * 16, 140 * 16);
    } else if (name == "person-add") {
        p.drawEllipse(QRectF(6, 3, 7, 7));
        p.drawArc(QRectF(3, 12, 13, 12), 0, 180 * 16);
        p.drawLine(QPointF(19, 12), QPointF(19, 20));
        p.drawLine(QPointF(15, 16), QPointF(23, 16));
    } else if (name == "search") {
        p.drawEllipse(QRectF(3, 3, 12, 12));
        p.drawLine(QPointF(14, 14), QPointF(21, 21));
    } else {
        p.drawEllipse(QRectF(3, 3, 18, 18));
        p.drawLine(QPointF(9, 9), QPointF(15, 15));
        p.drawLine(QPointF(15, 9), QPointF(9, 15));
    }
    return result;
}
