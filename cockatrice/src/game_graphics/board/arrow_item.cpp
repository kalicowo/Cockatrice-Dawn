#define _USE_MATH_DEFINES
#include "arrow_item.h"

#include "../../client/settings/cache_settings.h"
#include "../../game/board/arrow_data.h"
#include "../../game/player/player_actions.h"
#include "../../game/player/player_info.h"
#include "../../game/player/player_logic.h"
#include "../../game/zones/card_zone_logic.h"
#include "../game_scene.h"
#include "../player/player_target.h"
#include "../z_values.h"
#include "arrow_visual.h"
#include "card_halo.h"
#include "card_item.h"
#include "libcockatrice/card/printing/exact_card.h"
#include "libcockatrice/protocol/pb/color.pb.h"

#include <QBrush>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QString>
#include <QTransform>
#include <QtGlobal>
#include <QtMath>
#include <libcockatrice/card/card_info.h>
#include <libcockatrice/card/game_specific_terms.h>
#include <libcockatrice/protocol/pb/command_attach_card.pb.h>
#include <libcockatrice/protocol/pb/command_create_arrow.pb.h>
#include <libcockatrice/settings/cards_display_settings.h>
#include <libcockatrice/settings/interface_settings.h>
#include <libcockatrice/utility/color.h>
#include <libcockatrice/utility/zone_names.h>
#include <math.h>
#include <qnamespace.h>
#include <string>
#include <utility>

namespace
{
constexpr qreal kMinStrokeDurationMs = 200.0;
constexpr qreal kMaxStrokeDurationMs = 450.0;
constexpr qreal kMsPerPixel = 0.8;

/// @brief Ease-out cubic, for a natural "slow in / slow out" reveal.
qreal easeOutCubic(qreal t)
{
    const qreal inverse = 1.0 - t;
    return 1.0 - inverse * inverse * inverse;
}
} // namespace

ArrowItem::ArrowItem(QSharedPointer<const ArrowData> _data, ArrowTarget *_startItem, ArrowTarget *_targetItem)
    : data(std::move(_data)), startItem(_startItem), targetItem(_targetItem)
{
    setZValue(ZValues::ARROWS);

    auto doUpdate = [this]() {
        if (startItem && targetItem) {
            updatePath();
        }
    };

    if (startItem) {
        connect(startItem, &ArrowTarget::scenePositionChanged, this, doUpdate);
        connect(startItem, &QObject::destroyed, this, &ArrowItem::onTargetDestroyed);
    }
    if (targetItem) {
        connect(targetItem, &ArrowTarget::scenePositionChanged, this, doUpdate);
        connect(targetItem, &QObject::destroyed, this, &ArrowItem::onTargetDestroyed);
    }

    if (startItem && targetItem) {
        updatePath();
    }
}

ArrowItem::~ArrowItem()
{
    if (auto *scene = qobject_cast<GameScene *>(this->scene())) {
        scene->unregisterAnimationItem(this);
    }
}

void ArrowItem::onTargetDestroyed()
{
    if (data->id == -1) {
        // Drag and attach arrows are never inserted into the arrow registry and
        // have no server-side counterpart, so no deletion event can clean them
        // up. Delete them locally when either endpoint is destroyed.
        delArrow();
        return;
    }

    emit requestDeletion(data->creatorId, data->id);
}

void ArrowItem::delArrow()
{
    if (targetItem) {
        targetItem->setBeingPointedAt(false);
    }
    deleteLater();
}

void ArrowItem::updatePath()
{
    if (!targetItem) {
        return;
    }

    QPointF endPoint = targetItem->mapToScene(
        QPointF(targetItem->boundingRect().width() / 2, targetItem->boundingRect().height() / 2));
    updatePath(endPoint);
}

void ArrowItem::updatePath(const QPointF &endPoint)
{
    if (!startItem) {
        return;
    }
    const QPointF startPoint = startItem->mapToScene(startItem->boundingRect().center());
    const QLineF line(startPoint, endPoint);
    const qreal length = line.length();
    prepareGeometryChange();
    centerLine = QPainterPath();
    headPath = QPainterPath();
    path = QPainterPath();
    if (length >= 30) {
        centerLine.moveTo(0, 0);
        centerLine.quadTo(QPointF(length / 2, length * 0.22), QPointF(length, 0));
        const QPointF tip(length, 0);
        QLineF tangent(centerLine.pointAtPercent(0.99), tip);
        tangent.setLength(10);
        const QPointF direction = tangent.p2() - tangent.p1();
        const QPointF normal(-direction.y() * 0.38, direction.x() * 0.38);
        headPath.moveTo(tip);
        headPath.lineTo(tip - direction + normal);
        headPath.lineTo(tip - direction - normal);
        headPath.closeSubpath();
        path = ArrowVisual::hitShape(centerLine, headPath);
    }
    setPos(startPoint);
    setTransform(QTransform().rotate(-line.angle()));
    // Preview arrows are not added through GameScene::addArrow().
    if (data->id == -1 && !animationStarted && !centerLine.isEmpty()) {
        startDrawAnimation();
    }
    update();
}

void ArrowItem::startDrawAnimation()
{
    if (centerLine.isEmpty()) {
        return;
    }
    const bool reveal = SettingsCache::instance().cardsDisplay().getArrowDrawAnimation() && data->id != -1;
    strokeDurationMs =
        reveal ? qBound(kMinStrokeDurationMs, centerLine.length() * kMsPerPixel, kMaxStrokeDurationMs) : 0;
    animationStarted = false;
    lastAnimationFrame = -1;
    drawProgress = reveal ? 0.0 : 1.0;
    update();
    if (auto *gameScene = qobject_cast<GameScene *>(scene())) {
        gameScene->registerAnimationItem(this);
    }
}

bool ArrowItem::animationEvent()
{
    if (!animationStarted) {
        animationClock.start();
        animationStarted = true;
    }
    const qint64 elapsed = animationClock.elapsed();
    // The shared scene timer ticks at 100 Hz; cap visual updates at about 30 fps.
    const qint64 frame = elapsed / 33;
    if (frame != lastAnimationFrame) {
        lastAnimationFrame = frame;
        animationSeconds = elapsed / 1000.0;
        drawProgress = strokeDurationMs > 0 ? easeOutCubic(qBound<qreal>(0, elapsed / strokeDurationMs, 1)) : 1.0;
        update();
    }
    return true;
}

void ArrowItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    QList<QColor> colors{data->color};
    if (const auto *card = qgraphicsitem_cast<CardItem *>(startItem.data())) {
        colors =
            CardHalo::colorsForIdentity(card->getCard().getInfo().getProperty(Mtg::ColorIdentity), card->getFaceDown());
    }
    ArrowVisual::paint(painter, centerLine, headPath, colors, drawProgress, animationSeconds, fullColor);
}
void ArrowItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!data->isLocalCreator) {
        event->ignore();
        return;
    }

    for (auto *item : scene()->items(event->scenePos())) {
        if (qgraphicsitem_cast<CardItem *>(item)) {
            event->ignore();
            return;
        }
    }

    event->accept();
    if (event->button() == Qt::RightButton) {
        emit requestDeletion(data->creatorId, data->id);
    }
}

// ArrowDragItem

ArrowDragItem::ArrowDragItem(PlayerLogic *_owner, ArrowTarget *_startItem, const QColor &_color, int _deleteInPhase)
    : ArrowItem(QSharedPointer<ArrowData>::create(ArrowData{.creatorId = _owner->getPlayerInfo()->getId(),
                                                            .isLocalCreator = true,
                                                            .id = -1,
                                                            .color = _color}),
                _startItem,
                nullptr),
      player(_owner), deleteInPhase(_deleteInPhase)
{
}

void ArrowDragItem::addChildArrow(ArrowDragItem *child)
{
    childArrows.append(child);
}

void ArrowDragItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (targetLocked || !startItem) {
        return;
    }

    const QPointF endPos = event->scenePos();

    ArrowTarget *cursorItem = nullptr;
    qreal cursorItemZ = -1;
    for (auto *item : scene()->items(endPos)) {
        ArrowTarget *candidate = nullptr;
        if (auto *card = qgraphicsitem_cast<CardItem *>(item)) {
            candidate = card;
        } else if (auto *pt = qgraphicsitem_cast<PlayerTarget *>(item)) {
            candidate = pt;
        }

        if (candidate && candidate->zValue() > cursorItemZ) {
            cursorItem = candidate;
            cursorItemZ = candidate->zValue();
        }
    }

    if (cursorItem != targetItem) {
        if (targetItem) {
            disconnect(positionConnection);
            targetItem->setBeingPointedAt(false);
        }

        targetItem = cursorItem;
        fullColor = (cursorItem != nullptr);

        if (cursorItem && cursorItem != startItem) {
            cursorItem->setBeingPointedAt(true);
            positionConnection =
                connect(cursorItem, &ArrowTarget::scenePositionChanged, this, [this]() { updatePath(); });
        }
    }

    targetItem ? updatePath() : updatePath(endPos);
    update();

    for (auto *child : childArrows) {
        child->mouseMoveEvent(event);
    }
}

void ArrowDragItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (!startItem) {
        // The source card was destroyed while the arrow was being drawn.
        // Clean up the arrow and its children instead of leaking them.
        delArrow();
        for (auto *child : childArrows) {
            child->mouseReleaseEvent(event);
        }
        return;
    }

    if (targetItem && targetItem != startItem) {
        CardItem *startCard = qgraphicsitem_cast<CardItem *>(startItem);
        // For now, we can safely assume that the start item is always a card.
        // The target item can be a player as well.
        if (!startCard) {
            delArrow();
            return;
        }

        CardZoneLogic *startZone = startCard->getZone();

        Command_CreateArrow cmd;
        cmd.mutable_arrow_color()->CopyFrom(convertQColorToColor(data->color));
        cmd.set_start_player_id(startZone->getPlayer()->getPlayerInfo()->getId());
        cmd.set_start_zone(startZone->getName().toStdString());
        cmd.set_start_card_id(startCard->getId());

        if (auto *targetCard = qgraphicsitem_cast<CardItem *>(targetItem)) {
            CardZoneLogic *targetZone = targetCard->getZone();
            cmd.set_target_player_id(targetZone->getPlayer()->getPlayerInfo()->getId());
            cmd.set_target_zone(targetZone->getName().toStdString());
            cmd.set_target_card_id(targetCard->getId());
        } else if (auto *targetPlayer = qgraphicsitem_cast<PlayerTarget *>(targetItem)) {
            cmd.set_target_player_id(targetPlayer->getOwner()->getPlayerInfo()->getId());
        } else {
            delArrow();
            return;
        }

        // if the card is in hand then we will move the card to stack or table as part of drawing the arrow
        if (startZone->getName() == ZoneNames::HAND) {
            startCard->playCard(false);
            CardInfoPtr ci = startCard->getCard().getCardPtr();
            bool playToStack = SettingsCache::instance().userInterface().getPlayToStack();
            if (ci && ((!playToStack && ci->getUiAttributes().tableRow == 3) ||
                       (playToStack && ci->getUiAttributes().tableRow != 0 &&
                        startCard->getZone()->getName() != ZoneNames::STACK))) {
                cmd.set_start_zone(ZoneNames::STACK);
            } else {
                cmd.set_start_zone(playToStack ? ZoneNames::STACK : ZoneNames::TABLE);
            }
        }

        if (deleteInPhase != 0) {
            cmd.set_delete_in_phase(deleteInPhase);
        }

        player->getPlayerActions()->sendGameCommand(cmd);
    }

    delArrow();
    for (auto *child : childArrows) {
        child->mouseReleaseEvent(event);
    }
}

// ArrowAttachItem
ArrowAttachItem::ArrowAttachItem(ArrowTarget *_startItem)
    : ArrowItem(
          QSharedPointer<ArrowData>::create(ArrowData{.creatorId = _startItem->getOwner()->getPlayerInfo()->getId(),
                                                      .isLocalCreator = true,
                                                      .id = -1,
                                                      .color = Qt::green}),
          _startItem,
          nullptr),
      player(_startItem->getOwner())
{
}

void ArrowAttachItem::addChildArrow(ArrowAttachItem *child)
{
    childArrows.append(child);
}

void ArrowAttachItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (targetLocked || !startItem) {
        return;
    }

    const QPointF endPos = event->scenePos();

    ArrowTarget *cursorItem = nullptr;
    qreal cursorItemZ = -1;
    for (auto *item : scene()->items(endPos)) {
        if (auto *card = qgraphicsitem_cast<CardItem *>(item)) {
            if (card->zValue() > cursorItemZ) {
                cursorItem = card;
                cursorItemZ = card->zValue();
            }
        }
    }

    if (cursorItem != targetItem) {
        if (targetItem) {
            disconnect(positionConnection);
            targetItem->setBeingPointedAt(false);
        }

        targetItem = cursorItem;
        fullColor = (cursorItem != nullptr);

        if (cursorItem && cursorItem != startItem) {
            cursorItem->setBeingPointedAt(true);
            positionConnection =
                connect(cursorItem, &ArrowTarget::scenePositionChanged, this, [this]() { updatePath(); });
        }
    }

    targetItem ? updatePath() : updatePath(endPos);
    update();

    for (auto *child : childArrows) {
        child->mouseMoveEvent(event);
    }
}

void ArrowAttachItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (!startItem) {
        // The source card was destroyed while the arrow was being drawn.
        // Clean up the arrow and its children instead of leaking them.
        delArrow();
        for (auto *child : childArrows) {
            child->mouseReleaseEvent(event);
        }
        return;
    }

    // Attaching could move startItem under the current cursor position, causing all children to retarget to it right
    // before they are processed. Prevent that.
    for (auto *child : childArrows) {
        child->setTargetLocked(true);
    }

    if (targetItem && targetItem != startItem) {
        auto *startCard = qgraphicsitem_cast<CardItem *>(startItem);
        auto *targetCard = qgraphicsitem_cast<CardItem *>(targetItem);
        if (startCard && targetCard) {
            attachCards(startCard, targetCard);
        }
    }

    delArrow();
    for (auto *child : childArrows) {
        child->mouseReleaseEvent(event);
    }
}

void ArrowAttachItem::attachCards(CardItem *startCard, const CardItem *targetCard)
{
    if (targetCard->getAttachedTo() || targetCard->getZone()->getName() != ZoneNames::TABLE) {
        return;
    }

    // move card onto table first if attaching from some other zone
    if (startCard->getZone()->getName() != ZoneNames::TABLE) {
        player->getPlayerActions()->playCardToTable(startCard, false);
    }

    Command_AttachCard cmd;
    cmd.set_start_zone(ZoneNames::TABLE);
    cmd.set_card_id(startCard->getId());
    cmd.set_target_player_id(targetCard->getZone()->getPlayer()->getPlayerInfo()->getId());
    cmd.set_target_zone(targetCard->getZone()->getName().toStdString());
    cmd.set_target_card_id(targetCard->getId());
    player->getPlayerActions()->sendGameCommand(cmd);
}