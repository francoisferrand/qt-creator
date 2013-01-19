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

#include "cppelementevaluator.h"

#include <coreplugin/idocument.h>
#include <cpptools/cpptoolsreuse.h>

#include <FullySpecifiedType.h>
#include <Literals.h>
#include <Names.h>
#include <CoreTypes.h>
#include <Scope.h>
#include <Symbol.h>
#include <Symbols.h>
#include <Literals.h>
#include <cpptools/TypeHierarchyBuilder.h>
#include <cpptools/ModelManagerInterface.h>
#include <cplusplus/ExpressionUnderCursor.h>
#include <cplusplus/Overview.h>
#include <cplusplus/TypeOfExpression.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/LookupItem.h>
#include <cplusplus/Icons.h>
#include <cplusplus/AST.h>
#include <cplusplus/ASTVisitor.h>
#include <cplusplus/TranslationUnit.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QQueue>

using namespace CppEditor;
using namespace Internal;
using namespace CPlusPlus;

namespace {
    QStringList stripName(const QString &name) {
        QStringList all;
        all << name;
        int colonColon = 0;
        const int size = name.size();
        while ((colonColon = name.indexOf(QLatin1String("::"), colonColon)) != -1) {
            all << name.right(size - colonColon - 2);
            colonColon += 2;
        }
        return all;
    }

    class ConstantExpressionEvaluator : public ASTVisitor {
    public:
        static bool eval(int * result, const QString& expression)
        {
            Document::Ptr doc = Document::create(QLatin1String("<ConstantExpressionEvaluator>"));
            doc->setUtf8Source(expression.toUtf8());
            if (doc->parse(Document::ParseExpression)) {
                ConstantExpressionEvaluator evaluator(doc->translationUnit());
                evaluator.accept(doc->translationUnit()->ast());
                *result = evaluator._result;
                return !evaluator._error;
            }
            return false;
        }

    private:
        int _result;
        bool _error;

        ConstantExpressionEvaluator(TranslationUnit * translationUnit):
            ASTVisitor(translationUnit), _result(0), _error(false)
        {}

        virtual bool preVisit(AST *)
        {
            return !_error;
        }

        virtual bool visit(BoolLiteralAST * ast)
        {
            _result = tokenKind(ast->literal_token) == T_TRUE ? 1 : 0;
            return false;
        }

        virtual bool visit(NumericLiteralAST * ast)
        {
            bool ok;
            _result = QString(tokenAt(ast->literal_token).spell()).toInt(&ok, 0);
            if (!ok)
                _error = true;
            return false;
        }

        virtual bool visit(PointerLiteralAST * ast)
        {
            _result = tokenKind(ast->literal_token) == T_NULLPTR ? 1 : 0;
            return false;
        }

        virtual bool visit(StringLiteralAST * /*ast*/)
        {
            _result = 1;
            return false;
        }

        virtual bool visit(UnaryExpressionAST * ast)
        {
            accept(ast->expression);
            switch (tokenKind(ast->unary_op_token)) {
            case T_TILDE:       _result = ~_result; break;
            case T_EXCLAIM:     _result = !_result; break;
            default:            _error = true; break;
            }
            return false;
        }

        virtual bool visit(BinaryExpressionAST * ast)
        {
            accept(ast->left_expression);
            int leftResult = _result;
            accept(ast->right_expression);
            int rightResult = _result;
            switch (tokenKind(ast->binary_op_token)) {
            case T_PLUS:            _result = leftResult + rightResult; break;
            case T_MINUS:           _result = leftResult - rightResult; break;
            case T_STAR:            _result = leftResult * rightResult; break;
            case T_SLASH:           _result = leftResult / rightResult; break;
            case T_PERCENT:         _result = leftResult % rightResult; break;
            case T_CARET:           _result = leftResult ^ rightResult; break;
            case T_AMPER:           _result = leftResult & rightResult; break;
            case T_PIPE:            _result = leftResult | rightResult; break;
            case T_LESS:            _result = leftResult < rightResult; break;
            case T_GREATER:         _result = leftResult > rightResult; break;
            case T_LESS_LESS:       _result = leftResult << rightResult; break;
            case T_GREATER_GREATER: _result = leftResult >> rightResult; break;
            case T_EXCLAIM_EQUAL:   _result = leftResult != rightResult; break;
            case T_LESS_EQUAL:      _result = leftResult <= rightResult; break;
            case T_GREATER_EQUAL:   _result = leftResult >= rightResult; break;
            case T_AMPER_AMPER:     _result = leftResult && rightResult; break;
            case T_PIPE_PIPE:       _result = leftResult || rightResult; break;
            default:                _error = true; break;
            }
            return false;
        }

        virtual bool visit(CompoundExpressionAST * ast)
        {
            accept(ast->statement->statement_list->lastValue());
            return false;
        }

        virtual bool visit(ConditionalExpressionAST * ast)
        {
            // evaluate condition, then return evaluation of appropriate side
            accept(ast->condition);
            accept(_result ? ast->left_expression : ast->right_expression);
            return false;
        }

        virtual bool visit(CastExpressionAST *)     { _error = true; return true; }
        virtual bool visit(CppCastExpressionAST *)  { _error = true; return true; }
        virtual bool visit(SizeofExpressionAST *)   { _error = true; return true; }
    };
}

CppElementEvaluator::CppElementEvaluator(CPPEditorWidget *editor) :
    m_editor(editor),
    m_modelManager(CppModelManagerInterface::instance()),
    m_tc(editor->textCursor()),
    m_lookupBaseClasses(false),
    m_lookupDerivedClasses(false)
{}

void CppElementEvaluator::setTextCursor(const QTextCursor &tc)
{ m_tc = tc; }

void CppElementEvaluator::setLookupBaseClasses(const bool lookup)
{ m_lookupBaseClasses = lookup; }

void CppElementEvaluator::setLookupDerivedClasses(const bool lookup)
{ m_lookupDerivedClasses = lookup; }

// @todo: Consider refactoring code from CPPEditor::findLinkAt into here.
void CppElementEvaluator::execute()
{
    clear();

    if (!m_modelManager)
        return;

    const Snapshot &snapshot = m_modelManager->snapshot();
    Document::Ptr doc = snapshot.document(m_editor->editorDocument()->fileName());
    if (!doc)
        return;

    int line = 0;
    int column = 0;
    const int pos = m_tc.position();
    m_editor->convertPosition(pos, &line, &column);

    checkDiagnosticMessage(pos);

    if (!matchIncludeFile(doc, line) && !matchMacroInUse(doc, pos)) {
        CppTools::moveCursorToEndOfIdentifier(&m_tc);

        // Fetch the expression's code
        ExpressionUnderCursor expressionUnderCursor;
        const QString &expression = expressionUnderCursor(m_tc);
        Scope *scope = doc->scopeAt(line, column);

        TypeOfExpression typeOfExpression;
        typeOfExpression.init(doc, snapshot);
        // make possible to instantiate templates
        typeOfExpression.setExpandTemplates(true);
        const QList<LookupItem> &lookupItems = typeOfExpression(expression.toUtf8(), scope);
        if (lookupItems.isEmpty())
            return;

        const LookupItem &lookupItem = lookupItems.first(); // ### TODO: select best candidate.
        handleLookupItemMatch(snapshot, lookupItem, typeOfExpression.context());
    }
}

void CppElementEvaluator::checkDiagnosticMessage(int pos)
{
    foreach (const QTextEdit::ExtraSelection &sel,
             m_editor->extraSelections(TextEditor::BaseTextEditorWidget::CodeWarningsSelection)) {
        if (pos >= sel.cursor.selectionStart() && pos <= sel.cursor.selectionEnd()) {
            m_diagnosis = sel.format.toolTip();
            break;
        }
    }
}

bool CppElementEvaluator::matchIncludeFile(const CPlusPlus::Document::Ptr &document, unsigned line)
{
    foreach (const Document::Include &includeFile, document->includes()) {
        if (includeFile.line() == line) {
            m_element = QSharedPointer<CppElement>(new CppInclude(includeFile));
            return true;
        }
    }
    return false;
}

bool CppElementEvaluator::matchMacroInUse(const CPlusPlus::Document::Ptr &document, unsigned pos)
{
    foreach (const Document::MacroUse &use, document->macroUses()) {
        if (use.contains(pos)) {
            const unsigned begin = use.begin();
            if (pos < begin + use.macro().name().length()) {
                m_element = QSharedPointer<CppElement>(new CppMacro(use.macro()));
                return true;
            }
        }
    }
    return false;
}

void CppElementEvaluator::handleLookupItemMatch(const Snapshot &snapshot,
                                                const LookupItem &lookupItem,
                                                const LookupContext &context)
{
    Symbol *declaration = lookupItem.declaration();
    if (!declaration) {
        const QString &type = Overview().prettyType(lookupItem.type(), QString());
        m_element = QSharedPointer<CppElement>(new Unknown(type));
    } else {
        const FullySpecifiedType &type = declaration->type();
        if (declaration->isNamespace()) {
            m_element = QSharedPointer<CppElement>(new CppNamespace(declaration));
        } else if (declaration->isClass()
                   || declaration->isForwardClassDeclaration()
                   || (declaration->isTemplate() && declaration->asTemplate()->declaration()
                       && (declaration->asTemplate()->declaration()->isClass()
                           || declaration->asTemplate()->declaration()->isForwardClassDeclaration()))) {
            if (declaration->isForwardClassDeclaration())
                if (Symbol *classDeclaration =
                        m_symbolFinder.findMatchingClassDeclaration(declaration, snapshot)) {
                    declaration = classDeclaration;
                }
            CppClass *cppClass = new CppClass(declaration);
            if (m_lookupBaseClasses)
                cppClass->lookupBases(declaration, context);
            if (m_lookupDerivedClasses)
                cppClass->lookupDerived(declaration, snapshot);
            m_element = QSharedPointer<CppElement>(cppClass);
        } else if (Enum *enumDecl = declaration->asEnum()) {
            m_element = QSharedPointer<CppElement>(new CppEnum(enumDecl));
        } else if (EnumeratorDeclaration *enumerator = dynamic_cast<EnumeratorDeclaration *>(declaration)) {
            m_element = QSharedPointer<CppElement>(new CppEnumerator(enumerator));
        } else if (declaration->isTypedef()) {
            m_element = QSharedPointer<CppElement>(new CppTypedef(declaration));
        } else if (declaration->isFunction()
                   || (type.isValid() && type->isFunctionType())
                   || declaration->isTemplate()) {
            m_element = QSharedPointer<CppElement>(new CppFunction(declaration));
        } else if (declaration->isDeclaration() && type.isValid()) {
            m_element = QSharedPointer<CppElement>(
                new CppVariable(declaration, context, lookupItem.scope()));
        } else {
            m_element = QSharedPointer<CppElement>(new CppDeclarableElement(declaration));
        }
    }
}

bool CppElementEvaluator::identifiedCppElement() const
{
    return !m_element.isNull();
}

const QSharedPointer<CppElement> &CppElementEvaluator::cppElement() const
{
    return m_element;
}

bool CppElementEvaluator::hasDiagnosis() const
{
    return !m_diagnosis.isEmpty();
}

const QString &CppElementEvaluator::diagnosis() const
{
    return m_diagnosis;
}

void CppEditor::Internal::CppElementEvaluator::clear()
{
    m_element.clear();
    m_diagnosis.clear();
}

// CppElement
CppElement::CppElement() : m_helpCategory(TextEditor::HelpItem::Unknown)
{}

CppElement::~CppElement()
{}

void CppElement::setHelpCategory(const TextEditor::HelpItem::Category &cat)
{ m_helpCategory = cat; }

const TextEditor::HelpItem::Category &CppElement::helpCategory() const
{ return m_helpCategory; }

void CppElement::setHelpIdCandidates(const QStringList &candidates)
{ m_helpIdCandidates = candidates; }

void CppElement::addHelpIdCandidate(const QString &candidate)
{ m_helpIdCandidates.append(candidate); }

const QStringList &CppElement::helpIdCandidates() const
{ return m_helpIdCandidates; }

void CppElement::setHelpMark(const QString &mark)
{ m_helpMark = mark; }

const QString &CppElement::helpMark() const
{ return m_helpMark; }

void CppElement::setLink(const CPPEditorWidget::Link &link)
{ m_link = link; }

const CPPEditorWidget::Link &CppElement::link() const
{ return m_link; }

void CppElement::setTooltip(const QString &tooltip)
{ m_tooltip = tooltip; }

const QString &CppElement::tooltip() const
{ return m_tooltip; }


// Unknown
Unknown::Unknown(const QString &type) : CppElement(), m_type(type)
{
    setTooltip(m_type);
}

Unknown::~Unknown()
{}

const QString &Unknown::type() const
{ return m_type; }

// CppInclude
CppInclude::~CppInclude()
{}

CppInclude::CppInclude(const Document::Include &includeFile) :
    CppElement(),
    m_path(QDir::toNativeSeparators(includeFile.fileName())),
    m_fileName(QFileInfo(includeFile.fileName()).fileName())
{
    setHelpCategory(TextEditor::HelpItem::Brief);
    setHelpIdCandidates(QStringList(m_fileName));
    setHelpMark(m_fileName);
    setLink(CPPEditorWidget::Link(m_path));
    setTooltip(m_path);
}

const QString &CppInclude::path() const
{ return m_path; }

const QString &CppInclude::fileName() const
{ return m_fileName; }

// CppMacro
CppMacro::CppMacro(const Macro &macro) : CppElement()
{
    setHelpCategory(TextEditor::HelpItem::Macro);
    const QString macroName = QLatin1String(macro.name());
    setHelpIdCandidates(QStringList(macroName));
    setHelpMark(macroName);
    setLink(CPPEditorWidget::Link(macro.fileName(), macro.line()));
    setTooltip(macro.toStringWithLineBreaks());
}

CppMacro::~CppMacro()
{}

// CppDeclarableElement
CppDeclarableElement::CppDeclarableElement()
{}

CppDeclarableElement::CppDeclarableElement(Symbol *declaration) : CppElement()
{
    m_icon = Icons().iconForSymbol(declaration);

    Overview overview;
    overview.setShowArgumentNames(true);
    overview.setShowReturnTypes(true);
    m_name = overview.prettyName(declaration->name());
    if (declaration->enclosingScope()->isClass() ||
        declaration->enclosingScope()->isNamespace() ||
        declaration->enclosingScope()->isEnum()) {
        m_qualifiedName = overview.prettyName(LookupContext::fullyQualifiedName(declaration));
        setHelpIdCandidates(stripName(m_qualifiedName));
    } else {
        m_qualifiedName = m_name;
        setHelpIdCandidates(QStringList(m_name));
    }

    setTooltip(overview.prettyType(declaration->type(), m_qualifiedName));
    setLink(CPPEditorWidget::linkToSymbol(declaration));
    setHelpMark(m_name);
}

CppDeclarableElement::~CppDeclarableElement()
{}

void CppDeclarableElement::setName(const QString &name)
{ m_name = name; }

const QString &CppDeclarableElement::name() const
{ return m_name; }

void CppDeclarableElement::setQualifiedName(const QString &name)
{ m_qualifiedName = name; }

const QString &CppDeclarableElement::qualifiedName() const
{ return m_qualifiedName; }

void CppDeclarableElement::setType(const QString &type)
{ m_type = type; }

const QString &CppDeclarableElement::type() const
{ return m_type; }

void CppDeclarableElement::setIcon(const QIcon &icon)
{ m_icon = icon; }

const QIcon &CppDeclarableElement::icon() const
{ return m_icon; }

// CppNamespace
CppNamespace::CppNamespace(Symbol *declaration) : CppDeclarableElement(declaration)
{
    setHelpCategory(TextEditor::HelpItem::ClassOrNamespace);
    setTooltip(qualifiedName());
}

CppNamespace::~CppNamespace()
{}

// CppClass
CppClass::CppClass()
{}

CppClass::CppClass(Symbol *declaration) : CppDeclarableElement(declaration)
{
    setHelpCategory(TextEditor::HelpItem::ClassOrNamespace);
    setTooltip(qualifiedName());
}

CppClass::~CppClass()
{}

void CppClass::lookupBases(Symbol *declaration, const CPlusPlus::LookupContext &context)
{
    typedef QPair<ClassOrNamespace *, CppClass *> Data;

    if (ClassOrNamespace *clazz = context.lookupType(declaration)) {
        QSet<ClassOrNamespace *> visited;

        QQueue<Data> q;
        q.enqueue(qMakePair(clazz, this));
        while (!q.isEmpty()) {
            Data current = q.dequeue();
            clazz = current.first;
            visited.insert(clazz);
            const QList<ClassOrNamespace *> &bases = clazz->usings();
            foreach (ClassOrNamespace *baseClass, bases) {
                const QList<Symbol *> &symbols = baseClass->symbols();
                foreach (Symbol *symbol, symbols) {
                    if (symbol->isClass() && (
                        clazz = context.lookupType(symbol)) &&
                        !visited.contains(clazz)) {
                        CppClass baseCppClass(symbol);
                        CppClass *cppClass = current.second;
                        cppClass->m_bases.append(baseCppClass);
                        q.enqueue(qMakePair(clazz, &cppClass->m_bases.last()));
                    }
                }
            }
        }
    }
}

void CppClass::lookupDerived(CPlusPlus::Symbol *declaration, const CPlusPlus::Snapshot &snapshot)
{
    typedef QPair<CppClass *, TypeHierarchy> Data;

    TypeHierarchyBuilder builder(declaration, snapshot);
    const TypeHierarchy &completeHierarchy = builder.buildDerivedTypeHierarchy();

    QQueue<Data> q;
    q.enqueue(qMakePair(this, completeHierarchy));
    while (!q.isEmpty()) {
        const Data &current = q.dequeue();
        CppClass *clazz = current.first;
        const TypeHierarchy &classHierarchy = current.second;
        foreach (const TypeHierarchy &derivedHierarchy, classHierarchy.hierarchy()) {
            clazz->m_derived.append(CppClass(derivedHierarchy.symbol()));
            q.enqueue(qMakePair(&clazz->m_derived.last(), derivedHierarchy));
        }
    }
}

const QList<CppClass> &CppClass::bases() const
{ return m_bases; }

const QList<CppClass> &CppClass::derived() const
{ return m_derived; }

// CppFunction
CppFunction::CppFunction(Symbol *declaration) : CppDeclarableElement(declaration)
{
    setHelpCategory(TextEditor::HelpItem::Function);

    const FullySpecifiedType &type = declaration->type();

    // Functions marks can be found either by the main overload or signature based
    // (with no argument names and no return). Help ids have no signature at all.
    Overview overview;
    overview.setShowDefaultArguments(false);
    setHelpMark(overview.prettyType(type, name()));

    overview.setShowFunctionSignatures(false);
    addHelpIdCandidate(overview.prettyName(declaration->name()));
}

CppFunction::~CppFunction()
{}

// CppEnum
CppEnum::CppEnum(Enum *declaration)
    : CppDeclarableElement(declaration)
{
    setHelpCategory(TextEditor::HelpItem::Enum);
    setTooltip(qualifiedName());
}

CppEnum::~CppEnum()
{}

// CppTypedef
CppTypedef::CppTypedef(Symbol *declaration) : CppDeclarableElement(declaration)
{
    setHelpCategory(TextEditor::HelpItem::Typedef);
    setTooltip(Overview().prettyType(declaration->type(), qualifiedName()));
}

CppTypedef::~CppTypedef()
{}

// CppVariable
CppVariable::CppVariable(Symbol *declaration, const LookupContext &context, Scope *scope) :
    CppDeclarableElement(declaration)
{
    const FullySpecifiedType &type = declaration->type();

    const Name *typeName = 0;
    if (type->isNamedType()) {
        typeName = type->asNamedType()->name();
    } else if (type->isPointerType() || type->isReferenceType()) {
        FullySpecifiedType associatedType;
        if (type->isPointerType())
            associatedType = type->asPointerType()->elementType();
        else
            associatedType = type->asReferenceType()->elementType();
        if (associatedType->isNamedType())
            typeName = associatedType->asNamedType()->name();
    }

    if (typeName) {
        if (ClassOrNamespace *clazz = context.lookupType(typeName, scope)) {
            if (!clazz->symbols().isEmpty()) {
                Overview overview;
                Symbol *symbol = clazz->symbols().at(0);
                const QString &name =
                    overview.prettyName(LookupContext::fullyQualifiedName(symbol));
                if (!name.isEmpty()) {
                    setTooltip(name);
                    setHelpCategory(TextEditor::HelpItem::ClassOrNamespace);
                    const QStringList &allNames = stripName(name);
                    if (!allNames.isEmpty()) {
                        setHelpMark(allNames.last());
                        setHelpIdCandidates(allNames);
                    }
                }
            }
        }
    }
}

CppVariable::~CppVariable()
{}

CppEnumerator::CppEnumerator(CPlusPlus::EnumeratorDeclaration *declaration)
    : CppDeclarableElement(declaration)
{
    setHelpCategory(TextEditor::HelpItem::Enum);

    Overview overview;

    Symbol *enumSymbol = declaration->enclosingScope()->asEnum();
    const QString enumName = overview.prettyName(LookupContext::fullyQualifiedName(enumSymbol));
    const QString enumeratorName = overview.prettyName(declaration->name());
    QString enumeratorValue;
    if (enumSymbol) {
        //Compute value
        Scope *enumScope = declaration->enclosingScope();
        int offset = 0;
        const StringLiteral *basevalue = NULL;
        for (unsigned i = 0; i < enumScope->memberCount(); ++i) {
            Symbol *symbol = enumScope->memberAt(i);
            if (Declaration *decl = symbol->asDeclaration()) {
                if (EnumeratorDeclaration *enumerator = decl->asEnumeratorDeclarator()) {
                    if (enumerator->constantValue()) {
                        //a value is set in definition!
                        basevalue = enumerator->constantValue();
                        offset = 0;
                    }
                }
            }
            if (symbol == declaration)
                break;
            else
                ++offset;
        }

        if (!basevalue)
            enumeratorValue = QString("%1").arg(offset);
        else if (offset == 0)
            enumeratorValue = QString::fromUtf8(basevalue->chars(), basevalue->size());
        else
            enumeratorValue = QString::fromUtf8(basevalue->chars(), basevalue->size()) + QString(" + %1").arg(offset);
    }
    else if (const StringLiteral *value = declaration->constantValue()) {
        enumeratorValue = QString::fromUtf8(value->chars(), value->size());
    }

    int value;
    if (ConstantExpressionEvaluator::eval(&value, enumeratorValue)) {
        if (enumeratorValue.contains("0x", Qt::CaseInsensitive))
            enumeratorValue = QString("0x") + QString::number(value, 16);
        else
            enumeratorValue = QString::number(value);
    }

    setHelpMark(overview.prettyName(enumSymbol->name()));

    QString tooltip = enumeratorName;
    if (!enumName.isEmpty())
        tooltip.prepend(enumName + QLatin1Char('.'));
    if (!enumeratorValue.isEmpty())
        tooltip.append(QLatin1String(" = ") + enumeratorValue);
    setTooltip(tooltip);
}

CppEnumerator::~CppEnumerator()
{}
