/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "searchresultwidget.h"
#include "searchresulttreeview.h"
#include "searchresulttreemodel.h"
#include "searchresulttreeitems.h"
#include "searchresulttreeitemroles.h"

#include "ifindsupport.h"
#include "treeviewfind.h"

#include <aggregation/aggregate.h>
#include <coreplugin/icore.h>

#include <QDir>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace Find {
namespace Internal {

class WideEnoughLineEdit : public QLineEdit {
    Q_OBJECT
public:
    WideEnoughLineEdit(QWidget *parent):QLineEdit(parent){
        connect(this, SIGNAL(textChanged(QString)),
                this, SLOT(updateGeometry()));
    }
    ~WideEnoughLineEdit(){}
    QSize sizeHint() const {
        QSize sh = QLineEdit::minimumSizeHint();
        sh.rwidth() += qMax(25 * fontMetrics().width(QLatin1Char('x')),
                            fontMetrics().width(text()));
        return sh;
    }
public slots:
    void updateGeometry() { QLineEdit::updateGeometry(); }
};

} // Internal
} // Find

using namespace Find;
using namespace Find::Internal;

SearchResultWidget::SearchResultWidget(QWidget *parent) :
    QWidget(parent),
    m_count(0),
    m_isShowingReplaceUI(false),
    m_searchAgainSupported(false)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setMargin(0);
    layout->setSpacing(0);
    setLayout(layout);

    QFrame *topWidget = new QFrame;
    QPalette pal = topWidget->palette();
    pal.setColor(QPalette::Window, QColor(255, 255, 225));
    pal.setColor(QPalette::WindowText, Qt::black);
    topWidget->setPalette(pal);
    topWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
    topWidget->setLineWidth(1);
    topWidget->setAutoFillBackground(true);
    QHBoxLayout *topLayout = new QHBoxLayout(topWidget);
    topLayout->setMargin(2);
    topWidget->setLayout(topLayout);

    m_searchResultTreeView = new Internal::SearchResultTreeView(this);
    m_searchResultTreeView->setFrameStyle(QFrame::NoFrame);
    m_searchResultTreeView->setAttribute(Qt::WA_MacShowFocusRect, false);
    Aggregation::Aggregate * agg = new Aggregation::Aggregate;
    agg->add(m_searchResultTreeView);
    agg->add(new TreeViewFind(m_searchResultTreeView,
                              ItemDataRoles::ResultLineRole));

    layout->addWidget(topWidget);
    layout->addWidget(m_searchResultTreeView);

    m_infoBarDisplay.setTarget(layout, 1);
    m_infoBarDisplay.setInfoBar(&m_infoBar);

    m_descriptionContainer = new QWidget(topWidget);
    QHBoxLayout *descriptionLayout = new QHBoxLayout(m_descriptionContainer);
    m_descriptionContainer->setLayout(descriptionLayout);
    descriptionLayout->setMargin(0);
    m_descriptionContainer->setMinimumWidth(200);
    m_descriptionContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_label = new QLabel(m_descriptionContainer);
    m_label->setVisible(false);
    m_searchTerm = new QLabel(m_descriptionContainer);
    m_searchTerm->setVisible(false);
    descriptionLayout->addWidget(m_label);
    descriptionLayout->addWidget(m_searchTerm);
    m_cancelButton = new QToolButton(topWidget);
    m_cancelButton->setText(tr("Cancel"));
    m_cancelButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_cancelButton, SIGNAL(clicked()), this, SLOT(cancel()));
    m_searchAgainButton = new QToolButton(topWidget);
    m_searchAgainButton->setToolTip(tr("Repeat the search with same parameters"));
    m_searchAgainButton->setText(tr("Search again"));
    m_searchAgainButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_searchAgainButton->setVisible(false);
    connect(m_searchAgainButton, SIGNAL(clicked()), this, SLOT(searchAgain()));

    m_replaceLabel = new QLabel(tr("Replace with:"), topWidget);
    m_replaceTextEdit = new WideEnoughLineEdit(topWidget);
    m_replaceTextEdit->setMinimumWidth(120);
    m_replaceTextEdit->setEnabled(false);
    m_replaceTextEdit->setTabOrder(m_replaceTextEdit, m_searchResultTreeView);
    m_replaceButton = new QToolButton(topWidget);
    m_replaceButton->setToolTip(tr("Replace all occurrences"));
    m_replaceButton->setText(tr("Replace"));
    m_replaceButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_replaceButton->setEnabled(false);
    m_preserveCaseCheck = new QCheckBox(topWidget);
    m_preserveCaseCheck->setText(tr("Preserve case"));
    m_preserveCaseCheck->setEnabled(false);

    m_matchesFoundLabel = new QLabel(topWidget);
    updateMatchesFoundLabel();

    topLayout->addWidget(m_descriptionContainer);
    topLayout->addWidget(m_cancelButton);
    topLayout->addWidget(m_searchAgainButton);
    topLayout->addWidget(m_replaceLabel);
    topLayout->addWidget(m_replaceTextEdit);
    topLayout->addWidget(m_replaceButton);
    topLayout->addWidget(m_preserveCaseCheck);
    topLayout->addStretch(2);
    topLayout->addWidget(m_matchesFoundLabel);
    topWidget->setMinimumHeight(m_cancelButton->sizeHint().height()
                                + topLayout->contentsMargins().top() + topLayout->contentsMargins().bottom()
                                + topWidget->lineWidth());
    setShowReplaceUI(false);

    connect(m_searchResultTreeView, SIGNAL(jumpToSearchResult(SearchResultItem)),
            this, SLOT(handleJumpToSearchResult(SearchResultItem)));
    connect(m_replaceTextEdit, SIGNAL(returnPressed()), this, SLOT(handleReplaceButton()));
    connect(m_replaceButton, SIGNAL(clicked()), this, SLOT(handleReplaceButton()));
}

void SearchResultWidget::setInfo(const QString &label, const QString &toolTip, const QString &term)
{
    m_label->setText(label);
    m_label->setVisible(!label.isEmpty());
    m_descriptionContainer->setToolTip(toolTip);
    m_searchTerm->setText(term);
    m_searchTerm->setVisible(!term.isEmpty());
}

void SearchResultWidget::addResult(const QString &fileName, int lineNumber, const QString &rowText,
    int searchTermStart, int searchTermLength, const QVariant &userData)
{
    SearchResultItem item;
    item.path = QStringList() << QDir::toNativeSeparators(fileName);
    item.lineNumber = lineNumber;
    item.text = rowText;
    item.textMarkPos = searchTermStart;
    item.textMarkLength = searchTermLength;
    item.useTextEditorFont = true;
    item.userData = userData;
    addResults(QList<SearchResultItem>() << item, SearchResult::AddOrdered);
}

void SearchResultWidget::addResults(const QList<SearchResultItem> &items, SearchResult::AddMode mode)
{
    bool firstItems = (m_count == 0);
    m_count += items.size();
    m_searchResultTreeView->addResults(items, mode);
    if (firstItems) {
        if (showWarningMessage()) {
            Core::InfoBarEntry info(QLatin1String("warninglabel"), tr("This change cannot be undone."));
            info.setCustomButtonInfo(tr("Do not warn again"), this, SLOT(hideNoUndoWarning()));
            m_infoBar.addInfo(info);
        }

        m_replaceTextEdit->setEnabled(true);
        // We didn't have an item before, set the focus to the search widget or replace text edit
        if (m_isShowingReplaceUI) {
            m_replaceTextEdit->setFocus();
            m_replaceTextEdit->selectAll();
        } else {
            m_searchResultTreeView->setFocus();
        }
        m_searchResultTreeView->selectionModel()->select(m_searchResultTreeView->model()->index(0, 0, QModelIndex()), QItemSelectionModel::Select);
        emit navigateStateChanged();
    }
    updateMatchesFoundLabel();
}

int SearchResultWidget::count() const
{
    return m_count;
}

QString SearchResultWidget::dontAskAgainGroup() const
{
    return m_dontAskAgainGroup;
}

void SearchResultWidget::setDontAskAgainGroup(const QString &group)
{
    m_dontAskAgainGroup = group;
}


void SearchResultWidget::setTextToReplace(const QString &textToReplace)
{
    m_replaceTextEdit->setText(textToReplace);
}

QString SearchResultWidget::textToReplace() const
{
    return m_replaceTextEdit->text();
}

void SearchResultWidget::setShowReplaceUI(bool visible)
{
    m_searchResultTreeView->model()->setShowReplaceUI(visible);
    m_replaceLabel->setVisible(visible);
    m_replaceTextEdit->setVisible(visible);
    m_replaceButton->setVisible(visible);
    m_preserveCaseCheck->setVisible(visible);
    m_isShowingReplaceUI = visible;
}

bool SearchResultWidget::hasFocusInternally() const
{
    return m_searchResultTreeView->hasFocus() || (m_isShowingReplaceUI && m_replaceTextEdit->hasFocus());
}

void SearchResultWidget::setFocusInternally()
{
    if (m_count > 0) {
        if (m_isShowingReplaceUI) {
            if (!focusWidget() || focusWidget() == m_replaceTextEdit) {
                m_replaceTextEdit->setFocus();
                m_replaceTextEdit->selectAll();
            } else {
                m_searchResultTreeView->setFocus();
            }
        } else {
            m_searchResultTreeView->setFocus();
        }
    }
}

bool SearchResultWidget::canFocusInternally() const
{
    return m_count > 0;
}

void SearchResultWidget::notifyVisibilityChanged(bool visible)
{
    emit visibilityChanged(visible);
}

void SearchResultWidget::setTextEditorFont(const QFont &font)
{
    m_searchResultTreeView->setTextEditorFont(font);
}

void SearchResultWidget::setAutoExpandResults(bool expand)
{
    m_searchResultTreeView->setAutoExpandResults(expand);
}

void SearchResultWidget::expandAll()
{
    m_searchResultTreeView->expandAll();
}

void SearchResultWidget::collapseAll()
{
    m_searchResultTreeView->collapseAll();
}

void SearchResultWidget::goToNext()
{
    if (m_count == 0)
        return;
    QModelIndex idx = m_searchResultTreeView->model()->next(m_searchResultTreeView->currentIndex());
    if (idx.isValid()) {
        m_searchResultTreeView->setCurrentIndex(idx);
        m_searchResultTreeView->emitJumpToSearchResult(idx);
    }
}

void SearchResultWidget::goToPrevious()
{
    if (!m_searchResultTreeView->model()->rowCount())
        return;
    QModelIndex idx = m_searchResultTreeView->model()->prev(m_searchResultTreeView->currentIndex());
    if (idx.isValid()) {
        m_searchResultTreeView->setCurrentIndex(idx);
        m_searchResultTreeView->emitJumpToSearchResult(idx);
    }
}

void SearchResultWidget::restart()
{
    m_replaceTextEdit->setEnabled(false);
    m_replaceButton->setEnabled(false);
    m_searchResultTreeView->clear();
    m_count = 0;
    m_cancelButton->setVisible(true);
    m_searchAgainButton->setVisible(false);
    updateMatchesFoundLabel();
    emit restarted();
}

void SearchResultWidget::setSearchAgainSupported(bool supported)
{
    m_searchAgainSupported = supported;
    m_searchAgainButton->setVisible(supported && !m_cancelButton->isVisible());
}

void SearchResultWidget::setSearchAgainEnabled(bool enabled)
{
    m_searchAgainButton->setEnabled(enabled);
}

void SearchResultWidget::finishSearch()
{
    m_replaceTextEdit->setEnabled(m_count > 0);
    m_replaceButton->setEnabled(m_count > 0);
    m_preserveCaseCheck->setEnabled(m_count > 0);
    m_cancelButton->setVisible(false);
    m_searchAgainButton->setVisible(m_searchAgainSupported);
}

void SearchResultWidget::hideNoUndoWarning()
{
    setShowWarningMessage(false);
    m_infoBar.clear();
}

void SearchResultWidget::handleJumpToSearchResult(const SearchResultItem &item)
{
    emit activated(item);
}

void SearchResultWidget::handleReplaceButton()
{
    // check if button is actually enabled, because this is also triggered
    // by pressing return in replace line edit
    if (m_replaceButton->isEnabled()) {
        m_infoBar.clear();
        emit replaceButtonClicked(m_replaceTextEdit->text(), checkedItems(), m_preserveCaseCheck->isChecked());
    }
}

void SearchResultWidget::cancel()
{
    m_cancelButton->setVisible(false);
    emit cancelled();
}

void SearchResultWidget::searchAgain()
{
    emit searchAgainRequested();
}

bool SearchResultWidget::showWarningMessage() const
{
    if (m_dontAskAgainGroup.isEmpty())
        return false;
    // read settings
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(m_dontAskAgainGroup);
    settings->beginGroup(QLatin1String("Rename"));
    const bool showWarningMessage = settings->value(QLatin1String("ShowWarningMessage"), true).toBool();
    settings->endGroup();
    settings->endGroup();
    return showWarningMessage;
}

void SearchResultWidget::setShowWarningMessage(bool showWarningMessage)
{
    // write to settings
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(m_dontAskAgainGroup);
    settings->beginGroup(QLatin1String("Rename"));
    settings->setValue(QLatin1String("ShowWarningMessage"), showWarningMessage);
    settings->endGroup();
    settings->endGroup();
}

QList<SearchResultItem> SearchResultWidget::checkedItems() const
{
    QList<SearchResultItem> result;
    Internal::SearchResultTreeModel *model = m_searchResultTreeView->model();
    const int fileCount = model->rowCount(QModelIndex());
    for (int i = 0; i < fileCount; ++i) {
        QModelIndex fileIndex = model->index(i, 0, QModelIndex());
        Internal::SearchResultTreeItem *fileItem = static_cast<Internal::SearchResultTreeItem *>(fileIndex.internalPointer());
        Q_ASSERT(fileItem != 0);
        for (int rowIndex = 0; rowIndex < fileItem->childrenCount(); ++rowIndex) {
            QModelIndex textIndex = model->index(rowIndex, 0, fileIndex);
            Internal::SearchResultTreeItem *rowItem = static_cast<Internal::SearchResultTreeItem *>(textIndex.internalPointer());
            if (rowItem->checkState())
                result << rowItem->item;
        }
    }
    return result;
}

void SearchResultWidget::updateMatchesFoundLabel()
{
    if (m_count == 0)
        m_matchesFoundLabel->setText(tr("No matches found."));
    else
        m_matchesFoundLabel->setText(tr("%n matches found.", 0, m_count));
}

#include "searchresultwidget.moc"
