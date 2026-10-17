#include "card_drag_item.h"

#include "../game_scene.h"
#include "../zones/card_zone.h"
#include "../zones/table_zone.h"
#include "../zones/view_zone.h"
#include "card_item.h"

#include <QBrush>
#include <QColor>
#include <QConicalGradient>
#include <QCursor>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QList>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QSharedPointer>
#include <QVariantAnimation>
#include <QtMath>
#include <libcockatrice/card/game_specific_terms.h>
#include <qnamespace.h>

class CardZoneLogic;

namespace
{
constexpr qreal DRAG_HALO_RADIUS = 8.0;
constexpr int DRAG_HALO_BREATH_DURATION_MS = 2400;

QList<QColor> dragHaloColors(const AbstractCardItem *item, bool forceFaceDown)
{
    QList<QColor> colors;
    const auto card = item->getCard();
    // Never infer hidden information from the card name or mana cost.
    if (!forceFaceDown && !item->getFaceDown() && card) {
        const QString identity = card.getInfo().getProperty(Mtg::ColorIdentity).toUpper();
        const QString symbols = QStringLiteral("WUBRG");
        const QList<QColor> palette = {QColor(255, 239, 183), QColor(70, 165, 255), QColor(174, 112, 218),
                                       QColor(255, 91, 62), QColor(78, 214, 128)};
        for (int i = 0; i < symbols.size(); ++i) {
            if (identity.contains(symbols.at(i))) {
                colors.append(palette.at(i));
            }
        }
    }
    if (colors.isEmpty()) {
        colors.append(QColor(200, 215, 230));
    }
    return colors;
}
} // namespace

CardDragItem::CardDragItem(CardItem *_item,
                           int _id,
                           const QPointF &_hotSpot,
                           bool _forceFaceDown,
                           AbstractCardDragItem *parentDrag)
    : AbstractCardDragItem(_item, _hotSpot, parentDrag), id(_id), forceFaceDown(_forceFaceDown), occupied(false),
      currentZone(0)
{
    auto *breathing = new QVariantAnimation(this);
    breathing->setDuration(DRAG_HALO_BREATH_DURATION_MS);
    breathing->setStartValue(0.0);
    breathing->setEndValue(2.0 * M_PI);
    breathing->setLoopCount(-1);
    connect(breathing, &QVariantAnimation::valueChanged, this, [this](const QVariant &phase) {
        // A cosine keeps both the brightness and its slope continuous at the loop boundary.
        haloIntensity = 0.75 + 0.25 * qCos(phase.toReal());
        update();
    });
    breathing->start();
}

QRectF CardDragItem::boundingRect() const
{
    return AbstractCardDragItem::boundingRect().adjusted(-DRAG_HALO_RADIUS, -DRAG_HALO_RADIUS, DRAG_HALO_RADIUS,
                                                         DRAG_HALO_RADIUS);
}

QPainterPath CardDragItem::shape() const
{
    // The halo must not enlarge the interactive area or the ghost mask.
    return item->shape();
}

void CardDragItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    const auto colors = dragHaloColors(item, forceFaceDown);
    const QPainterPath outline = shape();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    // Translucent strokes build a soft halo without a blur effect or extra scene items.
    for (int layer = 8; layer >= 1; --layer) {
        QConicalGradient gradient(AbstractCardDragItem::boundingRect().center(), 90.0);
        for (int i = 0; i <= colors.size(); ++i) {
            QColor color = colors.at(i % colors.size());
            color.setAlpha(qRound((12 + (8 - layer) * 5) * haloIntensity));
            gradient.setColorAt(qreal(i) / colors.size(), color);
        }
        painter->setPen(QPen(QBrush(gradient), layer * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(outline);
    }
    painter->restore();

    AbstractCardDragItem::paint(painter, option, widget);

    if (occupied) {
        painter->fillPath(shape(), QColor(200, 0, 0, 100));
    }
}

void CardDragItem::updatePosition(const QPointF &cursorScenePos)
{
    QList<QGraphicsItem *> colliding =
        scene()->items(cursorScenePos, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder,
                       static_cast<GameScene *>(scene())->getViewTransform());

    CardZone *cardZone = 0;
    ZoneViewZone *zoneViewZone = 0;
    for (int i = colliding.size() - 1; i >= 0; i--) {
        CardZone *temp = qgraphicsitem_cast<CardZone *>(colliding.at(i));
        if (!cardZone) {
            cardZone = temp;
        }
        if (!zoneViewZone) {
            zoneViewZone = qobject_cast<ZoneViewZone *>(temp);
        }
    }
    CardZone *cursorZone = 0;
    if (zoneViewZone) {
        cursorZone = zoneViewZone;
    } else if (cardZone) {
        cursorZone = cardZone;
    }

    // Always update the current zone, even if its null, to cancel the drag
    // instead of dropping cards into an non-intuitive location.
    currentZone = cursorZone;

    if (!cursorZone) {
        // Avoid the cards getting stuck visually when not over
        // any zone.
        QPointF newPos = cursorScenePos - hotSpot;

        if (newPos != pos()) {
            for (int i = 0; i < childDrags.size(); i++) {
                childDrags[i]->setPos(newPos + childDrags[i]->getHotSpot());
            }
            setPos(newPos);
        }

        return;
    }

    QPointF zonePos = currentZone->scenePos();
    QPointF cursorPosInZone = cursorScenePos - zonePos;

    // If we are on a Table, we center the card around the cursor, because we
    // snap it into place and no longer see it being dragged.
    //
    // For other zones (where we do display the card under the cursor), we use
    // the hotspot to feel like the card was dragged at the corresponding
    // position.
    QPointF closestGridPoint = cursorPosInZone - hotSpot;

    QPointF newPos = zonePos + closestGridPoint;

    if (newPos != pos()) {
        for (int i = 0; i < childDrags.size(); i++) {
            childDrags[i]->setPos(newPos + childDrags[i]->getHotSpot());
        }
        setPos(newPos);

        bool newOccupied = false;
        TableZone *table = qobject_cast<TableZone *>(cursorZone);
        if (table) {
            if (table->getCardFromCoords(closestGridPoint)) {
                newOccupied = true;
            }
        }
        if (newOccupied != occupied) {
            occupied = newOccupied;
            update();
        }
    }
}

void CardDragItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    setCursor(Qt::OpenHandCursor);
    QGraphicsScene *sc = scene();
    QPointF sp = pos();
    sc->removeItem(this);

    QList<CardDragItem *> dragItemList;
    CardZoneLogic *startZone = static_cast<CardItem *>(item)->getZone();
    if (currentZone && !(static_cast<CardItem *>(item)->getAttachedTo() && (startZone == currentZone->getLogic()))) {
        if (!occupied) {
            dragItemList.append(this);
        }

        for (int i = 0; i < childDrags.size(); i++) {
            CardDragItem *c = static_cast<CardDragItem *>(childDrags[i]);
            if (!occupied &&
                !(static_cast<CardItem *>(c->item)->getAttachedTo() && (startZone == currentZone->getLogic())) &&
                !c->occupied) {
                dragItemList.append(c);
            }
            sc->removeItem(c);
        }
    }

    if (currentZone) {
        currentZone->handleDropEvent(dragItemList, startZone, (sp - currentZone->scenePos()).toPoint());
    }

    event->accept();
}
