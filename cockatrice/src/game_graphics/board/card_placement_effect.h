#ifndef COCKATRICE_CARD_PLACEMENT_EFFECT_H
#define COCKATRICE_CARD_PLACEMENT_EFFECT_H

#include "card_halo.h"

#include <QEasingCurve>
#include <QGraphicsObject>
#include <QRadialGradient>
#include <QVariantAnimation>
#include <QtMath>
#include <functional>
#include <utility>

// A separate child follows the real card's transforms without capturing or moving its image.
class CardPlacementEffect : public QGraphicsObject
{
    QRectF cardRect;
    std::function<QList<QColor>()> colors;
    qreal progress = 0.0;

public:
    CardPlacementEffect(const QPainterPath &outline, std::function<QList<QColor>()> colors, QGraphicsItem *parent)
        : QGraphicsObject(parent), cardRect(outline.boundingRect()), colors(std::move(colors))
    {
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(false);
        setFlag(QGraphicsItem::ItemStacksBehindParent);
        auto *animation = new QVariantAnimation(this);
        animation->setDuration(420);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress = value.toReal();
            update();
        });
        connect(animation, &QVariantAnimation::finished, this, &QObject::deleteLater);
        animation->start();
    }

    QRectF boundingRect() const override
    {
        // Fixed bounds include the wave and spark tips for every frame.
        return cardRect.adjusted(-42, -42, 42, 42);
    }

    QPainterPath shape() const override
    {
        return {}; // Decorative only: no hover, selection or drag interception.
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const auto palette = colors();
        if (palette.isEmpty() || progress <= 0.0 || progress >= 1.0) {
            return;
        }
        const qreal intensity = qMin(progress / 0.12, 1.0) * qPow(1.0 - progress, 1.4);
        const qreal spread = 2.0 + 30.0 * QEasingCurve(QEasingCurve::OutCubic).valueForProgress(progress);
        const QPointF center = cardRect.center();
        const QRectF ring = cardRect.adjusted(-spread, -spread, spread, spread);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        // Soft overlapping clouds spread from the card edges, filling the space
        // inside the impact wave instead of adding another hard outline.
        painter->setPen(Qt::NoPen);
        for (int i = 0; i < 16; ++i) {
            const qreal along = ((i % 4) + 0.5) / 4.0;
            QPointF edge;
            QPointF outward;
            switch (i / 4) {
                case 0:
                    edge = QPointF(cardRect.left() + along * cardRect.width(), cardRect.top());
                    outward = QPointF(0, -1);
                    break;
                case 1:
                    edge = QPointF(cardRect.right(), cardRect.top() + along * cardRect.height());
                    outward = QPointF(1, 0);
                    break;
                case 2:
                    edge = QPointF(cardRect.right() - along * cardRect.width(), cardRect.bottom());
                    outward = QPointF(0, 1);
                    break;
                default:
                    edge = QPointF(cardRect.left(), cardRect.bottom() - along * cardRect.height());
                    outward = QPointF(-1, 0);
                    break;
            }
            const QPointF cloudCenter = edge + outward * (2.0 + spread * 0.25);
            const qreal radius = 10.0 + spread * 0.65;
            QRadialGradient diffusion(cloudCenter, radius);
            QColor color = palette.at(i * palette.size() / 16);
            color.setAlpha(qRound(120 * intensity));
            diffusion.setColorAt(0.0, color);
            color.setAlpha(qRound(65 * intensity));
            diffusion.setColorAt(0.4, color);
            color.setAlpha(0);
            diffusion.setColorAt(1.0, color);
            painter->setBrush(diffusion);
            painter->drawEllipse(cloudCenter, radius, radius);
        }
        painter->setBrush(Qt::NoBrush);
        for (int layer = 3; layer >= 1; --layer) {
            QConicalGradient gradient(center, 90.0);
            for (int i = 0; i <= palette.size(); ++i) {
                QColor color = palette.at(i % palette.size());
                color.setAlpha(qRound((layer == 1 ? 230 : 35) * intensity));
                gradient.setColorAt(qreal(i) / palette.size(), color);
            }
            painter->setPen(QPen(QBrush(gradient), layer == 1 ? 2.5 : layer * 4.0));
            painter->drawEllipse(ring);
        }
        // Short radial sparks make the landing read as an impact.
        for (int i = 0; i < 20; ++i) {
            const qreal angle = i * (2.0 * M_PI / 20.0);
            const QPointF direction(qCos(angle), qSin(angle));
            const QPointF edge(center.x() + direction.x() * ring.width() / 2.0,
                               center.y() + direction.y() * ring.height() / 2.0);
            QColor color = palette.at(i * palette.size() / 20);
            color.setAlpha(qRound(240 * intensity));
            painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(edge, edge + direction * (3.0 + (i % 3) * 2.0));
        }
        painter->restore();
    }
};

#endif
