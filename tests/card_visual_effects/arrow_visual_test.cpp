#include "../../cockatrice/src/game_graphics/board/arrow_visual.h"
#include "../../cockatrice/src/game_graphics/board/card_halo.h"

#include <QApplication>
#include <QImage>

static QImage render(const QList<QColor> &colors, qreal seconds, qreal progress = 1)
{
    QImage image(340, 180, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(20, 40);
    QPainterPath curve;
    curve.moveTo(0, 0);
    curve.quadTo(150, 66, 300, 0);
    QPainterPath head;
    head.moveTo(300, 0);
    head.lineTo(289, -1);
    head.lineTo(291, 7);
    head.closeSubpath();
    const auto hit = ArrowVisual::hitShape(curve, head);
    if (!hit.contains(curve.pointAtPercent(0.5) + QPointF(0, 5))) {
        qFatal("The thin arrow must retain a usable click area");
    }
    ArrowVisual::paint(&painter, curve, head, colors, progress, seconds, true);
    return image;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto red = CardHalo::colorsForIdentity("R", false);
    const QImage initial = render(red, 1);
    if (initial == render(red, 9.4)) {
        qFatal("Arrow particles must keep moving long after the reveal");
    }
    if (initial == render(CardHalo::colorsForIdentity("U", false), 1)) {
        qFatal("Source color must affect the arrow");
    }
    if (render(CardHalo::colorsForIdentity("R", true), 1) != render(CardHalo::colorsForIdentity("U", true), 1)) {
        qFatal("Face-down arrows must not reveal source identity");
    }
    const QImage mixed = render(CardHalo::colorsForIdentity("UR", false), 1);
    bool blue = false;
    bool redPixel = false;
    for (int y = 0; y < mixed.height(); ++y) {
        for (int x = 0; x < mixed.width(); ++x) {
            const QColor color = mixed.pixelColor(x, y);
            if (color.alpha() > 80) {
                blue |= color.blue() > color.red() + 40;
                redPixel |= color.red() > color.blue() + 40;
            }
            if (initial.pixelColor(x, y).alpha() && (x < 8 || x > 332 || y < 28 || y > 118)) {
                qFatal("Particles must remain inside the declared arrow margin");
            }
        }
    }
    if (!blue || !redPixel) {
        qFatal("Multicolor sources must retain both arrow colors");
    }
    const auto hidden = render(red, 1, 0);
    QImage empty(hidden.size(), hidden.format());
    empty.fill(Qt::transparent);
    if (hidden != empty) {
        qFatal("An unrevealed arrow must be transparent");
    }
    if (argc > 1) {
        initial.save(QString::fromLocal8Bit(argv[1]));
    }
    return 0;
}
