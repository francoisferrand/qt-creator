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

#include "searchresulttreeview.h"
#include "searchresulttreeitemroles.h"
#include "searchresulttreemodel.h"
#include "searchresulttreeitemdelegate.h"

#include <texteditor/icodestylepreferences.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditorsettings.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/editorconfiguration.h>

#include <QHeaderView>
#include <QKeyEvent>

using namespace Find::Internal;

static TextEditor::ICodeStylePreferences *getCodeStylePreferences(Core::Id languageId)
{
    ProjectExplorer::Project *project = ProjectExplorer::ProjectExplorerPlugin::currentProject();
    if (!project)
        return TextEditor::TextEditorSettings::instance()->codeStyle(languageId);

    ProjectExplorer::EditorConfiguration *editorConfiguration = project->editorConfiguration();
    return editorConfiguration->codeStyle(languageId);
}

SearchResultTreeView::SearchResultTreeView(QWidget *parent, Core::Id languageId)
    : QTreeView(parent)
    , m_model(new SearchResultTreeModel(this))
    , m_autoExpandResults(false)
{
    TextEditor::ICodeStylePreferences *codeStylePref = getCodeStylePreferences(languageId);

    setModel(m_model);
    setItemDelegate(new SearchResultTreeItemDelegate(codeStylePref->currentTabSettings().m_tabSize, this));
    setIndentation(14);
    setUniformRowHeights(true);
    setExpandsOnDoubleClick(true);
    header()->hide();

    connect(this, SIGNAL(activated(QModelIndex)), this, SLOT(emitJumpToSearchResult(QModelIndex)));
    connect(codeStylePref, SIGNAL(currentTabSettingsChanged(TextEditor::TabSettings)),
            this, SLOT(tabSettingsChanged(TextEditor::TabSettings)));
}

void SearchResultTreeView::setAutoExpandResults(bool expand)
{
    m_autoExpandResults = expand;
}

void SearchResultTreeView::setTextEditorFont(const QFont &font, const SearchResultColor color)
{
    m_model->setTextEditorFont(font, color);

    QPalette p = palette();
    p.setColor(QPalette::Base, color.textBackground);
    setPalette(p);
}

void SearchResultTreeView::clear()
{
    m_model->clear();
}

void SearchResultTreeView::addResults(const QList<Find::SearchResultItem> &items, Find::SearchResult::AddMode mode)
{
    QList<QModelIndex> addedParents = m_model->addResults(items, mode);
    if (m_autoExpandResults && !addedParents.isEmpty()) {
        foreach (const QModelIndex &index, addedParents)
            setExpanded(index, true);
    }
}

void SearchResultTreeView::emitJumpToSearchResult(const QModelIndex &index)
{
    if (model()->data(index, ItemDataRoles::IsGeneratedRole).toBool())
        return;
    SearchResultItem item = model()->data(index, ItemDataRoles::ResultItemRole).value<SearchResultItem>();

    emit jumpToSearchResult(item);
}

void SearchResultTreeView::tabSettingsChanged(const TextEditor::TabSettings &ts)
{
    SearchResultTreeItemDelegate *delegate = static_cast<SearchResultTreeItemDelegate *>(itemDelegate());
    delegate->setTabWidth(ts.m_tabSize);
}

void SearchResultTreeView::keyPressEvent(QKeyEvent *e)
{
    if (!e->modifiers() && e->key() == Qt::Key_Return) {
        emit activated(currentIndex());
        e->accept();
        return;
    }
    QTreeView::keyPressEvent(e);
}

SearchResultTreeModel *SearchResultTreeView::model() const
{
    return m_model;
}
