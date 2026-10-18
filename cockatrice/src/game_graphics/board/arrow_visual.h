#ifndef COCKATRICE_ARROW_VISUAL_H
#define COCKATRICE_ARROW_VISUAL_H

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>
#include <cmath>

namespace ArrowVisual
{
constexpr qreal EXTENT = 12.0;

inline QPainterPath hitShape(const QPainterPath &curve, const QPainterPath &head)
{
    QPainterPathStroker stroker;
    stroker.setWidth(14);
    return stroker.createStroke(curve).united(head);
}

inline void paint(QPainter *painter,
                  const QPainterPath &curve,
                  const QPainterPath &head,
                  const QList<QColor> &colors,
                  qreal progress,
                  qreal seconds,
                  bool fullColor)
{
    if (curve.isEmpty() || colors.isEmpty() || progress <= 0) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setOpacity(painter->opacity() * (fullColor ? 1.0 : 0.65));
    QLinearGradient gradient(curve.pointAtPercent(0), curve.pointAtPercent(1));
    for (int i = 0; i < colors.size(); ++i) {
        gradient.setColorAt(colors.size() == 1 ? 0 : qreal(i) / (colors.size() - 1), colors.at(i));
    }
    if (colors.size() == 1) {
        gradient.setColorAt(1, colors.first());
    }
    // Reveal the slim curve without changing its hit area or endpoint geometry.
    QPainterPath visible;
    visible.moveTo(curve.pointAtPercent(0));
    constexpr int samples = 64;
    for (int i = 1; i <= samples; ++i) {
        visible.lineTo(curve.pointAtPercent(progress * i / samples));
    }
    painter->setBrush(Qt::NoBrush);
    const qreal opacity = painter->opacity();
    painter->setOpacity(opacity * 0.16);
    painter->setPen(QPen(QBrush(gradient), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(visible);
    painter->setOpacity(opacity * 0.9);
    painter->setPen(QPen(QBrush(gradient), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(visible);
    painter->setOpacity(opacity * 0.65);
    painter->setPen(QPen(QColor(255, 255, 255, 190), 0.65, Qt::SolidLine, Qt::RoundCap));
    painter->drawPath(visible);
    painter->setOpacity(opacity);
    if (progress >= 0.96) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(gradient);
        painter->drawPath(head);
    }
    // Fixed, bounded particle budget: no allocations, timers or scene items per mote.
    const int count = qBound(10, int(curve.length() / 12), 42);
    painter->setPen(Qt::NoPen);
    for (int i = 0; i < count; ++i) {
        const qreal life = std::fmod(seconds * (0.55 + (i % 5) * 0.035) + i * 0.618034, 1.0);
        const qreal t = std::fmod(i * 0.381966 + seconds * 0.16, 1.0);
        if (t > progress) {
            continue;
        }
        const QPointF point = curve.pointAtPercent(t);
        const qreal angle = qDegreesToRadians(curve.angleAtPercent(t));
        const QPointF normal(qSin(angle), qCos(angle));
        const qreal offset = (i % 2 ? 1 : -1) * (2.0 + life * 6.0);
        QColor color = colors.at(i % colors.size());
        color.setAlphaF(0.72 * qSin(life * 3.141592653589793));
        painter->setBrush(color);
        const qreal radius = 0.55 + (i % 3) * 0.18;
        painter->drawEllipse(point + normal * offset, radius, radius);
    }
    // A small repeating pulse travels toward the target throughout the arrow's life.
    const qreal pulse = std::fmod(seconds * 0.65, 1.0);
    if (pulse <= progress) {
        painter->setBrush(QColor(255, 255, 255, 210));
        painter->drawEllipse(curve.pointAtPercent(pulse), 1.5, 1.5);
    }
    painter->restore();
}
} // namespace ArrowVisual

#endif
