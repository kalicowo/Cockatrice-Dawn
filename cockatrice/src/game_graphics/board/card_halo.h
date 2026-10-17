#ifndef COCKATRICE_CARD_HALO_H
#define COCKATRICE_CARD_HALO_H

#include <QColor>
#include <QConicalGradient>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>
#include <QtMath>

namespace CardHalo
{
constexpr qreal RADIUS = 8.0;
constexpr qreal READING_RADIUS = 12.0;

inline void paintReadingAura(QPainter *painter, const QPainterPath &outline, const QList<QColor> &colors)
{
    if (colors.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    // Broad, low-opacity strokes approximate a Gaussian falloff without a hard rim.
    for (int radius = int(READING_RADIUS); radius >= 1; --radius) {
        QConicalGradient gradient(outline.boundingRect().center(), 90.0);
        for (int i = 0; i <= colors.size(); ++i) {
            QColor color = colors.at(i % colors.size());
            color.setAlphaF(0.045 * qExp(-qreal(radius * radius) / (2.0 * 5.5 * 5.5)));
            gradient.setColorAt(qreal(i) / colors.size(), color);
        }
        painter->setPen(QPen(QBrush(gradient), radius * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(outline);
    }
    painter->restore();
}

inline QList<QColor> colorsForIdentity(const QString &identity, bool hidden)
{
    QList<QColor> colors;
    if (!hidden) {
        const QString symbols = QStringLiteral("WUBRG");
        const QString normalized = identity.toUpper();
        const QList<QColor> palette = {QColor(255, 239, 183), QColor(70, 165, 255), QColor(174, 112, 218),
                                       QColor(255, 91, 62), QColor(78, 214, 128)};
        for (int i = 0; i < symbols.size(); ++i) {
            if (normalized.contains(symbols.at(i))) {
                colors.append(palette.at(i));
            }
        }
    }
    if (colors.isEmpty()) {
        colors.append(QColor(200, 215, 230));
    }
    return colors;
}

inline void paint(QPainter *painter, const QPainterPath &outline, const QList<QColor> &colors, qreal intensity)
{
    if (colors.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    // Layered translucent strokes keep the effect light and its extent predictable.
    for (int layer = 8; layer >= 1; --layer) {
        QConicalGradient gradient(outline.boundingRect().center(), 90.0);
        for (int i = 0; i <= colors.size(); ++i) {
            QColor color = colors.at(i % colors.size());
            color.setAlpha(qBound(0, qRound((12 + (8 - layer) * 5) * intensity), 255));
            gradient.setColorAt(qreal(i) / colors.size(), color);
        }
        painter->setPen(QPen(QBrush(gradient), layer * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(outline);
    }
    painter->restore();
}
} // namespace CardHalo

#endif
