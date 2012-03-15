/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "searchresulttreeitemdelegate.h"
#include "searchresulttreeitemroles.h"

#include <QTextDocument>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QApplication>

#include <QModelIndex>
#include <QDebug>

#include <math.h>

using namespace Find::Internal;

SearchResultTreeItemDelegate::SearchResultTreeItemDelegate(QObject *parent)
  : QItemDelegate(parent)
{
}

void SearchResultTreeItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    static const int iconSize = 16;

    painter->save();

    QStyleOptionViewItemV3 opt = setOptions(index, option);
    painter->setFont(opt.font);

    QItemDelegate::drawBackground(painter, opt, index);

    // ---- do the layout
    QRect checkRect;
    QRect pixmapRect;
    QRect textRect;

    // check mark
    bool checkable = (index.model()->flags(index) & Qt::ItemIsUserCheckable);
    Qt::CheckState checkState = Qt::Unchecked;
    if (checkable) {
        QVariant checkStateData = index.data(Qt::CheckStateRole);
        checkState = static_cast<Qt::CheckState>(checkStateData.toInt());
#if QT_VERSION >= 0x050000
        checkRect = doCheck(opt, opt.rect, checkStateData);
#else // Qt4
        checkRect = check(opt, opt.rect, checkStateData);
#endif
    }

    // icon
    QIcon icon = index.model()->data(index, ItemDataRoles::ResultIconRole).value<QIcon>();
    if (!icon.isNull()) {
        pixmapRect = QRect(0, 0, iconSize, iconSize);
    }

    // text
    textRect = opt.rect.adjusted(0, 0, checkRect.width() + pixmapRect.width(), 0);

    // do layout
    doLayout(opt, &checkRect, &pixmapRect, &textRect, false);

    // ---- draw the items
    // icon
    if (!icon.isNull())
        QItemDelegate::drawDecoration(painter, opt, pixmapRect, icon.pixmap(iconSize));

    // line numbers
    int lineNumberAreaWidth = drawLineNumber(painter, opt, textRect, index);
    textRect.adjust(lineNumberAreaWidth, 0, 0, 0);

    // show number of subresults in displayString
    QString displayString = index.model()->data(index, Qt::DisplayRole).toString();
    if (index.model()->hasChildren(index)) {
        displayString += QString::fromLatin1(" (")
                         + QString::number(index.model()->rowCount(index))
                         + QLatin1Char(')');
    }

    // text and focus/selection
    drawDisplay(painter, opt, textRect, displayString,
                index.model()->data(index, ItemDataRoles::SearchTermStartRole).toInt(),
                index.model()->data(index, ItemDataRoles::SearchTermLengthRole).toInt());
    QItemDelegate::drawFocus(painter, opt, opt.rect);

    // check mark
    if (checkable)
        QItemDelegate::drawCheck(painter, opt, checkRect, checkState);

    painter->restore();
}

// returns the width of the line number area
int SearchResultTreeItemDelegate::drawLineNumber(QPainter *painter, const QStyleOptionViewItemV3 &option,
                                                 const QRect &rect,
                                                 const QModelIndex &index) const
{
    static const int lineNumberAreaHorizontalPadding = 4;
    int lineNumber = index.model()->data(index, ItemDataRoles::ResultLineNumberRole).toInt();
    if (lineNumber < 1)
        return 0;
    const bool isSelected = option.state & QStyle::State_Selected;
    QString lineText = QString::number(lineNumber);
    int minimumLineNumberDigits = qMax((int)m_minimumLineNumberDigits, lineText.count());
    int fontWidth = painter->fontMetrics().width(QString(minimumLineNumberDigits, QLatin1Char('0')));
    int lineNumberAreaWidth = lineNumberAreaHorizontalPadding + fontWidth + lineNumberAreaHorizontalPadding;
    QRect lineNumberAreaRect(rect);
    lineNumberAreaRect.setWidth(lineNumberAreaWidth);

    QPalette::ColorGroup cg = QPalette::Normal;
    if (!(option.state & QStyle::State_Active))
        cg = QPalette::Inactive;
    else if (!(option.state & QStyle::State_Enabled))
        cg = QPalette::Disabled;

    painter->fillRect(lineNumberAreaRect, QBrush(isSelected ?
        option.palette.brush(cg, QPalette::Highlight) :
        option.palette.color(cg, QPalette::Base).darker(111)));

    QStyleOptionViewItemV3 opt = option;
    opt.displayAlignment = Qt::AlignRight | Qt::AlignVCenter;
    opt.palette.setColor(cg, QPalette::Text, Qt::darkGray);

    const QStyle *style = QApplication::style();
    const int textMargin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, 0, 0) + 1;

    const QRect rowRect = lineNumberAreaRect.adjusted(-textMargin, 0, textMargin-lineNumberAreaHorizontalPadding, 0);
    QItemDelegate::drawDisplay(painter, opt, rowRect, lineText);

    return lineNumberAreaWidth;
}

QSizeF SearchResultTreeItemDelegate::doTextLayout(int lineWidth) const
{
    QFontMetrics fontMetrics(textLayout.font());
    int leading = fontMetrics.leading();
    qreal height = 0;
    qreal widthUsed = 0;
    textLayout.beginLayout();
    while (true) {
        QTextLine line = textLayout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(lineWidth);
        height += leading;
        line.setPosition(QPointF(0, height));
        height += line.height();
        widthUsed = qMax(widthUsed, line.naturalTextWidth());
    }
    textLayout.endLayout();
    return QSizeF(widthUsed, height);
}

void SearchResultTreeItemDelegate::drawDisplay(QPainter *painter, const QStyleOptionViewItem &option,
                                               const QRect &rect, QString text,
                                               int searchTermStart, int searchTermLength) const
{
    if (text.isEmpty())
        return;

    QPen pen = painter->pen();
    QPalette::ColorGroup cg = option.state & QStyle::State_Enabled
                              ? QPalette::Normal : QPalette::Disabled;
    if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
        cg = QPalette::Inactive;
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(rect, option.palette.brush(cg, QPalette::Highlight));
        painter->setPen(option.palette.color(cg, QPalette::HighlightedText));
    } else {
        painter->setPen(option.palette.color(cg, QPalette::Text));
    }

    if (option.state & QStyle::State_Editing) {
        painter->save();
        painter->setPen(option.palette.color(cg, QPalette::Text));
        painter->drawRect(rect.adjusted(0, 0, -1, -1));
        painter->restore();
    }

    const QStyleOptionViewItemV2 opt = option;
    const int textMargin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1;
    QRect textRect = rect.adjusted(textMargin, 0, -textMargin, 0); // remove width padding
    const bool wrapText = opt.features & QStyleOptionViewItemV2::WrapText;
    textOption.setWrapMode(wrapText ? QTextOption::WordWrap : QTextOption::ManualWrap);
    textOption.setTextDirection(option.direction);
    textOption.setAlignment(QStyle::visualAlignment(option.direction, option.displayAlignment));
    textOption.setTabStop(option.fontMetrics.width(QLatin1Char(' ')) * 4);  ///TODO: make configurable!
    textLayout.setTextOption(textOption);
    textLayout.setFont(option.font);
    textLayout.setText(text.replace('\n', QChar::LineSeparator));

    QSizeF textLayoutSize = doTextLayout(textRect.width());

    if (textRect.width() < textLayoutSize.width() || textRect.height() < textLayoutSize.height()) {
        const QChar ellipsis(0x2026);   //TODO: may be "..." in some case (if not supported by font...)

        Qt::TextElideMode elideMode = option.textElideMode;
        if (option.direction == Qt::RightToLeft) {
            if (elideMode == Qt::ElideRight)
                elideMode = Qt::ElideLeft;
            else if (elideMode == Qt::ElideLeft)
                elideMode = Qt::ElideRight;
        }

        switch (elideMode) {
        case Qt::ElideRight: {
            int left = textLayout.lineAt(0)
                                 .xToCursor(textRect.width() - option.fontMetrics.width(ellipsis));
            textLayout.setText(text.left(left) + ellipsis);
            textLayoutSize = doTextLayout(textRect.width());
            }break;

        case Qt::ElideLeft: {
            //TODO: 'end' pos is not correct w/ multi-line: last line may not reach the end....
            int right = textLayout.lineAt(textLayout.lineCount()-1)
                                  .xToCursor(textLayoutSize.width() - textRect.width() + option.fontMetrics.width(ellipsis));
            textLayout.setText(ellipsis + text.mid(right));
            textLayoutSize = doTextLayout(textRect.width());
            searchTermStart -= right - 1;
            }break;

        case Qt::ElideMiddle: {
            int left = textLayout.lineAt(0)
                                 .xToCursor((textRect.width() - option.fontMetrics.width(ellipsis)) / 2);
            //TODO: 'end' pos is not correct w/ multi-line: last line may not reach the end....
            int right = textLayout.lineAt(textLayout.lineCount()-1)
                                  .xToCursor((textLayoutSize.width() - textRect.width() - option.fontMetrics.width(ellipsis)) / 2);
            textLayout.setText(text.left(left) + ellipsis + text.mid(right));
            textLayoutSize = doTextLayout(textRect.width());
            //TODO: update highlight range to match ellipsis
            }break;

        case Qt::ElideNone:
            break;
        }
    }

    textRect.setTop(textRect.top() + (textRect.height()/2) - (textLayoutSize.toSize().height()/2));

    if (searchTermStart >= 0 && searchTermStart < text.length() && searchTermLength >= 1 &&
        !(option.state & QStyle::State_Selected)) {
        QTextLayout::FormatRange range;
        range.start = searchTermStart;
        range.length = searchTermLength;
        range.format.setBackground(QBrush(qRgb(255, 240, 120)));
        textLayout.draw(painter, textRect.topLeft(), QVector<QTextLayout::FormatRange>() << range, textRect);
    }
    else
        textLayout.draw(painter, textRect.topLeft(), QVector<QTextLayout::FormatRange>(), textRect);
}
