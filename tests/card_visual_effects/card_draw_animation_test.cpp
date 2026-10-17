#include "../../cockatrice/src/game_graphics/board/card_draw_animation.h"

#include <QApplication>
#include <QImage>
#include <cstdio>

class TestCard : public QGraphicsObject
{
public:
    QRectF boundingRect() const override
    {
        return QRectF(0, 0, 72, 102);
    }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->fillRect(boundingRect(), Qt::white);
    }
};

void require(bool condition, const char *message)
{
    if (!condition) {
        qFatal("%s", message);
    }
}

CardDrawAnimation::Request request(TestCard *card, bool fast, const std::function<bool()> &eligible)
{
    CardDrawAnimation::Request result;
    result.card = card;
    result.libraryCenter = QPointF(80, 150);
    result.mayReveal = eligible;
    result.directToHand = fast;
    result.picture = [](bool front) {
        QPixmap image(72, 102);
        image.fill(front ? Qt::red : Qt::blue);
        return image;
    };
    return result;
}

QColor sample(QGraphicsScene &scene, const QPointF &position)
{
    QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    scene.render(&painter, QRectF(0, 0, 800, 600), QRectF(0, 0, 800, 600));
    return image.pixelColor(position.toPoint());
}

void testReadingSequence()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 800, 600);
    auto *card = new TestCard;
    scene.addItem(card);
    card->setPos(100, 480);
    auto *overlay = CardDrawAnimation::enqueue(&scene, request(card, false, []() { return true; }));
    auto *clock = overlay->findChild<QVariantAnimation *>();
    require(card->opacity() == 0, "The hand must not show a duplicate during presentation");
    require(overlay->pos() == QPointF(80, 150), "The back must start at the library");
    require(sample(scene, overlay->pos()) == QColor(Qt::blue), "The drawn card must initially be face down");
    clock->setCurrentTime(CardDrawAnimation::TRAVEL_MS);
    require(sample(scene, overlay->pos()) == QColor(Qt::blue), "Travel must not reveal the card face");
    const int readingStart = CardDrawAnimation::TRAVEL_MS + CardDrawAnimation::FLIP_MS;
    clock->setCurrentTime(readingStart);
    const QPointF focus = overlay->pos();
    const qreal expectedScale = 0.8 * qMin(800.0 * 0.34 / 72.0, 600.0 * 0.55 / 102.0);
    require(qAbs(overlay->scale() - expectedScale) < 0.001, "Foreground cards must be 20 percent smaller");
    require(sample(scene, focus) == QColor(Qt::red), "The front must be visible after the flip");
    clock->setCurrentTime(readingStart + CardDrawAnimation::READ_MS - 1);
    require(overlay->pos() == focus, "The reading pause must last the full 2.2 seconds");
    clock->setCurrentTime(readingStart + CardDrawAnimation::READ_MS + 100);
    require(overlay->pos() != focus, "The card must slide towards the hand after reading");
    card->setPos(200, 480);
    clock->setCurrentTime(CardDrawAnimation::TOTAL_MS - 1);
    require(QLineF(overlay->pos(), card->mapToScene(card->boundingRect().center())).length() < 1,
            "The destination must track hand layout changes");
    clock->setCurrentTime(CardDrawAnimation::TOTAL_MS);
    require(card->opacity() == 1 && !overlay->isVisible(), "Finish must restore the real hand card");
}

void testBulkAndPrivacy()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 800, 600);
    QList<CardDrawAnimation *> overlays;
    QList<TestCard *> cards;
    for (int i = 0; i < 4; ++i) {
        auto *card = new TestCard;
        scene.addItem(card);
        card->setPos(150 + i * 60, 480);
        cards.append(card);
        overlays.append(CardDrawAnimation::enqueue(&scene, request(card, true, []() { return true; })));
    }
    require(overlays[0] != overlays[1], "Bulk draws must travel together instead of queueing reading pauses");
    for (int i = 0; i < overlays.size(); ++i) {
        auto *clock = overlays[i]->findChild<QVariantAnimation *>();
        require(clock->duration() == CardDrawAnimation::FAST_MS, "Bulk draws must have no reading delay");
        clock->setCurrentTime(100);
        require(sample(scene, overlays[i]->pos()) == QColor(Qt::blue), "Bulk cards must start face down");
        clock->setCurrentTime(550);
        require(sample(scene, overlays[i]->pos()) == QColor(Qt::red), "Bulk cards must turn over in flight");
        clock->setCurrentTime(CardDrawAnimation::FAST_MS);
        require(cards[i]->opacity() == 1, "Bulk finish must restore every card");
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    auto *privateCard = new TestCard;
    scene.addItem(privateCard);
    require(!CardDrawAnimation::enqueue(&scene, request(privateCard, false, []() { return false; })),
            "Unauthorized cards must never start a reveal animation");
    require(privateCard->opacity() == 1, "Rejected requests must not hide hand cards");
    bool local = true;
    auto *overlay = CardDrawAnimation::enqueue(&scene, request(privateCard, false, [&local]() { return local; }));
    local = false;
    overlay->findChild<QVariantAnimation *>()->setCurrentTime(100);
    require(privateCard->opacity() == 1 && !overlay->isVisible(), "Ownership loss must cancel the presentation");
}

void testQueueAndCleanup()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 800, 600);
    auto *first = new TestCard;
    auto *second = new TestCard;
    scene.addItem(first);
    scene.addItem(second);
    auto *queue = CardDrawAnimation::enqueue(&scene, request(first, false, []() { return true; }));
    require(CardDrawAnimation::enqueue(&scene, request(second, false, []() { return true; })) == queue,
            "Small draws must share one sequential reading queue");
    queue->findChild<QVariantAnimation *>()->setCurrentTime(CardDrawAnimation::TOTAL_MS);
    require(first->opacity() == 1 && second->opacity() == 0, "Next card must wait until the first is finished");
    require(queue->findChild<QVariantAnimation *>()->state() == QAbstractAnimation::Running,
            "The next queued card must start its own live timeline");
    delete queue;
    require(second->opacity() == 1, "Closing the presentation must restore pending cards");
    queue = CardDrawAnimation::enqueue(&scene, request(second, false, []() { return true; }));
    CardDrawAnimation::enqueue(&scene, request(second, false, []() { return true; }));
    queue->findChild<QVariantAnimation *>()->setCurrentTime(CardDrawAnimation::TOTAL_MS);
    require(second->opacity() == 1, "Undo/redraw must not leave a reused card permanently transparent");
    CardDrawAnimation::enqueue(&scene, request(second, false, []() { return true; }));
    scene.clear(); // Destruction order must be safe with live animation callbacks.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void testReadingAuraAndHiddenLibrary()
{
    require(CardDrawAnimation::READ_MS == 2200, "Reading must last exactly 2.2 seconds");
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 800, 600);
    auto *card = new TestCard;
    scene.addItem(card);
    card->setPos(100, 480);
    int auraCalls = 0;
    auto draw = request(card, false, []() { return true; });
    draw.readingAura = [&auraCalls]() {
        ++auraCalls;
        return CardHalo::colorsForIdentity("R", false);
    };
    auto *overlay = CardDrawAnimation::enqueue(&scene, draw);
    auto *clock = overlay->findChild<QVariantAnimation *>();
    sample(scene, overlay->pos());
    require(sample(scene, overlay->pos()) == QColor(Qt::blue), "A prepared aura must not reveal a face-down card");
    require(auraCalls == 1, "The aura must be prepared once before the flip");
    clock->setCurrentTime(CardDrawAnimation::TRAVEL_MS + CardDrawAnimation::FLIP_MS);
    require(sample(scene, overlay->mapToScene(QPointF(38, 0))).red() > 0,
            "Reading must paint a colored aura outside the card");
    const int nearGlow = sample(scene, overlay->mapToScene(QPointF(39, 0))).red();
    const int farGlow = sample(scene, overlay->mapToScene(QPointF(44, 0))).red();
    require(farGlow > 0 && farGlow < nearGlow, "The reading aura must extend broadly and fade progressively");
    require(auraCalls == 1, "Repeated frames must reuse the aura image instead of regenerating gradients");
    clock->setCurrentTime(CardDrawAnimation::TOTAL_MS);
    require(card->opacity() == 1 && !overlay->isVisible(), "The aura must disappear when the card arrives in hand");

    auto hidden = request(card, true, []() { return true; });
    hidden.startsFaceUp = false;
    hidden.endsFaceUp = false;
    hidden.isFaceVisible = []() { return false; };
    auto *transfer = CardDrawAnimation::enqueue(&scene, hidden);
    auto *transferClock = transfer->findChild<QVariantAnimation *>();
    for (int time : {0, 100, 325, 550}) {
        transferClock->setCurrentTime(time);
        require(sample(scene, transfer->pos()) == QColor(Qt::blue),
                "Library to face-down exile must preserve the card back throughout the flight");
    }
    transferClock->setCurrentTime(CardDrawAnimation::FAST_MS);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void testRoundedAnimatedCards()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 800, 600);
    auto *card = new TestCard;
    scene.addItem(card);
    auto *overlay = CardDrawAnimation::enqueue(&scene, request(card, false, []() { return true; }));
    auto *clock = overlay->findChild<QVariantAnimation *>();
    const QPointF nearCorner = overlay->cardRect().topLeft() + QPointF(0.6, 0.6);
    clock->setCurrentTime(CardDrawAnimation::TRAVEL_MS);
    require(sample(scene, overlay->pos()) == QColor(Qt::blue), "The back must remain visible before flipping");
    require(sample(scene, overlay->mapToScene(nearCorner)) != QColor(Qt::blue),
            "The animated back must have rounded corners");
    clock->setCurrentTime(CardDrawAnimation::TRAVEL_MS + CardDrawAnimation::FLIP_MS);
    require(sample(scene, overlay->pos()) == QColor(Qt::red), "The front must remain visible after flipping");
    require(sample(scene, overlay->mapToScene(nearCorner)) != QColor(Qt::red),
            "The revealed face must retain rounded corners");
    clock->setCurrentTime(CardDrawAnimation::TOTAL_MS);

    auto square = request(card, false, []() { return true; });
    square.roundCorners = false;
    overlay = CardDrawAnimation::enqueue(&scene, square);
    overlay->findChild<QVariantAnimation *>()->setCurrentTime(CardDrawAnimation::TRAVEL_MS +
                                                              CardDrawAnimation::FLIP_MS);
    require(sample(scene, overlay->mapToScene(nearCorner)) == QColor(Qt::red),
            "Disabling rounded corners must remain supported");
}

void testBoardToPile()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 800, 600);
    auto *card = new TestCard;
    scene.addItem(card);
    card->setPos(600, 400);
    card->setRotation(90);
    card->setVisible(false); // Piles paint their top card themselves.
    auto transfer = request(card, true, []() { return true; });
    transfer.startsFaceUp = true;
    transfer.endsFaceUp = true;
    transfer.libraryCenter = QPointF(180, 200);
    transfer.sourceAngle = 90;
    bool publicFace = true;
    int pileUpdates = 0;
    transfer.isFaceVisible = [&publicFace]() { return publicFace; };
    transfer.refreshDestination = [&pileUpdates]() { ++pileUpdates; };
    auto *overlay = CardDrawAnimation::enqueue(&scene, std::move(transfer));
    auto *clock = overlay->findChild<QVariantAnimation *>();
    require(overlay->pos() == QPointF(180, 200), "Transfer must start at the card's former board position");
    require(overlay->rotation() == 90, "A tapped card must retain its initial orientation");
    require(pileUpdates == 1 && card->opacity() == 0, "The destination pile must hide its in-flight top card");
    for (int time : {0, 100, 325, 550}) {
        clock->setCurrentTime(time);
        require(sample(scene, overlay->pos()) == QColor(Qt::red),
                "A public board card must remain face up throughout the transfer without flipping");
    }
    publicFace = false;
    require(sample(scene, overlay->pos()) == QColor(Qt::blue), "Face-down exile must never expose the card face");
    clock->setCurrentTime(CardDrawAnimation::FAST_MS - 1);
    require(QLineF(overlay->pos(), card->mapToScene(card->boundingRect().center())).length() < 1,
            "Transfer must end at the destination pile, including its rotation");
    clock->setCurrentTime(CardDrawAnimation::FAST_MS);
    require(pileUpdates == 2 && card->opacity() == 1, "Landing must refresh the destination's top picture");
    require(!card->isVisible(), "Transfer must preserve the pile's own card visibility policy");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void testSideBySide(int count, qreal viewportWidth)
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, viewportWidth, 600);
    QList<CardDrawAnimation *> overlays;
    QList<TestCard *> cards;
    const int readingStart = CardDrawAnimation::TRAVEL_MS + CardDrawAnimation::FLIP_MS;
    QRectF groupBounds;
    for (int i = 0; i < count; ++i) {
        auto *card = new TestCard;
        scene.addItem(card);
        card->setPos(30 + i * 40, 480);
        cards.append(card);
        auto draw = request(card, false, []() { return true; });
        draw.groupSize = count;
        draw.groupIndex = i;
        auto *overlay = CardDrawAnimation::enqueue(&scene, std::move(draw));
        overlays.append(overlay);
        auto *clock = overlay->findChild<QVariantAnimation *>();
        clock->setCurrentTime(readingStart);
        const auto rect = overlay->mapRectToScene(overlay->cardRect());
        if (i > 0) {
            require(overlays[i - 1] != overlay, "Grouped cards must animate simultaneously");
            require(overlays[i - 1]->mapRectToScene(overlays[i - 1]->cardRect()).right() < rect.left(),
                    "Cards presented together must not overlap");
        }
        groupBounds = groupBounds.united(rect);
        const QPointF focus = overlay->pos();
        clock->setCurrentTime(readingStart + CardDrawAnimation::READ_MS - 1);
        require(overlay->pos() == focus, "Every grouped card must retain the full reading pause");
    }
    require(qAbs(groupBounds.center().x() - viewportWidth / 2) < 0.001,
            "The entire group must be centered in the viewport");
    require(groupBounds.left() >= 0 && groupBounds.right() <= viewportWidth,
            "All grouped cards must fit in narrow viewports");
    for (auto *overlay : overlays) {
        overlay->findChild<QVariantAnimation *>()->setCurrentTime(CardDrawAnimation::TOTAL_MS);
    }
    for (auto *card : cards) {
        require(card->opacity() == 1, "All grouped cards must be restored to the hand together");
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    testReadingSequence();
    testBulkAndPrivacy();
    testQueueAndCleanup();
    testBoardToPile();
    testRoundedAnimatedCards();
    testReadingAuraAndHiddenLibrary();
    testSideBySide(2, 800);
    testSideBySide(3, 800);
    testSideBySide(3, 360);
    std::puts("PASS: draw flip, 2.2s reading, bulk shortcut, privacy, layout and queue cleanup");
}
