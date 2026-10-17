#ifndef COCKATRICE_CARD_DRAW_ANIMATION_H
#define COCKATRICE_CARD_DRAW_ANIMATION_H

#include "../card_dimensions.h"
#include "../z_values.h"
#include "card_halo.h"

#include <QEasingCurve>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QQueue>
#include <QVariantAnimation>
#include <QtMath>
#include <cmath>
#include <functional>
#include <utility>

class CardDrawAnimation : public QGraphicsObject
{
public:
    static constexpr int TRAVEL_MS = 350;
    static constexpr int FLIP_MS = 360;
    static constexpr int READ_MS = 2200;
    static constexpr int HAND_MS = 450;
    static constexpr int TOTAL_MS = TRAVEL_MS + FLIP_MS + READ_MS + HAND_MS;
    static constexpr int FAST_MS = 650;

    struct Request
    {
        QPointer<QGraphicsObject> card;
        QPointF libraryCenter;
        qreal libraryScale = 1.0;
        qreal sourceAngle = 0.0;
        QString name;
        std::function<bool()> mayReveal;
        std::function<QPixmap(bool)> picture;
        qreal originalOpacity = 1.0;
        bool directToHand = false;
        int groupSize = 1;
        int groupIndex = 0;
        bool startsFaceUp = false;
        bool endsFaceUp = true;
        std::function<bool()> isFaceVisible;
        std::function<void()> refreshDestination;
        std::function<QList<QColor>()> readingAura;
        bool roundCorners = true;
    };

private:
    QQueue<Request> pending;
    Request current;
    QVariantAnimation *timeline;
    QPointF focus;
    qreal focusScale = 1.0;
    qreal flipWidth = 1.0;
    bool faceUp = false;
    bool standaloneMode = false;
    QPixmap cachedReadingAura;

    static bool valid(const Request &request)
    {
        return request.card && request.mayReveal && request.mayReveal();
    }

    static void restore(Request &request)
    {
        if (request.card) {
            request.card->setOpacity(request.originalOpacity);
        }
        if (request.refreshDestination) {
            request.refreshDestination();
        }
        request = {};
    }

    void discardInvalidRequests()
    {
        for (int i = pending.size() - 1; i >= 0; --i) {
            if (!valid(pending[i])) {
                restore(pending[i]);
                pending.removeAt(i);
            }
        }
    }

    void startNext()
    {
        timeline->stop();
        cachedReadingAura = QPixmap();
        discardInvalidRequests();
        if (pending.isEmpty()) {
            setVisible(false);
            if (standaloneMode) {
                deleteLater();
            }
            return;
        }
        current = pending.dequeue();
        // Pre-render once before the flip. Each frame then draws one small image
        // instead of repeatedly rasterizing translucent gradient strokes.
        if (!current.directToHand && current.readingAura) {
            const QRectF bounds = boundingRect();
            cachedReadingAura = QPixmap(bounds.size().toSize() * 2);
            cachedReadingAura.setDevicePixelRatio(2.0);
            cachedReadingAura.fill(Qt::transparent);
            QPainter auraPainter(&cachedReadingAura);
            auraPainter.translate(-bounds.topLeft());
            QPainterPath outline;
            const qreal radius = current.roundCorners ? CardDimensions::WIDTH_F * 0.05 : 0.0;
            outline.addRoundedRect(cardRect(), radius, radius);
            CardHalo::paintReadingAura(&auraPainter, outline, current.readingAura());
        }
        const int duration = current.directToHand ? FAST_MS : TOTAL_MS;
        timeline->setDuration(duration);
        timeline->setEndValue(duration);
        QRectF visible = scene()->sceneRect();
        if (!scene()->views().isEmpty()) {
            auto *view = scene()->views().first();
            visible = view->mapToScene(view->viewport()->rect()).boundingRect();
        }
        focus = visible.center();
        focusScale = qMax(0.1, qMin(visible.width() * 0.34 / CardDimensions::WIDTH_F,
                                    visible.height() * 0.55 / CardDimensions::HEIGHT_F));
        if (!current.directToHand && current.groupSize > 1) {
            const qreal groupWidth = CardDimensions::WIDTH_F * (current.groupSize + 0.26 * (current.groupSize - 1));
            focusScale = qMin(focusScale, visible.width() * 0.90 / groupWidth);
        }
        focusScale *= 0.8;
        if (!current.directToHand && current.groupSize > 1) {
            const qreal spacing = CardDimensions::WIDTH_F * focusScale * 1.26;
            focus.rx() += (current.groupIndex - (current.groupSize - 1) / 2.0) * spacing;
        }
        setVisible(true);
        advance(0);
        timeline->start();
    }

    void advance(int elapsed)
    {
        discardInvalidRequests();
        if (!valid(current)) {
            timeline->stop();
            restore(current);
            startNext();
            return;
        }
        faceUp = elapsed >= TRAVEL_MS + FLIP_MS / 2 ? current.endsFaceUp : current.startsFaceUp;
        flipWidth = 1.0;
        setRotation(0);
        if (current.directToHand) {
            const qreal t = QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(qreal(elapsed) / FAST_MS);
            const auto *card = current.card.data();
            const auto transform = card->sceneTransform();
            const qreal targetScale = qSqrt(transform.m11() * transform.m11() + transform.m12() * transform.m12());
            setPos(current.libraryCenter * (1.0 - t) + card->mapToScene(card->boundingRect().center()) * t);
            setScale(current.libraryScale * (1.0 - t) + targetScale * t);
            const qreal targetAngle = qRadiansToDegrees(qAtan2(transform.m12(), transform.m11()));
            const qreal angleDelta = std::remainder(targetAngle - current.sourceAngle, 360.0);
            setRotation(current.sourceAngle + angleDelta * t);
            const qreal flip = qBound(0.0, (qreal(elapsed) / FAST_MS - 0.15) / 0.7, 1.0);
            faceUp = flip >= 0.5 ? current.endsFaceUp : current.startsFaceUp;
            flipWidth = current.startsFaceUp == current.endsFaceUp ? 1.0 : qMax(0.015, qAbs(qCos(M_PI * flip)));
        } else if (elapsed < TRAVEL_MS) {
            const qreal t = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(qreal(elapsed) / TRAVEL_MS);
            setPos(current.libraryCenter * (1.0 - t) + focus * t);
            setScale(current.libraryScale * (1.0 - t) + focusScale * t);
        } else if (elapsed < TRAVEL_MS + FLIP_MS) {
            setPos(focus);
            setScale(focusScale);
            const qreal t = qreal(elapsed - TRAVEL_MS) / FLIP_MS;
            flipWidth = current.startsFaceUp == current.endsFaceUp ? 1.0 : qMax(0.015, qAbs(qCos(M_PI * t)));
        } else if (elapsed < TRAVEL_MS + FLIP_MS + READ_MS) {
            setPos(focus);
            setScale(focusScale);
        } else {
            const qreal t = QEasingCurve(QEasingCurve::InOutCubic)
                                .valueForProgress(qreal(elapsed - TRAVEL_MS - FLIP_MS - READ_MS) / HAND_MS);
            const auto *card = current.card.data();
            const QPointF target = card->mapToScene(card->boundingRect().center());
            const QTransform transform = card->sceneTransform();
            const qreal targetScale = qSqrt(transform.m11() * transform.m11() + transform.m12() * transform.m12());
            setPos(focus * (1.0 - t) + target * t);
            setScale(focusScale * (1.0 - t) + targetScale * t);
            setRotation(qRadiansToDegrees(qAtan2(transform.m12(), transform.m11())) * t);
        }
        update();
    }

    explicit CardDrawAnimation(QGraphicsScene *scene, bool standalone)
        : timeline(new QVariantAnimation(this)), standaloneMode(standalone)
    {
        QObject::setParent(scene);
        scene->addItem(this);
        setZValue(ZValues::HOVERED_CARD + 10);
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(false);
        setVisible(false);
        timeline->setDuration(TOTAL_MS);
        timeline->setStartValue(0);
        timeline->setEndValue(TOTAL_MS);
        connect(timeline, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) { advance(value.toInt()); });
        connect(timeline, &QVariantAnimation::finished, this, [this]() {
            restore(current);
            if (standaloneMode) {
                setVisible(false);
                deleteLater();
                return;
            }
            startNext();
        });
    }

public:
    ~CardDrawAnimation() override
    {
        restore(current);
        for (auto &request : pending) {
            restore(request);
        }
    }

    static CardDrawAnimation *enqueue(QGraphicsScene *scene, Request request)
    {
        if (!scene || !valid(request)) {
            return nullptr;
        }
        CardDrawAnimation *queue = nullptr;
        const bool standalone = request.directToHand || request.groupSize > 1;
        for (auto *child : scene->children()) {
            if (auto *existing = dynamic_cast<CardDrawAnimation *>(child)) {
                // Undo/redraw can reuse the same card before its preview finishes.
                for (int i = existing->pending.size() - 1; i >= 0; --i) {
                    if (existing->pending[i].card == request.card) {
                        restore(existing->pending[i]);
                        existing->pending.removeAt(i);
                    }
                }
                if (existing->current.card == request.card) {
                    existing->timeline->stop();
                    restore(existing->current);
                    existing->startNext();
                }
                if (!existing->standaloneMode && !standalone) {
                    queue = existing;
                }
            }
        }
        if (!queue) {
            queue = new CardDrawAnimation(scene, standalone);
        }
        request.originalOpacity = request.card->opacity();
        request.card->setOpacity(0.0);
        if (request.refreshDestination) {
            request.refreshDestination();
        }
        queue->pending.enqueue(std::move(request));
        if (!queue->current.card) {
            queue->startNext();
        }
        return queue;
    }

    QRectF cardRect() const
    {
        return QRectF(-CardDimensions::WIDTH_HALF_F, -CardDimensions::HEIGHT_HALF_F, CardDimensions::WIDTH_F,
                      CardDimensions::HEIGHT_F);
    }

    QRectF boundingRect() const override
    {
        const qreal radius = CardHalo::READING_RADIUS;
        return cardRect().adjusted(-radius, -radius, radius, radius);
    }

    QPainterPath shape() const override
    {
        return {};
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        // Re-check at paint time so losing ownership cannot leak a queued card face.
        if (!valid(current)) {
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->scale(flipWidth, 1.0);
        const bool showFront = faceUp && (!current.isFaceVisible || current.isFaceVisible());
        if (showFront && !cachedReadingAura.isNull()) {
            painter->drawPixmap(boundingRect().topLeft(), cachedReadingAura);
        }
        const QPixmap image = current.picture(showFront);
        // Clip both images and fallback content, after drawing the outer aura.
        QPainterPath cardOutline;
        const qreal radius = current.roundCorners ? CardDimensions::WIDTH_F * 0.05 : 0.0;
        cardOutline.addRoundedRect(cardRect(), radius, radius);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(cardOutline, Qt::IntersectClip);
        if (!image.isNull()) {
            painter->drawPixmap(cardRect(), image, image.rect());
        } else {
            painter->fillRect(cardRect(), showFront ? QColor(36, 39, 45) : QColor(30, 40, 70));
            if (showFront) {
                painter->setPen(Qt::white);
                painter->drawText(cardRect().adjusted(3, 3, -3, -3), Qt::AlignCenter | Qt::TextWordWrap, current.name);
            }
        }
        painter->restore();
    }
};

#endif
