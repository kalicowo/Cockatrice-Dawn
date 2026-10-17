#include "../../cockatrice/src/game_graphics/board/card_placement_effect.h"

#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QImage>
#include <QPointer>
#include <cstdio>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition) {
        qFatal("%s", message);
    }
}
QImage render(QGraphicsScene &scene)
{
    QImage image(320, 320, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter painter(&image);
    scene.render(&painter, QRectF(0, 0, 320, 320), QRectF(0, 0, 320, 320));
    return image;
}
void testIdentity()
{
    const auto neutral = CardHalo::colorsForIdentity(QString(), false);
    require(CardHalo::colorsForIdentity("WUBRG", true) == neutral, "Hidden identities must stay neutral");
    require(CardHalo::colorsForIdentity("C", false) == neutral, "Colorless identities must stay neutral");
    require(CardHalo::colorsForIdentity("wu", false).size() == 2, "Retain both identity colors");
    require(CardHalo::colorsForIdentity("WUBRG", false).size() == 5, "Retain all five identity colors");
}
void testSplash(qreal rotation, qreal scale, const QPointF &position)
{
    QGraphicsScene scene;
    auto *card = scene.addRect(QRectF(0, 0, 80, 110), QPen(Qt::NoPen), QBrush(Qt::white));
    card->setPos(position);
    card->setTransformOriginPoint(40, 55);
    card->setRotation(rotation);
    card->setScale(scale);
    card->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    const auto baseline = render(scene);
    const auto cardTransform = card->sceneTransform();
    auto *effect =
        new CardPlacementEffect(card->shape(), []() { return CardHalo::colorsForIdentity("R", false); }, card);
    QPointer<CardPlacementEffect> lifetime(effect);
    auto *animation = effect->findChild<QVariantAnimation *>();
    require(render(scene) == baseline, "Starting a splash must never move or duplicate the card");
    const QRectF bounds = effect->boundingRect();
    for (int time : {50, 130, 250, 380}) {
        animation->setCurrentTime(time);
        const auto frame = render(scene);
        if (time == 130 && rotation == 0 && scale == 1 && position == QPointF(100, 80)) {
            require(frame.pixelColor(183, 135).red() > 15,
                    "Diffuse color must fill the space between the card edge and the expanding ring");
        }
        require(card->sceneTransform() == cardTransform, "Splash must never modify the card transform");
        require(effect->sceneTransform() == cardTransform, "Splash must share the real card coordinates");
        require(effect->boundingRect() == bounds, "Repaint bounds must remain fixed throughout the splash");
        require(effect->shape().isEmpty(), "Splash must not intercept interaction");
        int colored = 0;
        for (int y = 0; y < 320; ++y) {
            for (int x = 0; x < 320; ++x) {
                if (baseline.pixelColor(x, y) == QColor(Qt::white)) {
                    require(frame.pixelColor(x, y) == QColor(Qt::white), "Card pixels must stay aligned and untouched");
                }
                const auto pixel = frame.pixelColor(x, y);
                if (pixel.red() > pixel.green() + 5) {
                    ++colored;
                }
            }
        }
        require(colored > 10, "A clearly visible identity-colored splash must extend outside the card");
    }
    animation->setCurrentTime(420);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    require(lifetime.isNull(), "Splash must clean up after completion");
    require(render(scene) == baseline, "No residual halo or shifted image may remain after the splash");
}
void testInterruption()
{
    QGraphicsScene scene;
    for (int i = 0; i < 3; ++i) {
        auto *card = scene.addRect(QRectF(0, 0, 30, 40));
        card->setPos(50 * i, 80);
        auto *effect =
            new CardPlacementEffect(card->shape(), []() { return CardHalo::colorsForIdentity("UG", false); }, card);
        QPointer<CardPlacementEffect> lifetime(effect);
        effect->findChild<QVariantAnimation *>()->setCurrentTime(100);
        if (i == 0) {
            delete effect;
            require(lifetime.isNull(), "Cancellation must remove the splash");
        } else {
            delete card;
            require(lifetime.isNull(), "Deleting a card must remove its splash");
        }
    }
    scene.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void testVisibilityChange()
{
    QGraphicsScene scene;
    auto *card = scene.addRect(QRectF(0, 0, 80, 110), QPen(Qt::NoPen), QBrush(Qt::white));
    card->setPos(100, 80);
    bool hidden = false;
    auto *effect =
        new CardPlacementEffect(card->shape(), [&hidden]() { return CardHalo::colorsForIdentity("R", hidden); }, card);
    effect->findChild<QVariantAnimation *>()->setCurrentTime(50);
    hidden = true;
    effect->update();
    const auto frame = render(scene);
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const auto pixel = frame.pixelColor(x, y);
            require(pixel.red() <= pixel.blue(), "A card hidden during the splash must immediately use neutral colors");
        }
    }
}
} // namespace
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    testIdentity();
    testSplash(0, 1, QPointF(100, 80));
    testSplash(90, 1.25, QPointF(100, 80));
    testSplash(0, 0.65, QPointF(-10, 150));
    testSplash(180, 0.8, QPointF(240, 20));
    testInterruption();
    testVisibilityChange();
    std::puts("PASS: identity splash, alignment, edge clipping, transforms, cleanup and cancellation");
    return 0;
}
