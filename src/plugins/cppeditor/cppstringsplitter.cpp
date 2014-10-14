/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
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

#include "cppstringsplitter.h"
#include <QTextBlock>
#include <QTextCursor>
#include <texteditor/autocompleter.h>
#include <texteditor/basetexteditor.h>
#include <texteditor/texteditorsettings.h>

namespace CppEditor {
namespace Internal {

CppStringSplitter::CppStringSplitter(TextEditor::BaseTextEditorWidget *editorWidget)
    : m_editorWidget(editorWidget)
{}

bool CppStringSplitter::handleKeyPressEvent(QKeyEvent *e) const
{
    if (!TextEditor::TextEditorSettings::completionSettings().m_autoSplitStrings)
        return false;

    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        QTextCursor cursor = m_editorWidget->textCursor();

        if (m_editorWidget->autoCompleter()->isInString(cursor)) {
            cursor.beginEditBlock();
            if (cursor.positionInBlock() > 0
                    && cursor.block().text().at(cursor.positionInBlock() - 1) == QLatin1Char('\\')) {
                //Already escaped: simply go back to line, but do not indent
                cursor.insertText(QLatin1String("\n"));
            } else if (e->modifiers() & Qt::ShiftModifier) {
                //With 'shift' modifier, escape the end of line character and start at beginning of next line
                cursor.insertText(QLatin1String("\\\n"));
            } else {
                //End the current string, and start a new one on the line, properly indented
                cursor.insertText(QLatin1String("\"\n\""));
                m_editorWidget->baseTextDocument()->autoIndent(cursor);
            }
            cursor.endEditBlock();
            e->accept();
            return true;
        }
    }

    return false;
}

} // namespace Internal
} // namespace CppEditor
