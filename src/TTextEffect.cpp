/***************************************************************************
 *   Copyright (C) 2026 by the Mudlet developers                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "TTextEffect.h"

#include "utils.h"

#include <QCache>
#include <QElapsedTimer>
#include <QImage>
#include <QLinearGradient>
#include <QPainterPath>
#include <QLineF>
#include <QPainter>
#include <QPolygonF>
#include <QRadialGradient>
#include <QString>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

static_assert(TTextEffect::KindCount <= 32, "TChar::EffectMask holds the kind in five bits");

namespace {
quint32 mix(quint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Uniform in [0, 1)
qreal random01(const quint32 a, const quint32 b, const quint32 c = 0, const quint32 d = 0)
{
    return mix(a ^ mix(b ^ mix(c ^ mix(d)))) / 4294967296.0;
}

void glow(QPainter& painter, const TTextEffect::Span& text, const QColor& color, qreal radius, qreal strength);
} // namespace

qreal TTextEffect::seconds()
{
    static QElapsedTimer clock;
    if (!clock.isValid()) {
        clock.start();
    }
    return clock.nsecsElapsed() / 1e9;
}

// Each mote is a pure function of its slot and the time rather than state kept
// between frames, so a partial repaint, a scroll or a second view of the same
// line all agree on where it is.
void TTextEffect::paintLifedrain(QPainter& painter, const Span& text, const qreal seconds)
{
    const QRect& span = text.rect;
    const QColor& color = text.color;
    const quint32 seed = text.seed;
    const qreal h = span.height();
    if (h <= 0 || span.width() <= 0) {
        return;
    }
    const int moteCount = std::max(2, qRound(span.width() / (h * 0.45)));
    const QColor glow = color.lighter(140);
    const QColor core = QColor::fromRgbF((glow.redF() + 2.0) / 3.0, (glow.greenF() + 2.0) / 3.0, (glow.blueF() + 2.0) / 3.0);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    for (int slot = 0; slot < moteCount; ++slot) {
        const qreal lifetime = 1.4 + 0.9 * random01(seed, slot, 1);
        const qreal period = lifetime + 0.2 + 1.2 * random01(seed, slot, 2);
        const qreal local = seconds + period * random01(seed, slot, 3);
        const auto cycle = static_cast<quint32>(std::floor(local / period));
        const qreal age = local - cycle * period;
        if (age >= lifetime) {
            continue;
        }
        const qreal f = age / lifetime;
        const qreal r1 = random01(seed, slot, cycle, 4);
        const qreal r2 = random01(seed, slot, cycle, 5);
        const qreal r3 = random01(seed, slot, cycle, 6);
        const qreal r4 = random01(seed, slot, cycle, 7);

        // Slow to leave the letter, then pulled away ever faster
        const qreal pull = std::pow(f, 1.7);
        const qreal x = span.left() + r1 * span.width() + (0.5 + 1.7 * r3) * h * pull + std::sin(age * 5.0 + r1 * 6.2832) * 0.15 * h * f;
        const qreal y = span.top() + h * (0.3 + 0.45 * r2) - (1.2 + 2.0 * r4) * h * pull;
        const qreal alpha = std::min(1.0, f / 0.15) * std::pow(1.0 - f, 1.2);
        const qreal radius = h * (0.07 + 0.05 * r2) * (1.0 - 0.5 * f);

        QRadialGradient halo(QPointF(x, y), radius * 3.0);
        QColor inner = glow;
        inner.setAlphaF(0.55 * alpha);
        QColor outer = glow;
        outer.setAlphaF(0.0);
        halo.setColorAt(0.0, inner);
        halo.setColorAt(1.0, outer);
        painter.setBrush(halo);
        painter.drawEllipse(QPointF(x, y), radius * 3.0, radius * 3.0);

        QColor dot = core;
        dot.setAlphaF(alpha);
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(x, y), radius, radius);
    }
    painter.restore();
}

namespace {
// A jagged bolt from a to b by midpoint displacement, each level kinking it
// less than the one before, as forked lightning does
QPolygonF bolt(const QPointF& a, const QPointF& b, const quint32 seed, const quint32 salt, const qreal wildness = 0.3, const quint32 levels = 3)
{
    QPolygonF points{a, b};
    qreal amplitude = wildness * QLineF(a, b).length();
    for (quint32 level = 0; level < levels; ++level) {
        QPolygonF finer;
        for (qsizetype i = 0; i + 1 < points.size(); ++i) {
            const QPointF& p = points.at(i);
            const QPointF& q = points.at(i + 1);
            const QPointF along = q - p;
            const qreal length = std::hypot(along.x(), along.y());
            const QPointF normal = length > 0 ? QPointF(-along.y() / length, along.x() / length) : QPointF();
            const qreal offset = (random01(seed, salt, level * 16 + i, 21) - 0.5) * 2.0 * amplitude;
            finer << p << (p + q) / 2.0 + normal * offset;
        }
        finer << points.last();
        points = finer;
        amplitude *= 0.55;
    }
    return points;
}
} // namespace

// Strands are redrawn elsewhere on every tick of a coarse clock, which is what
// makes them crackle rather than glide
void TTextEffect::paintLightning(QPainter& painter, const Span& text, const qreal seconds)
{
    const QRect& span = text.rect;
    const QColor& color = text.color;
    const quint32 seed = text.seed;
    const qreal h = span.height();
    const qreal w = span.width();
    if (h <= 0 || w <= 0) {
        return;
    }
    constexpr qreal tickLength = 0.065;
    const auto tick = static_cast<quint32>(seconds / tickLength);
    const QColor hot = QColor::fromRgbF((color.redF() + 6.0) / 8.0, (color.greenF() + 6.0) / 8.0, (color.blueF() + 5.0) / 8.0);
    auto withAlpha = [](QColor c, const qreal alpha) {
        c.setAlphaF(std::clamp(alpha, 0.0, 1.0));
        return c;
    };
    auto pen = [](const QColor& c, const qreal width) {
        return QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Charged letters, faintly aglow between strikes
    glow(painter, text, color, h * 0.2, 0.45);

    // Every tick a letter or two is scored through with short jagged strokes,
    // some in the text's own color and some cut dark, so it looks to short out
    const int letters = std::max(1, qRound(w / (h * 0.46)));
    const qreal cell = w / letters;
    const int scores = 2 + static_cast<int>(random01(seed, tick, 50) * std::min(3, letters));
    for (int i = 0; i < scores; ++i) {
        const int letter = static_cast<int>(random01(seed, tick, i, 51) * letters);
        const QPointF a(span.left() + cell * (letter + 0.1 * random01(seed, tick, i, 52)), span.top() + h * (0.3 + 0.45 * random01(seed, tick, i, 53)));
        const QPointF b = a + QPointF(cell * (0.6 + 0.6 * random01(seed, tick, i, 54)), (random01(seed, tick, i, 55) - 0.5) * h * 0.35);
        const bool whiteHot = random01(seed, tick, i, 56) < 0.4;
        painter.setPen(pen(whiteHot ? hot : color, std::max(1.5, h * 0.11)));
        painter.drawPolyline(bolt(a, b, seed ^ tick, 200 + i, 0.25));
    }

    // A strike about every 0.7s, from above and off to one side down onto a letter
    constexpr qreal strikeInterval = 0.7;
    constexpr qreal strikeLength = 0.3;
    const qreal local = seconds + strikeInterval * random01(seed, 31);
    const auto strike = static_cast<quint32>(local / strikeInterval);
    const qreal sinceStrike = local - strike * strikeInterval - 0.12 * random01(seed, strike, 32);
    if (sinceStrike >= 0.0 && sinceStrike < strikeLength) {
        // Full for its first two ticks, then dying away
        const qreal strength = sinceStrike < 0.13 ? 1.0 : std::pow(1.0 - (sinceStrike - 0.13) / (strikeLength - 0.13), 1.3);
        // Adding light rather than covering, so what is beneath glows instead of being hidden
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        const int boltCount = random01(seed, strike, 43) < 0.15 ? 2 : 1;
        for (int boltIndex = 0; boltIndex < boltCount; ++boltIndex) {
            const quint32 key = strike * 2 + boltIndex;
            const qreal targetX = span.left() + w * (0.1 + 0.8 * random01(seed, key, 33));
            const QPointF target(targetX, span.top() + h * (0.4 + 0.3 * random01(seed, key, 34)));
            const qreal lean = (random01(seed, key, 35) < 0.5 ? -1.0 : 1.0) * h * (1.0 + 2.0 * random01(seed, key, 42));
            const QPointF origin(targetX + lean, span.top() - h * (1.5 + 0.7 * random01(seed, key, 36)));

            const QRectF reachRect = QRectF(origin, target).normalized().adjusted(-h * 1.2, -h * 0.6, h * 1.2, h * 0.9);
            QRadialGradient haze(reachRect.center(), std::max(reachRect.width(), reachRect.height()) * 0.6);
            haze.setColorAt(0.0, withAlpha(color, 0.16 * strength));
            haze.setColorAt(0.7, withAlpha(color, 0.07 * strength));
            haze.setColorAt(1.0, withAlpha(color, 0.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(haze);
            painter.drawEllipse(reachRect.center(), reachRect.width() * 0.75, reachRect.height() * 0.75);

            // Two strands between the same ends, finely kinked, with short forks off them
            for (int strand = 0; strand < 2; ++strand) {
                const QPointF drift((random01(seed, tick, strand, 37) - 0.5) * h * 0.5, 0.0);
                const QPolygonF path = bolt(origin + drift, target, seed ^ tick ^ key, 400 + strand, 0.14, 5);
                painter.setPen(pen(withAlpha(color, 0.1 * strength), h * 0.2));
                painter.drawPolyline(path);
                painter.setPen(pen(withAlpha(hot, (strand == 0 ? 0.95 : 0.35) * strength), std::max(1.0, h * 0.05)));
                painter.drawPolyline(path);
                for (int fork = 0; fork < 2; ++fork) {
                    if (random01(seed, tick, strand * 4 + fork, 38) < 0.4) {
                        continue;
                    }
                    const QPointF from = path.at(2 + static_cast<int>(random01(seed, tick, strand * 4 + fork, 39) * (path.size() - 4)));
                    const qreal side = random01(seed, tick, strand * 4 + fork, 40) < 0.5 ? -1.0 : 1.0;
                    const QPointF to = from + QPointF(side * h * (0.4 + 0.6 * random01(seed, tick, strand * 4 + fork, 41)), h * (0.2 + 0.5 * random01(seed, tick, strand * 4 + fork, 45)));
                    painter.setPen(pen(withAlpha(hot, 0.5 * strength), std::max(1.0, h * 0.035)));
                    painter.drawPolyline(bolt(from, to, seed ^ tick ^ key, 500 + strand * 4 + fork, 0.2, 3));
                }
            }
            QRadialGradient impact(target, h * 1.1);
            impact.setColorAt(0.0, withAlpha(hot, 0.35 * strength));
            impact.setColorAt(1.0, withAlpha(color, 0.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(impact);
            painter.drawEllipse(target, h * 1.1, h * 1.1);
        }
    }
    painter.restore();
}

namespace {
using Span = TTextEffect::Span;
constexpr qreal tau = 6.283185307179586;

QColor withAlpha(QColor color, const qreal alpha)
{
    color.setAlphaF(std::clamp(alpha, 0.0, 1.0));
    return color;
}

QColor blend(const QColor& from, const QColor& to, const qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount, from.greenF() + (to.greenF() - from.greenF()) * amount, from.blueF() + (to.blueF() - from.blueF()) * amount);
}

QPen roundPen(const QColor& color, const qreal width)
{
    return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// Smooth, unrepeating drift in about [-1, 1]
qreal drift(const quint32 seed, const quint32 salt, const qreal t)
{
    return 0.6 * std::sin(t * (1.1 + random01(seed, salt, 1)) + tau * random01(seed, salt, 2)) + 0.4 * std::sin(t * (2.3 + random01(seed, salt, 3)) + tau * random01(seed, salt, 4));
}

// Where one slot of an endless stream of particles is in its life: each lives
// `lifetime` seconds, then rests a while before the slot is reused, so that
// every particle is a pure function of the slot and the time.
struct Life
{
    quint32 generation;
    qreal age;
    qreal fraction;
    bool alive;
};

Life lifeOf(const quint32 seed, const quint32 slot, const qreal t, const qreal lifetime, const qreal rest)
{
    const qreal period = lifetime + rest * (0.5 + random01(seed, slot, 90));
    const qreal local = t + period * random01(seed, slot, 91);
    const auto generation = static_cast<quint32>(std::floor(local / period));
    const qreal age = local - generation * period;
    return {generation, age, age / lifetime, age < lifetime};
}

// Running-sum box blur along every row or every column; three of these each
// way come close enough to a gaussian
void boxBlur(QImage& image, const int radius, const bool horizontal)
{
    const int lines = horizontal ? image.height() : image.width();
    const int length = horizontal ? image.width() : image.height();
    const int window = 2 * radius + 1;
    std::vector<QRgb> source(length);
    for (int line = 0; line < lines; ++line) {
        auto at = [&](const int i) -> QRgb& {
            return horizontal ? reinterpret_cast<QRgb*>(image.scanLine(line))[i] : reinterpret_cast<QRgb*>(image.scanLine(i))[line];
        };
        for (int i = 0; i < length; ++i) {
            source[i] = at(i);
        }
        int sa = 0, sr = 0, sg = 0, sb = 0;
        for (int i = -radius; i <= radius; ++i) {
            const QRgb pixel = source[std::clamp(i, 0, length - 1)];
            sa += qAlpha(pixel);
            sr += qRed(pixel);
            sg += qGreen(pixel);
            sb += qBlue(pixel);
        }
        for (int i = 0; i < length; ++i) {
            at(i) = qRgba(sr / window, sg / window, sb / window, sa / window);
            const QRgb leaving = source[std::clamp(i - radius, 0, length - 1)];
            const QRgb entering = source[std::clamp(i + radius + 1, 0, length - 1)];
            sa += qAlpha(entering) - qAlpha(leaving);
            sr += qRed(entering) - qRed(leaving);
            sg += qGreen(entering) - qGreen(leaving);
            sb += qBlue(entering) - qBlue(leaving);
        }
    }
}

// A halo hugging the letters' own shapes: the text drawn and blurred. Text on
// screen keeps its halo from frame to frame, so each is only blurred once.
QImage glowImage(const Span& text, const QColor& color, const int radius, QPoint* origin)
{
    static QCache<QString, QImage> cache(16 * 1024 * 1024);
    const int margin = radius * 2;
    *origin = text.rect.topLeft() - QPoint(margin, margin);
    QString key = text.font.key() + color.name(QColor::HexArgb) + QString::number(radius);
    for (const auto& letter : text.letters) {
        key += letter.text + QString::number(letter.rect.x() - text.rect.x()) + QLatin1Char(',') + QString::number(letter.rect.width());
    }
    if (const QImage* cached = cache.object(key)) {
        return *cached;
    }
    QImage image(text.rect.size() + QSize(2 * margin, 2 * margin), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setFont(text.font);
    painter.setPen(color);
    for (const auto& letter : text.letters) {
        painter.drawText(letter.rect.translated(-*origin), Qt::AlignCenter, letter.text);
    }
    painter.end();
    const int pass = std::max(1, radius / 2);
    for (int i = 0; i < 3; ++i) {
        boxBlur(image, pass, true);
        boxBlur(image, pass, false);
    }
    cache.insert(key, new QImage(image), static_cast<int>(image.sizeInBytes()));
    return image;
}

void glow(QPainter& painter, const Span& text, const QColor& color, const qreal radius, const qreal strength)
{
    if (strength <= 0.0) {
        return;
    }
    QPoint origin;
    const QImage image = glowImage(text, color, std::max(1, qRound(radius)), &origin);
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    // Past 1 the halo is laid down again, thickening it
    for (qreal left = strength; left > 0.0; left -= 1.0) {
        painter.setOpacity(std::min(1.0, left));
        painter.drawImage(origin, image);
    }
    painter.restore();
}

// Draws the letters again in `brush`, each moved by its entry in `offsets` if
// there is one. Over an opaque background each is erased first, so that a moved
// letter does not leave itself behind; over a see-through one only letters that
// stay put can be redrawn, on top of themselves.
void redraw(QPainter& painter, const Span& text, const QBrush& brush, const std::vector<QPointF>& offsets = {})
{
    const bool erase = text.background.alpha() == 255;
    if (!erase && !offsets.empty()) {
        return;
    }
    painter.save();
    if (erase) {
        for (const auto& letter : text.letters) {
            painter.fillRect(letter.rect, text.background);
        }
    }
    painter.setFont(text.font);
    painter.setPen(QPen(brush, 1));
    for (size_t i = 0; i < text.letters.size(); ++i) {
        const auto& letter = text.letters[i];
        const QPointF offset = i < offsets.size() ? offsets[i] : QPointF();
        painter.drawText(QRectF(letter.rect).translated(offset), Qt::AlignCenter, letter.text);
    }
    painter.restore();
}

// The letters drawn again on top, as they are, so a strong glow does not wash them out
void overdraw(QPainter& painter, const Span& text, const QColor& color)
{
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setFont(text.font);
    painter.setPen(color);
    for (const auto& letter : text.letters) {
        painter.drawText(letter.rect, Qt::AlignCenter, letter.text);
    }
    painter.restore();
}

void dot(QPainter& painter, const QPointF& centre, const qreal radius, const QColor& color, const qreal alpha)
{
    QRadialGradient halo(centre, radius * 2.5);
    halo.setColorAt(0.0, withAlpha(color, 0.6 * alpha));
    halo.setColorAt(1.0, withAlpha(color, 0.0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(centre, radius * 2.5, radius * 2.5);
    painter.setBrush(withAlpha(blend(color, Qt::white, 0.5), alpha));
    painter.drawEllipse(centre, radius, radius);
}

// A four pointed star flare
void twinkle(QPainter& painter, const QPointF& centre, const qreal size, const QColor& color, const qreal alpha)
{
    if (alpha <= 0.0 || size <= 0.0) {
        return;
    }
    const qreal waist = size * 0.12;
    for (int arm = 0; arm < 2; ++arm) {
        QPolygonF star;
        if (arm == 0) {
            star << centre + QPointF(-size, 0) << centre + QPointF(0, -waist) << centre + QPointF(size, 0) << centre + QPointF(0, waist);
        } else {
            star << centre + QPointF(0, -size) << centre + QPointF(waist, 0) << centre + QPointF(0, size) << centre + QPointF(-waist, 0);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(withAlpha(color, alpha));
        painter.drawPolygon(star);
    }
    dot(painter, centre, size * 0.15, color, alpha);
}

void twinkles(QPainter& painter, const Span& text, const qreal t, const QColor& color, const int count, const qreal size, const quint32 salt)
{
    const qreal h = text.rect.height();
    for (int i = 0; i < count; ++i) {
        const Life life = lifeOf(text.seed ^ salt, i, t, 0.6, 0.8);
        if (!life.alive) {
            continue;
        }
        const QPointF at(text.rect.left() + text.rect.width() * random01(text.seed, salt, i, life.generation), text.rect.top() + h * (0.1 + 0.8 * random01(text.seed, salt + 1, i, life.generation)));
        twinkle(painter, at, h * size * std::sin(M_PI * life.fraction), color, std::sin(M_PI * life.fraction));
    }
}

// Glowing specks that rise off the text and fade
void risingDots(QPainter& painter, const Span& text, const qreal t, const QColor& color, const int count, const qreal rise, const qreal radius, const quint32 salt)
{
    const qreal h = text.rect.height();
    for (int i = 0; i < count; ++i) {
        const Life life = lifeOf(text.seed ^ salt, i, t, 1.3, 0.4);
        if (!life.alive) {
            continue;
        }
        const qreal r1 = random01(text.seed, salt, i, life.generation);
        const QPointF at(text.rect.left() + r1 * text.rect.width() + std::sin(life.age * 3.0 + r1 * tau) * h * 0.15, text.rect.top() + h * 0.3 - rise * h * life.fraction);
        dot(painter, at, h * radius * (1.0 - 0.4 * life.fraction), color, std::sin(M_PI * std::min(1.0, life.fraction * 1.3)));
    }
}

// Hollow bubbles rising, swelling a little and popping at the top
void risingRings(QPainter& painter, const Span& text, const qreal t, const QColor& color, const int count, const qreal from, const qreal rise, const qreal radius, const quint32 salt)
{
    const qreal h = text.rect.height();
    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i < count; ++i) {
        const Life life = lifeOf(text.seed ^ salt, i, t, 1.6, 0.5);
        if (!life.alive) {
            continue;
        }
        const qreal r1 = random01(text.seed, salt, i, life.generation);
        const qreal r2 = random01(text.seed, salt + 1, i, life.generation);
        const QPointF at(text.rect.left() + r1 * text.rect.width() + std::sin(life.age * 2.5 + r1 * tau) * h * 0.12, text.rect.top() + h * from - rise * h * life.fraction);
        const qreal popping = life.fraction > 0.88 ? (life.fraction - 0.88) / 0.12 : 0.0;
        const qreal size = h * radius * (0.6 + 0.6 * r2) * (1.0 + 0.3 * life.fraction + popping);
        const qreal alpha = std::min(1.0, life.fraction * 5.0) * (1.0 - popping);
        painter.setPen(roundPen(withAlpha(color, 0.9 * alpha), std::max(1.0, h * 0.06)));
        painter.drawEllipse(at, size, size);
    }
}

// Liquid gathering under the letters, running down and letting go of a drop
void drips(QPainter& painter, const Span& text, const qreal t, const QColor& color, const int perLetter, const qreal longest, const qreal thickness)
{
    const qreal h = text.rect.height();
    const qreal base = text.rect.top() + h * 0.76;
    painter.setPen(Qt::NoPen);
    for (size_t i = 0; i < text.letters.size(); ++i) {
        const QRect& cell = text.letters[i].rect;
        const qreal width = std::max(1.5, cell.width() * thickness);
        for (int k = 0; k < perLetter; ++k) {
            const quint32 slot = static_cast<quint32>(i) * 4 + k;
            const Life life = lifeOf(text.seed ^ 0x5151, slot, t, 1.8, 0.9);
            if (!life.alive) {
                continue;
            }
            const qreal x = cell.left() + cell.width() * (0.2 + 0.6 * random01(text.seed, 52, slot, life.generation));
            const qreal full = h * longest * (0.4 + 0.6 * random01(text.seed, 53, slot, life.generation));
            const qreal grown = life.fraction < 0.7 ? std::pow(life.fraction / 0.7, 0.7) : 1.0 - (life.fraction - 0.7) / 0.3;
            const qreal length = full * grown;
            painter.setBrush(color);
            painter.drawRoundedRect(QRectF(x - width / 2, base - h * 0.1, width, h * 0.1 + length), width / 2, width / 2);
            painter.drawEllipse(QPointF(x, base + length), width * 0.65, width * 0.75);
            if (life.fraction >= 0.7) {
                const qreal falling = (life.fraction - 0.7) / 0.3;
                painter.setBrush(withAlpha(color, 1.0 - falling));
                painter.drawEllipse(QPointF(x, base + full + falling * falling * h * 1.2), width * 0.6, width * 0.8);
            }
        }
    }
}

// Soft blobs of colour drifting about behind the text
void cloud(QPainter& painter, const Span& text, const qreal t, const QColor& color, const qreal alpha, const qreal size, const qreal lift)
{
    const qreal h = text.rect.height();
    const int blobs = std::max(2, static_cast<int>(text.letters.size()) / 2 + 1);
    painter.setPen(Qt::NoPen);
    for (int b = 0; b < blobs; ++b) {
        const QPointF centre(text.rect.left() + text.rect.width() * (b + 0.5) / blobs + drift(text.seed, 60 + b, t * 0.5) * h * 0.4,
                             text.rect.center().y() - lift * h + drift(text.seed, 80 + b, t * 0.4) * h * 0.3);
        const qreal radius = h * size * (0.7 + 0.4 * random01(text.seed, 61, b)) * (1.0 + 0.12 * drift(text.seed, 100 + b, t * 0.7));
        QRadialGradient blob(centre, radius);
        blob.setColorAt(0.0, withAlpha(color, alpha));
        blob.setColorAt(0.6, withAlpha(color, alpha * 0.6));
        blob.setColorAt(1.0, withAlpha(color, 0.0));
        painter.setBrush(blob);
        painter.drawEllipse(centre, radius * 1.2, radius);
    }
}

// Short dashes torn out of the letters, flying off up and to the right and fading
void outgoingStreaks(QPainter& painter, const Span& text, const qreal t, const QColor& color, const int count)
{
    const qreal h = text.rect.height();
    for (int s = 0; s < count; ++s) {
        const Life life = lifeOf(text.seed ^ 0x7777, s, t, 0.7, 0.35);
        if (!life.alive) {
            continue;
        }
        auto r = [&](const quint32 salt) {
            return random01(text.seed, salt, s, life.generation);
        };
        const QPointF start(text.rect.left() + text.rect.width() * (0.2 + 0.8 * r(70)), text.rect.top() + h * (0.35 + 0.35 * r(71)));
        const qreal angle = (15.0 + 12.0 * r(72)) * M_PI / 180.0;
        const QPointF unit(std::cos(angle), -std::sin(angle));
        const qreal travelled = h * (1.8 + 1.6 * r(73)) * std::pow(life.fraction, 0.8);
        const QPointF head = start + unit * travelled;
        const QPointF tail = head - unit * std::min(travelled + h * 0.2, h * (0.45 + 0.25 * r(74)));
        const qreal alpha = std::min(1.0, life.fraction * 8.0) * (1.0 - life.fraction);
        painter.setPen(roundPen(withAlpha(color, 0.9 * alpha), std::max(2.0, h * 0.13)));
        painter.drawLine(tail, head);
        painter.setPen(roundPen(withAlpha(blend(color, Qt::white, 0.6), 0.75 * alpha), std::max(1.0, h * 0.04)));
        painter.drawLine(tail, head);
    }
}

std::vector<QPointF> waveOffsets(const Span& text, const qreal t, const qreal speed, const qreal spacing, const qreal dx, const qreal dy)
{
    std::vector<QPointF> offsets;
    offsets.reserve(text.letters.size());
    for (size_t i = 0; i < text.letters.size(); ++i) {
        const qreal phase = t * speed - i * spacing;
        offsets.emplace_back(std::sin(phase) * dx, std::cos(phase) * dy);
    }
    return offsets;
}

// A web of cells whose corners wander, each corner shared by the cells that
// meet there - the net of light sunlight casts through water
void honeycomb(QPainter& painter, const Span& text, const qreal t, const QColor& color, const qreal alpha, const qreal size, const qreal blur = 0.0)
{
    const qreal h = text.rect.height();
    const qreal spacing = h * size;
    const qreal rowStep = spacing * 0.866;
    const QRectF area = QRectF(text.rect).adjusted(-h * 1.2, -h * 1.4, h * 1.2, h * 1.4);
    const int columns = static_cast<int>(area.width() / spacing) + 2;
    const int rows = static_cast<int>(area.height() / rowStep) + 2;
    const qreal corner = spacing * 0.577;
    auto wander = [&](const QPointF& ideal) {
        const auto key = static_cast<quint32>(qRound(ideal.x() * 4.0 / spacing) * 7919 + qRound(ideal.y() * 4.0 / spacing));
        return ideal + QPointF(drift(text.seed, key, t * 0.6), drift(text.seed, key + 77777, t * 0.55)) * spacing * 0.28;
    };
    const QPointF centre = QRectF(text.rect).center();
    const qreal reachX = area.width() / 2;
    const qreal reachY = area.height() / 2;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const QPointF middle = area.topLeft() + QPointF((x + 0.5 * (y & 1)) * spacing, y * rowStep);
            const qreal fall = 1.0 - std::hypot((middle.x() - centre.x()) / reachX, (middle.y() - centre.y()) / reachY);
            if (fall <= 0.0) {
                continue;
            }
            // A blurred web is the same lines drawn wide and faint
            painter.setPen(roundPen(withAlpha(color, alpha * std::min(1.0, fall * 1.6)), std::max(1.0, blur * h)));
            // Three of the six sides; the cells alongside draw the other three
            for (int side = 0; side < 3; ++side) {
                const qreal from = (30.0 + 60.0 * side) * M_PI / 180.0;
                const qreal to = from + M_PI / 3.0;
                painter.drawLine(wander(middle + QPointF(std::cos(from), std::sin(from)) * corner), wander(middle + QPointF(std::cos(to), std::sin(to)) * corner));
            }
        }
    }
}

void paintFire(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    const qreal flicker = 0.85 + 0.15 * drift(text.seed, 1, t * 7.0);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setPen(Qt::NoPen);
    // Tongues of flame licking up off the letters
    for (size_t i = 0; i < text.letters.size(); ++i) {
        const QRect& cell = text.letters[i].rect;
        for (int k = 0; k < 2; ++k) {
            const quint32 slot = static_cast<quint32>(i) * 2 + k;
            const Life life = lifeOf(text.seed ^ 0xf1, slot, t, 0.55, 0.15);
            if (!life.alive) {
                continue;
            }
            const qreal x = cell.left() + cell.width() * (0.2 + 0.6 * random01(text.seed, 2, slot, life.generation));
            const qreal base = cell.top() + h * 0.4;
            const qreal height = h * (0.35 + 0.35 * random01(text.seed, 3, slot, life.generation)) * std::sin(M_PI * life.fraction);
            const qreal width = cell.width() * 0.55;
            const qreal sway = std::sin(t * 9.0 + i) * h * 0.06;
            QPainterPath tongue;
            tongue.moveTo(x - width / 2, base);
            tongue.quadTo(x - width * 0.35 + sway, base - height * 0.6, x + sway * 2, base - height);
            tongue.quadTo(x + width * 0.35 + sway, base - height * 0.6, x + width / 2, base);
            tongue.closeSubpath();
            QLinearGradient heat(x, base, x, base - height);
            heat.setColorAt(0.0, QColor(255, 170, 40, 150));
            heat.setColorAt(1.0, QColor(255, 40, 0, 0));
            painter.setBrush(heat);
            painter.drawPath(tongue);
        }
    }
    glow(painter, text, QColor(255, 90, 0), h * 0.4, 3.5 * flicker);
    glow(painter, text, QColor(255, 220, 70), h * 0.12, 2.6 * flicker);
    overdraw(painter, text, text.color);
    risingDots(painter, text, t, QColor(255, 150, 40), std::max(2, static_cast<int>(text.letters.size()) / 2 + 1), 1.6, 0.06, 0xe1);
    painter.restore();
}

void paintCold(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    glow(painter, text, text.color, h * 0.3, 1.7);
    overdraw(painter, text, text.color);
    // Frost crystals forming around the letters and slowly sinking
    const QColor ice = blend(text.color, Qt::white, 0.35);
    painter.setPen(Qt::NoPen);
    for (size_t i = 0; i < text.letters.size(); ++i) {
        const QRect& cell = text.letters[i].rect;
        for (int k = 0; k < 4; ++k) {
            const quint32 slot = static_cast<quint32>(i) * 4 + k;
            const Life life = lifeOf(text.seed ^ 0xc0, slot, t, 1.8, 0.7);
            if (!life.alive) {
                continue;
            }
            const qreal angle = tau * random01(text.seed, 4, slot, life.generation);
            const QPointF at(cell.center().x() + std::cos(angle) * cell.width() * 0.7, cell.center().y() + std::sin(angle) * h * 0.6 + life.fraction * h * 0.25);
            const qreal size = h * (0.05 + 0.05 * random01(text.seed, 5, slot, life.generation));
            const qreal alpha = std::sin(M_PI * life.fraction);
            painter.setBrush(withAlpha(ice, alpha));
            painter.save();
            painter.translate(at);
            painter.rotate(45.0);
            painter.drawRect(QRectF(-size, -size, size * 2, size * 2));
            painter.restore();
        }
    }
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    twinkles(painter, text, t, QColor(220, 255, 255), 2, 0.3, 0xc1);
    painter.restore();
}

void paintAcid(QPainter& painter, const Span& text, const qreal t)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    glow(painter, text, text.color, text.rect.height() * 0.25, 1.3);
    overdraw(painter, text, text.color);
    drips(painter, text, t, blend(text.color, Qt::white, 0.15), 2, 0.6, 0.18);
    painter.restore();
}

void paintMagic(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    glow(painter, text, Qt::white, h * 0.25, 0.7);
    overdraw(painter, text, text.color);
    risingRings(painter, text, t, QColor(180, 90, 255), std::max(2, static_cast<int>(text.letters.size()) / 2), 1.6, 1.6, 0.16, 0xa1);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    twinkles(painter, text, t, Qt::white, 3, 0.35, 0xa2);
    painter.restore();
}

void paintPoison(QPainter& painter, const Span& text, const qreal t)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    cloud(painter, text, t, blend(text.color, QColor(170, 60, 255), 0.4), 0.6, 0.95, 0.1);
    overdraw(painter, text, text.color);
    risingRings(painter, text, t, blend(text.color, QColor(200, 120, 255), 0.5), static_cast<int>(text.letters.size()) / 2 + 2, 1.0, 1.6, 0.13, 0xb1);
    painter.restore();
}

void paintDrain(QPainter& painter, const Span& text, const qreal t, const QColor& streak)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    glow(painter, text, text.color, text.rect.height() * 0.3, 1.4);
    overdraw(painter, text, text.color);
    outgoingStreaks(painter, text, t, streak, std::max(4, static_cast<int>(text.letters.size())));
    painter.restore();
}

void paintDarkness(QPainter& painter, const Span& text, const qreal t)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // A murky fog, veined with a dim web that slowly writhes
    cloud(painter, text, t, QColor(50, 50, 50), 0.45, 1.1, 0.0);
    honeycomb(painter, text, t * 0.7, QColor(130, 130, 130), 0.06, 0.85, 0.3);
    honeycomb(painter, text, t * 0.7, QColor(130, 130, 130), 0.09, 0.85, 0.12);
    overdraw(painter, text, text.color);
    painter.restore();
}

void paintLight(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setPen(Qt::NoPen);
    // Slowly turning rays of light, fanning out from all along the text
    const QPointF centre = QRectF(text.rect).center();
    const QColor beam = blend(text.color, QColor(255, 240, 150), 0.5);
    constexpr int rays = 16;
    const qreal turn = t * 0.25 + tau * random01(text.seed, 20);
    for (int i = 0; i < rays; ++i) {
        const qreal angle = turn + i * tau / rays + 0.08 * drift(text.seed, 21 + i, t * 0.5);
        const qreal length = h * (2.0 + 1.2 * random01(text.seed, 22, i)) * (0.85 + 0.15 * drift(text.seed, 40 + i, t));
        const QPointF origin = centre + QPointF(std::cos(angle) * text.rect.width() * 0.3, std::sin(angle) * h * 0.15);
        const qreal spread = 0.05;
        QPolygonF ray;
        ray << origin << origin + QPointF(std::cos(angle - spread), std::sin(angle - spread)) * length << origin + QPointF(std::cos(angle + spread), std::sin(angle + spread)) * length;
        QRadialGradient fade(origin, length);
        fade.setColorAt(0.0, withAlpha(beam, 0.6));
        fade.setColorAt(1.0, withAlpha(beam, 0.0));
        painter.setBrush(fade);
        painter.drawPolygon(ray);
    }
    glow(painter, text, beam, h * 0.3, 1.8);
    overdraw(painter, text, text.color);
    twinkles(painter, text, t, Qt::white, 2, 0.3, 0x11);
    painter.restore();
}

void paintWind(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    const qreal cell = text.letters.empty() ? h * 0.5 : text.letters.front().rect.width();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    std::vector<QPointF> sway;
    for (size_t i = 0; i < text.letters.size(); ++i) {
        sway.emplace_back((0.6 * std::sin(t * 2.6 + i * 0.5) + 0.4 * drift(text.seed, i, t * 1.5)) * cell * 0.12, 0.0);
    }
    redraw(painter, text, text.color, sway);
    // Gusts: thin streaks racing past
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    const QColor streak = blend(text.color, QColor(0, 200, 160), 0.4);
    for (int s = 0; s < 7; ++s) {
        const Life life = lifeOf(text.seed ^ 0x3d, s, t, 0.7, 0.3);
        if (!life.alive) {
            continue;
        }
        const qreal y = text.rect.top() + h * (-0.4 + 1.8 * random01(text.seed, 30, s, life.generation));
        const qreal length = h * (0.8 + 1.6 * random01(text.seed, 31, s, life.generation));
        const qreal x = text.rect.left() - h * 1.5 + (text.rect.width() + h * 4.0) * life.fraction;
        painter.setPen(roundPen(withAlpha(streak, 0.6 * std::sin(M_PI * life.fraction)), std::max(1.0, h * 0.06)));
        painter.drawLine(QPointF(x - length, y), QPointF(x, y));
    }
    painter.restore();
}

void paintWater(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    redraw(painter, text, text.color, waveOffsets(text, t, 2.2, 0.7, 0.0, h * 0.06));
    honeycomb(painter, text, t, blend(text.color, QColor(140, 140, 255), 0.5), 0.85, 0.75);
    // Lifted toward white and lit from within, so that a deep blue still reads over the net
    glow(painter, text, text.color, h * 0.25, 0.8);
    overdraw(painter, text, blend(text.color, Qt::white, 0.3));
    risingRings(painter, text, t, QColor(150, 170, 255), static_cast<int>(text.letters.size()) / 2 + 2, 1.0, 1.4, 0.11, 0x22);
    painter.restore();
}

void paintEnergy(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    const qreal pulse = 0.65 + 0.35 * std::sin(t * 5.0);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    glow(painter, text, text.color, h * 0.32, 1.3 * pulse);
    glow(painter, text, text.color, h * 0.1, 1.0);
    overdraw(painter, text, text.color);
    // Tiny arcs crackling inside the letters
    const auto tick = static_cast<quint32>(t / 0.06);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int s = 0; s < 3; ++s) {
        if (text.letters.empty()) {
            break;
        }
        const size_t letter = static_cast<size_t>(random01(text.seed, tick, s, 80) * text.letters.size());
        const QRectF cell = text.letters[letter].rect;
        const QPointF a(cell.left() + cell.width() * random01(text.seed, tick, s, 81), cell.top() + h * (0.25 + 0.5 * random01(text.seed, tick, s, 82)));
        const QPointF b(cell.left() + cell.width() * random01(text.seed, tick, s, 83), cell.top() + h * (0.25 + 0.5 * random01(text.seed, tick, s, 84)));
        painter.setPen(roundPen(QColor(255, 230, 255, 230), 1.0));
        painter.drawPolyline(bolt(a, b, text.seed ^ tick, s, 0.35));
    }
    painter.restore();
}

void leaf(QPainter& painter, const QPointF& at, const qreal angle, const qreal length)
{
    painter.save();
    painter.translate(at);
    painter.rotate(angle);
    QPainterPath blade;
    blade.moveTo(0, 0);
    blade.cubicTo(length * 0.35, -length * 0.15, length * 0.35, -length * 0.8, 0, -length);
    blade.cubicTo(-length * 0.35, -length * 0.8, -length * 0.35, -length * 0.15, 0, 0);
    QLinearGradient green(0, 0, 0, -length);
    green.setColorAt(0.0, QColor(45, 130, 40));
    green.setColorAt(1.0, QColor(120, 215, 90));
    painter.setPen(Qt::NoPen);
    painter.setBrush(green);
    painter.drawPath(blade);
    painter.setPen(roundPen(QColor(35, 95, 30), std::max(0.8, length * 0.06)));
    painter.drawLine(QPointF(0, 0), QPointF(0, -length * 0.85));
    painter.restore();
}

// A curl of tendril, winding in on itself
void tendril(QPainter& painter, const QPointF& at, const qreal size, const qreal direction, const qreal sway)
{
    QPainterPath curl;
    curl.moveTo(at);
    for (int step = 1; step <= 24; ++step) {
        const qreal f = step / 24.0;
        const qreal angle = direction + sway + f * 4.0 * M_PI * (direction > 0 ? 1.0 : -1.0);
        const qreal radius = size * (1.0 - f * 0.85);
        curl.lineTo(at + QPointF(direction > 0 ? f * size * 1.2 : -f * size * 1.2, 0) + QPointF(std::cos(angle), std::sin(angle)) * radius * f);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(roundPen(QColor(70, 160, 60), std::max(1.0, size * 0.12)));
    painter.drawPath(curl);
}

// A vine wound around the words like a helix: the turns behind are drawn
// before the letters go back over them, the turns in front after. Nothing
// about it is regular - the twist, the reach, the girth and where leaves
// grow all wander along its length, fixed by the span's seed.
void paintWood(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    const qreal left = text.rect.left() - h * 0.5;
    const qreal right = text.rect.right() + h * 0.5;
    const qreal middle = QRectF(text.rect).center().y();
    const quint32 seed = text.seed;
    auto wave = [&](const qreal x, const quint32 salt, const qreal scale) {
        return std::sin(x / (h * scale) + tau * random01(seed, salt));
    };
    // The twist: tight in places, long and lazy in others
    auto twist = [&](const qreal x, const qreal offset) {
        const qreal u = x - left;
        // The slow writhing: coils drifting along, tightening and loosening
        return tau * u / (h * 2.6) + 1.3 * wave(x, 1, 2.9) + 0.6 * wave(x, 2, 1.3) + offset + 0.45 * std::sin(t * 0.6 + u / (h * 1.4)) + 0.25 * std::sin(t * 1.1 - u / (h * 0.8));
    };
    auto reachAt = [&](const qreal x) {
        return h * (0.62 + 0.22 * wave(x, 3, 2.1) + 0.08 * wave(x, 4, 0.9) + 0.1 * std::sin(t * 0.8 + x / (h * 1.5)));
    };
    auto heightAt = [&](const qreal x, const qreal offset, const qreal scale) {
        return middle + h * 0.12 * std::sin(x / (h * 3.7) + tau * random01(seed, 5) + t * 0.5) + std::sin(twist(x, offset)) * reachAt(x) * scale;
    };
    auto taper = [&](const qreal x) {
        return std::clamp(std::min(x - left, right - x) / (h * 0.8), 0.25, 1.0);
    };

    auto strand = [&](const bool front, const qreal offset, const qreal scale, const QColor& colour, const QColor& highlight, const qreal width, const quint32 salt) {
        QPointF previous;
        bool havePrevious = false;
        for (qreal x = left; x <= right; x += 1.5) {
            const QPointF point(x, heightAt(x, offset, scale));
            const bool inFront = std::cos(twist(x, offset)) > 0.0;
            if (havePrevious && inFront == front) {
                const qreal girth = width * taper(x) * (0.8 + 0.25 * wave(x, salt, 0.7) + 0.15 * wave(x, salt + 1, 0.23));
                painter.setPen(roundPen(colour, std::max(1.0, girth)));
                painter.drawLine(previous, point);
                if (highlight.isValid()) {
                    painter.setPen(roundPen(highlight, std::max(0.8, girth * 0.3)));
                    painter.drawLine(previous - QPointF(0, girth * 0.2), point - QPointF(0, girth * 0.2));
                }
            }
            previous = point;
            havePrevious = true;
        }
    };
    // Leaves at uneven spacing, sometimes in pairs, only where the vine is
    // clear of the letters, pointing away from them
    auto leaves = [&](const bool front) {
        int index = 0;
        for (qreal x = left + h * 0.3; x <= right - h * 0.2; x += h * (0.25 + 0.6 * random01(seed, 41, index)), ++index) {
            const qreal y = heightAt(x, 0.0, 1.0);
            // Grown in as the vine swings clear of the letters, rather than popping in
            const qreal clear = std::clamp((std::abs(y - middle) - h * 0.3) / (h * 0.2), 0.0, 1.0);
            if (clear <= 0.0 || (std::cos(twist(x, 0.0)) > 0.0) != front || random01(seed, 42, index) < 0.15) {
                continue;
            }
            const qreal outward = y < middle ? 0.0 : 180.0;
            const int count = random01(seed, 43, index) < 0.3 ? 2 : 1;
            for (int k = 0; k < count; ++k) {
                const qreal lean = (random01(seed, 44, index * 2 + k) - 0.5) * 110.0 + 18.0 * std::sin(t * (1.4 + random01(seed, 45, index)) + index);
                leaf(painter, QPointF(x + k * h * 0.12, y), outward + lean, clear * h * (0.32 + 0.22 * random01(seed, 46, index * 2 + k)));
            }
        }
    };
    // Now and then a short woody twig with a leaf at its tip
    auto twigs = [&](const bool front) {
        int index = 0;
        for (qreal x = left + h; x <= right - h * 0.5; x += h * (1.4 + 1.6 * random01(seed, 51, index)), ++index) {
            const qreal y = heightAt(x, 0.0, 1.0);
            const qreal clear = std::clamp((std::abs(y - middle) - h * 0.35) / (h * 0.2), 0.0, 1.0);
            if (clear <= 0.0 || (std::cos(twist(x, 0.0)) > 0.0) != front) {
                continue;
            }
            const qreal side = y < middle ? -1.0 : 1.0;
            const QPointF end(x + (random01(seed, 52, index) - 0.3) * h * 0.6 + std::sin(t * 1.3 + index) * h * 0.08, y + side * clear * h * (0.25 + 0.2 * random01(seed, 53, index)));
            painter.setPen(roundPen(QColor(95, 62, 30), std::max(1.0, h * 0.05)));
            painter.drawLine(QPointF(x, y), end);
            leaf(painter, end, (side < 0 ? 0.0 : 180.0) + (random01(seed, 54, index) - 0.5) * 60.0 + 15.0 * std::sin(t * 1.7 + index), clear * h * 0.28);
        }
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const QColor bark(105, 68, 32);
    const QColor barkLight(160, 112, 60);
    const QColor twine(55, 140, 50);
    const qreal stem = std::max(2.0, h * 0.11);
    const qreal tendrilPhase = M_PI * (0.4 + 0.5 * random01(seed, 6));
    strand(false, 0.0, 1.0, bark.darker(130), QColor(), stem, 60);
    strand(false, tendrilPhase, 0.8, twine.darker(130), QColor(), std::max(1.0, h * 0.045), 70);
    twigs(false);
    leaves(false);
    overdraw(painter, text, text.color);
    strand(true, tendrilPhase, 0.8, twine, QColor(), std::max(1.0, h * 0.045), 70);
    strand(true, 0.0, 1.0, bark, barkLight, stem, 60);
    twigs(true);
    leaves(true);
    // Wherever the vine does cross a letter, the letter still shows through
    painter.setOpacity(0.55);
    overdraw(painter, text, text.color);
    painter.setOpacity(1.0);
    const qreal sway = 0.5 * std::sin(t * 1.5);
    tendril(painter, QPointF(right, heightAt(right, 0.0, 1.0)), h * 0.35, 1.0, sway);
    tendril(painter, QPointF(left, heightAt(left, 0.0, 1.0)), h * 0.3, -1.0, -sway);
    painter.restore();
}

QBrush sandBrush(const QColor& color)
{
    static QCache<QRgb, QImage> cache(64);
    const QRgb key = color.rgba();
    if (const QImage* tile = cache.object(key)) {
        return QBrush(*tile);
    }
    QImage tile(8, 8, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const qreal grain = random01(key, x, y, 9);
            const QColor shade = grain < 0.35 ? blend(color, Qt::black, 0.35) : grain > 0.8 ? blend(color, Qt::white, 0.35) : blend(color, QColor(230, 200, 120), 0.3);
            tile.setPixel(x, y, shade.rgba());
        }
    }
    cache.insert(key, new QImage(tile));
    return QBrush(tile);
}

void paintEarth(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    redraw(painter, text, sandBrush(blend(text.color, QColor(200, 170, 90), 0.5)));
    cloud(painter, text, t, QColor(130, 105, 55), 0.55, 0.8, -0.5);
    // Grains trickling down off the letters
    painter.setPen(Qt::NoPen);
    const int grains = static_cast<int>(text.letters.size()) + 2;
    for (int g = 0; g < grains; ++g) {
        const Life life = lifeOf(text.seed ^ 0xea, g, t, 1.2, 0.5);
        if (!life.alive) {
            continue;
        }
        const QPointF at(text.rect.left() + text.rect.width() * random01(text.seed, 90, g, life.generation), text.rect.top() + h * 0.8 + life.fraction * life.fraction * h * 1.1);
        painter.setBrush(QColor(225, 185, 95, static_cast<int>(255 * (1.0 - life.fraction))));
        painter.drawEllipse(at, std::max(1.0, h * 0.06), std::max(1.0, h * 0.06));
    }
    painter.restore();
}

void paintToxic(QPainter& painter, const Span& text, const qreal t)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    cloud(painter, text, t, blend(text.color, QColor(150, 60, 220), 0.4), 0.55, 0.9, 0.2);
    overdraw(painter, text, text.color);
    drips(painter, text, t, text.color, 2, 0.7, 0.24);
    painter.restore();
}

void paintSilver(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // Polished metal: bright above and below a dark horizon band
    QLinearGradient chrome(0, text.rect.top() + h * 0.15, 0, text.rect.bottom() - h * 0.15);
    chrome.setColorAt(0.0, Qt::white);
    chrome.setColorAt(0.44, blend(text.color, Qt::white, 0.7));
    chrome.setColorAt(0.52, QColor(60, 60, 70));
    chrome.setColorAt(0.65, blend(text.color, Qt::white, 0.2));
    chrome.setColorAt(1.0, Qt::white);
    redraw(painter, text, chrome);
    glow(painter, text, Qt::white, h * 0.15, 0.6);
    // A star glint hopping from letter to letter
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    constexpr qreal interval = 1.1;
    const qreal local = t + interval * random01(text.seed, 12);
    const auto hop = static_cast<quint32>(local / interval);
    const qreal age = local - hop * interval;
    if (age < 0.5 && !text.letters.empty()) {
        const QRect& cell = text.letters[static_cast<size_t>(random01(text.seed, hop, 13) * text.letters.size())].rect;
        const qreal grow = std::sin(M_PI * age / 0.5);
        twinkle(painter, QPointF(cell.right(), cell.top() + h * 0.3), h * 0.7 * grow, Qt::white, grow);
    }
    twinkles(painter, text, t, QColor(230, 230, 255), 2, 0.2, 0x51);
    painter.restore();
}

void paintPiercing(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    // Needles of light driving down through the text
    for (int s = 0; s < 4; ++s) {
        const Life life = lifeOf(text.seed ^ 0x9e, s, t, 0.55, 0.25);
        if (!life.alive) {
            continue;
        }
        const qreal x = text.rect.left() + text.rect.width() * (0.1 + 0.8 * random01(text.seed, 95, s, life.generation));
        const QPointF start(x + h * 1.1, text.rect.top() - h * 1.6);
        const QPointF end(x - h * 0.5, text.rect.bottom() + h * 0.9);
        const QPointF head = start + (end - start) * life.fraction;
        const QPointF along = (end - start) / std::hypot(end.x() - start.x(), end.y() - start.y());
        const QPointF tail = head - along * std::min(h * 1.7, std::hypot(head.x() - start.x(), head.y() - start.y()));
        const qreal alpha = life.fraction < 0.85 ? 1.0 : (1.0 - life.fraction) / 0.15;
        QLinearGradient streak(tail, head);
        streak.setColorAt(0.0, QColor(255, 255, 255, 0));
        streak.setColorAt(1.0, QColor(255, 255, 255, static_cast<int>(230 * alpha)));
        painter.setPen(QPen(QBrush(streak), std::max(1.5, h * 0.08), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(tail, head);
        dot(painter, head, h * 0.11, Qt::white, alpha);
    }
    painter.restore();
}

void paintPhysical(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    constexpr qreal interval = 1.5;
    const qreal local = t + interval * random01(text.seed, 14);
    const auto blow = static_cast<quint32>(local / interval);
    const qreal age = local - blow * interval;
    if (age > 0.9) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    if (age < 0.3) {
        glow(painter, text, Qt::white, h * 0.25, 1.0 - age / 0.3);
    }
    // The shock of the blow ringing outward in rounded frames
    painter.setBrush(Qt::NoBrush);
    for (int k = 0; k < 2; ++k) {
        const qreal a = age - k * 0.12;
        if (a <= 0.0) {
            continue;
        }
        const qreal margin = h * (0.25 + 2.0 * a);
        const qreal alpha = std::max(0.0, 0.75 * (1.0 - a / 0.78));
        painter.setPen(roundPen(QColor(220, 220, 220, static_cast<int>(255 * alpha)), std::max(1.5, h * 0.08)));
        const QRectF frame = QRectF(text.rect).adjusted(-margin * 1.3, -margin, margin * 1.3, margin);
        painter.drawRoundedRect(frame, h * 0.6 + margin, h * 0.6 + margin);
    }
    // Shards flung out from the impact
    const QPointF centre = QRectF(text.rect).center();
    for (int s = 0; s < 10; ++s) {
        const qreal angle = tau * random01(text.seed, blow, s, 15);
        const QPointF unit(std::cos(angle), std::sin(angle));
        const qreal out = h * (0.4 + 2.2 * std::pow(age / 0.9, 0.6));
        const QPointF from = centre + QPointF(unit.x() * (text.rect.width() / 2.0 + out), unit.y() * (h * 0.5 + out * 0.6));
        painter.setPen(roundPen(QColor(255, 255, 255, static_cast<int>(255 * (1.0 - age / 0.9))), std::max(1.0, h * 0.06)));
        painter.drawLine(from, from + unit * h * 0.35);
    }
    painter.restore();
}

void paintBleeding(QPainter& painter, const Span& text, const qreal t)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    drips(painter, text, t, blend(text.color, QColor(220, 20, 20), 0.5), 3, 0.8, 0.22);
    painter.restore();
}

void paintSound(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    // Arcs rippling outward from both ends of the words
    const qreal mid = QRectF(text.rect).center().y();
    for (int side = 0; side < 2; ++side) {
        const qreal x = side == 0 ? text.rect.left() + h * 0.3 : text.rect.right() - h * 0.3;
        for (int k = 0; k < 3; ++k) {
            const qreal phase = std::fmod(t * 0.9 + k / 3.0, 1.0);
            const qreal radius = h * (0.45 + 0.9 * phase);
            const qreal alpha = (1.0 - phase) * 0.9;
            painter.setPen(roundPen(withAlpha(text.color, alpha), std::max(1.5, h * 0.08)));
            const QRectF bounds(x - radius, mid - radius * 1.1, radius * 2, radius * 2.2);
            painter.drawArc(bounds, side == 0 ? 130 * 16 : -50 * 16, 100 * 16);
        }
    }
    painter.restore();
}

void paintMental(QPainter& painter, const Span& text, const qreal t)
{
    const qreal h = text.rect.height();
    const qreal cell = text.letters.empty() ? h * 0.5 : text.letters.front().rect.width();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // Psychic pressure: a wave pushing through the letters
    redraw(painter, text, text.color, waveOffsets(text, t, 5.0, 0.9, cell * 0.1, h * 0.04));
    glow(painter, text, text.color, h * 0.3, 1.3 + 0.4 * std::sin(t * 3.0));
    overdraw(painter, text, text.color);
    painter.restore();
}

struct KindInfo
{
    // The first is the name it is reported by; the rest are accepted too
    QStringList names;
    // How far it reaches past the text, in font heights: left, top, right, bottom
    qreal left, top, right, bottom;
};

const std::array<KindInfo, TTextEffect::KindCount>& kinds()
{
    static const std::array<KindInfo, TTextEffect::KindCount> table{{
            {{qsl("none")}, 0, 0, 0, 0},
            {{qsl("lifedrain")}, 0.5, 3.8, 3.0, 0.5},
            {{qsl("lightning"), qsl("shocking")}, 5, 4, 5, 2},
            {{qsl("fire"), qsl("flaming")}, 0.6, 2.2, 0.6, 0.6},
            {{qsl("cold"), qsl("freezing")}, 0.8, 0.8, 0.8, 0.8},
            {{qsl("acid"), qsl("acidic")}, 0.4, 0.4, 0.4, 1.8},
            {{qsl("magic"), qsl("arcane")}, 0.6, 1.2, 0.6, 1.4},
            {{qsl("poison"), qsl("poisonous")}, 1.2, 1.4, 1.2, 1.2},
            {{qsl("drain"), qsl("draining")}, 0.6, 2.0, 5.0, 0.6},
            {{qsl("darkness"), qsl("unholy")}, 1.3, 1.4, 1.3, 1.3},
            {{qsl("light"), qsl("radiant")}, 3.5, 3.5, 3.5, 3.5},
            {{qsl("wind"), qsl("gusting")}, 2.0, 1.0, 3.0, 1.0},
            {{qsl("water"), qsl("drenching")}, 1.2, 1.5, 1.2, 1.5},
            {{qsl("energy"), qsl("energized")}, 0.6, 0.6, 0.6, 0.6},
            {{qsl("manadrain")}, 0.6, 2.0, 5.0, 0.6},
            {{qsl("wood"), qsl("overgrown"), qsl("vines")}, 1.6, 1.3, 1.6, 1.3},
            {{qsl("earth"), qsl("earthen")}, 0.6, 0.6, 0.6, 1.8},
            {{qsl("toxic"), qsl("diseased")}, 1.2, 1.2, 1.2, 1.8},
            {{qsl("silver"), qsl("silvered")}, 0.8, 0.8, 0.8, 0.8},
            {{qsl("piercing"), qsl("nonormal")}, 1.0, 2.2, 2.0, 1.2},
            {{qsl("physical"), qsl("normal")}, 3.5, 3.0, 3.5, 3.0},
            {{qsl("bleeding")}, 0.4, 0.4, 0.4, 2.0},
            {{qsl("sound"), qsl("sonic"), qsl("soundwaves")}, 2.2, 1.4, 2.2, 1.4},
            {{qsl("mental"), qsl("psionic")}, 0.8, 0.8, 0.8, 0.8},
    }};
    return table;
}
} // namespace

int TTextEffect::kindFromName(const QString& name)
{
    if (name.isEmpty()) {
        return None;
    }
    const auto& table = kinds();
    for (int kind = 0; kind < KindCount; ++kind) {
        if (table[kind].names.contains(name, Qt::CaseInsensitive)) {
            return kind;
        }
    }
    return -1;
}

QString TTextEffect::nameForKind(const int kind)
{
    return (kind > None && kind < KindCount) ? kinds()[kind].names.first() : qsl("none");
}

QMargins TTextEffect::reach(const int kind, const int fontHeight)
{
    if (kind <= None || kind >= KindCount) {
        return QMargins();
    }
    const KindInfo& info = kinds()[kind];
    return QMargins(qCeil(info.left * fontHeight), qCeil(info.top * fontHeight), qCeil(info.right * fontHeight), qCeil(info.bottom * fontHeight));
}

void TTextEffect::paint(QPainter& painter, const int kind, const Span& span, const qreal seconds)
{
    if (span.rect.height() <= 0 || span.rect.width() <= 0) {
        return;
    }
    switch (kind) {
    case Lifedrain:
        paintLifedrain(painter, span, seconds);
        break;
    case Lightning:
        paintLightning(painter, span, seconds);
        break;
    case Fire:
        paintFire(painter, span, seconds);
        break;
    case Cold:
        paintCold(painter, span, seconds);
        break;
    case Acid:
        paintAcid(painter, span, seconds);
        break;
    case Magic:
        paintMagic(painter, span, seconds);
        break;
    case Poison:
        paintPoison(painter, span, seconds);
        break;
    case Drain:
        paintDrain(painter, span, seconds, QColor(235, 45, 45));
        break;
    case Darkness:
        paintDarkness(painter, span, seconds);
        break;
    case Light:
        paintLight(painter, span, seconds);
        break;
    case Wind:
        paintWind(painter, span, seconds);
        break;
    case Water:
        paintWater(painter, span, seconds);
        break;
    case Energy:
        paintEnergy(painter, span, seconds);
        break;
    case ManaDrain:
        paintDrain(painter, span, seconds, blend(span.color, QColor(170, 170, 255), 0.5));
        break;
    case Wood:
        paintWood(painter, span, seconds);
        break;
    case Earth:
        paintEarth(painter, span, seconds);
        break;
    case Toxic:
        paintToxic(painter, span, seconds);
        break;
    case Silver:
        paintSilver(painter, span, seconds);
        break;
    case Piercing:
        paintPiercing(painter, span, seconds);
        break;
    case Physical:
        paintPhysical(painter, span, seconds);
        break;
    case Bleeding:
        paintBleeding(painter, span, seconds);
        break;
    case Sound:
        paintSound(painter, span, seconds);
        break;
    case Mental:
        paintMental(painter, span, seconds);
        break;
    default:
        break;
    }
}
