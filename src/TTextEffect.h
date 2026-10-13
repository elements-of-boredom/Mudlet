#ifndef MUDLET_TTEXTEFFECT_H
#define MUDLET_TTEXTEFFECT_H

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

#include <QColor>
#include <QFont>
#include <QMargins>
#include <QRect>
#include <QString>

#include <vector>

class QPainter;

// Animated decorations painted over a run of text that carries one, see TChar::effect()
class TTextEffect
{
public:
    // Stored in five bits of TChar (TChar::EffectMask), so at most 31 besides None
    enum Kind { None = 0, Bolt, Flame, Frost, Drip, Sparkle, Fumes, Streak, Shadow, Rays, Sway, Ripple, Vine, Dust, Needles, Impact, Waves, Warp, KindCount };

    struct Letter
    {
        QRect rect;
        QString text;
    };
    // One run of text with the same effect, on one row of the screen
    struct Span
    {
        QRect rect;
        std::vector<Letter> letters;
        QColor color;
        // Transparent where the console shows through, which rules out redrawing a letter elsewhere
        QColor background;
        QFont font;
        quint32 seed = 0;
    };

    // -1 for a name that is not an effect
    static int kindFromName(const QString& name);
    static QString nameForKind(int kind);
    // How far past the text it decorates an effect paints
    static QMargins reach(int kind, int fontHeight);
    static qreal loopLength(int kind);
    static void paint(QPainter& painter, int kind, const Span& span, qreal seconds);
    // One clock for every console, so that two views of the same text animate in step
    static qreal seconds();

private:
    static void paintBolt(QPainter& painter, const Span& text, qreal seconds);
};

#endif // MUDLET_TTEXTEFFECT_H
