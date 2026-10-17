#include "hand_zone.h"

#include "../../client/settings/cache_settings.h"
#include "../../game/player/player_actions.h"
#include "../../game/player/player_info.h"
#include "../../game/player/player_logic.h"
#include "../../game/zones/card_zone_logic.h"
#include "../../game/zones/hand_zone_logic.h"
#include "../../interface/theme_manager.h"
#include "../board/card_drag_item.h"
#include "../board/card_item.h"
#include "../card_dimensions.h"

#include <QBrush>
#include <QList>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <libcockatrice/protocol/pb/command_move_card.pb.h>
#include <libcockatrice/settings/interface_settings.h>
#include <string>

class QGraphicsItem;

HandZone::HandZone(HandZoneLogic *_logic, int _zoneHeight, QGraphicsItem *parent)
    : SelectZone(_logic, parent), zoneHeight(_zoneHeight)
{
    connect(themeManager, &ThemeManager::themeChanged, this, &HandZone::updateBg);
    updateBg();
    setCacheMode(DeviceCoordinateCache);
}

void HandZone::updateBg()
{
    update();
}

void HandZone::handleDropEvent(const QList<CardDragItem *> &dragItems,
                               CardZoneLogic *startZone,
                               const QPoint &dropPoint)
{
    if (startZone == nullptr || startZone->getPlayer() == nullptr || dragItems.isEmpty()) {
        return;
    }

    QPoint point = dropPoint + scenePos().toPoint();
    int x = -1;
    if (SettingsCache::instance().userInterface().getHorizontalHand()) {
        for (x = 0; x < getLogic()->getCards().size(); x++) {
            if (point.x() < static_cast<CardItem *>(getLogic()->getCards().at(x))->scenePos().x()) {
                break;
            }
        }
    } else {
        bool sameZone = startZone == getLogic();
        x = calcDropIndexFromY(dropPoint.y(), !sameZone);
    }

    Command_MoveCard cmd;
    cmd.set_start_player_id(startZone->getPlayer()->getPlayerInfo()->getId());
    cmd.set_start_zone(startZone->getName().toStdString());
    cmd.set_target_player_id(getLogic()->getPlayer()->getPlayerInfo()->getId());
    cmd.set_target_zone(getLogic()->getName().toStdString());
    cmd.set_x(x);
    cmd.set_y(-1);

    for (int i = 0; i < dragItems.size(); ++i) {
        cmd.mutable_cards_to_move()->add_card()->set_card_id(dragItems[i]->getId());
    }

    getLogic()->getPlayer()->getPlayerActions()->sendGameCommand(cmd);
}

QRectF HandZone::boundingRect() const
{
    if (SettingsCache::instance().userInterface().getHorizontalHand()) {
        return QRectF(0, 0, width, CardDimensions::HEIGHT_F + 10);
    } else {
        return QRectF(0, 0, CardDimensions::WIDTH_F * 1.5, zoneHeight);
    }
}

void HandZone::paint(QPainter *painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * /*widget*/)
{
    QBrush brush = themeManager->getExtraBgBrush(ThemeManager::Hand, getLogic()->getPlayer()->getZoneId());
    painter->fillRect(boundingRect(), brush);
}


void HandZone::reorganizeCards()
{
    if (!getLogic()->getCards().isEmpty()) {
        const int cardCount = getLogic()->getCards().size();

        if (SettingsCache::instance().userInterface().getHorizontalHand()) {
            const bool leftJustified = SettingsCache::instance().userInterface().getLeftJustified();
            const qreal cardWidth = getLogic()->getCards().at(0)->boundingRect().width();
            const qreal availableWidth = boundingRect().width();
            const qreal xPadding = leftJustified ? cardWidth * 1.4 : 5.0;
            const qreal totalWidth = qMax(0.0, availableWidth - xPadding - (leftJustified ? 5.0 : xPadding));

            if (cardCount == 1) {
                CardItem *card = getLogic()->getCards().at(0);
                card->setHandFanAngle(0.0);

                const qreal xPosition = leftJustified ? xPadding : xPadding + (totalWidth - cardWidth) / 2.0;
                card->setPos(xPosition, 5);
                card->setRealZValue(0);
            } else {
                // Keep full spacing while the cards fit; smoothly tighten the fan as the hand grows.
                const qreal targetSpacing = cardWidth * 0.42;
                const qreal spacing = cardCount * cardWidth <= totalWidth
                                          ? qMin(cardWidth, targetSpacing)
                                          : qMax(cardWidth * 0.18, (totalWidth - cardWidth) / (cardCount - 1));

                const qreal occupiedWidth = cardWidth + spacing * (cardCount - 1);
                const qreal startX = leftJustified ? xPadding : xPadding + (totalWidth - occupiedWidth) / 2.0;

                for (int i = 0; i < cardCount; ++i) {
                    CardItem *card = getLogic()->getCards().at(i);

                    const qreal center = (cardCount - 1) / 2.0;
                    const qreal normalizedPosition = (i - center) / qMax(1.0, center);
                    const qreal fanAngle = normalizedPosition * 12.0;

                    card->setHandFanAngle(fanAngle);
                    card->setPos(startX + spacing * i, 5);
                    card->setHandBaseY(5);
                    card->setRealZValue(i);
                }
            }
        } else {
            // No fan rotation in vertical hand mode.
            for (CardItem *card : getLogic()->getCards()) {
                card->setHandFanAngle(0.0);
            }

            // No clip container: hand cards should always be visible to the player.
            const auto params = buildStackParams();
            layoutCardsVertically(params);
        }
    }

    update();
}


void HandZone::sortHand(const QList<CardList::SortOption> &options)
{
    if (getLogic()->getCards().isEmpty()) {
        return;
    }

    getLogic()->sortCards(options);
    reorganizeCards();
}

void HandZone::setWidth(qreal _width)
{
    if (SettingsCache::instance().userInterface().getHorizontalHand()) {
        prepareGeometryChange();
        width = _width;
        reorganizeCards();
    }
}

void HandZone::updateOrientation()
{
    prepareGeometryChange();
    reorganizeCards();
}
