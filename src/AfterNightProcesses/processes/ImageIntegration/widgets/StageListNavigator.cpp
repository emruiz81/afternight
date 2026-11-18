// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/widgets/StageListNavigator.h"

#include "components/AnStageList.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAction>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::StageListNavigator)

namespace ImageIntegration
{

namespace
{

namespace parts = an::ui::parts;

/// A layout of the column's own: its margins and its spacing are the column's, not the style's.
template <typename Layout>
void keepItsOwnSizes(Layout* layout, const int left, const int top, const int right, const int bottom,
                     const int spacing)
{
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
    layout->setContentsMargins(left, top, right, bottom);
    layout->setSpacing(spacing);
}

/// The column: the first surface behind a hairline at its right.
class StageColumn final : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.fillRect(rect(), t.layers.surface1);
        painter.fillRect(QRect(width() - 1, 0, 1, height()), t.colors.borderSubtle);
    }
};

/// The column's eyebrow: a caps line in the secondary text, set in from the column's edge as the rows' marks are.
class Eyebrow final : public QWidget
{
public:
    Eyebrow(QString text, QWidget* parent) : QWidget(parent), m_text(std::move(text))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const QFontMetrics metrics(capsFont());
        return QSize(2 * parts::kStageColumnEyebrowInset + metrics.horizontalAdvance(m_text), metrics.height());
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setFont(capsFont());
        painter.setPen(an::ui::tokens().colors.textSecondary);
        painter.drawText(rect().adjusted(parts::kStageColumnEyebrowInset, 0, -parts::kStageColumnEyebrowInset, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    [[nodiscard]] static QFont capsFont()
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        return parts::capsFont(t.typography.caption, t.metrics.fontCaption);
    }

    QString m_text;
};

/// A quiet button of the foot that follows an act of the process: enabled as the act is, saying its tooltip.
[[nodiscard]] QPushButton* makeFootButton(const QString& text, const QString& objectName, QAction* act, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setProperty("anButtonVariant", QStringLiteral("quiet"));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (act == nullptr)
    {
        button->setEnabled(false);
        return button;
    }
    const auto follow = [button, act]() {
        button->setEnabled(act->isEnabled());
        button->setToolTip(act->toolTip());
    };
    follow();
    QObject::connect(act, &QAction::changed, button, follow);
    return button;
}

} // namespace

StageListNavigator::StageListNavigator(const std::array<QWidget*, kStagePageCount>& pages, const StagePage current,
                                       const Foot& foot, QWidget* parent, QObject* owner)
    : StageNavigator(owner), m_page(pages), m_current(current)
{
    auto* body = new QWidget(parent);
    body->setObjectName(QStringLiteral("integrationStageBody"));
    m_body = body;
    auto* bodyLayout = new QHBoxLayout(body);
    keepItsOwnSizes(bodyLayout, 0, 0, 0, 0, 0);

    m_column = new StageColumn(body);
    m_column->setObjectName(QStringLiteral("integrationStageColumn"));
    m_column->setFixedWidth(parts::kStageColumnWidth);
    auto* columnLayout = new QVBoxLayout(m_column);
    keepItsOwnSizes(columnLayout, parts::kStageColumnPaddingHorizontal, parts::kStageColumnPaddingVertical,
                    parts::kStageColumnPaddingHorizontal, parts::kStageColumnPaddingVertical, parts::kStageColumnGap);

    auto* eyebrow = new Eyebrow(tr("Run sheet"), m_column);
    eyebrow->setObjectName(QStringLiteral("integrationStageEyebrow"));
    columnLayout->addWidget(eyebrow);

    m_list = new an::ui::AnStageList(m_column);
    m_list->setObjectName(QStringLiteral("integrationStageList"));
    m_list->setAccessibleName(tr("Stages"));
    columnLayout->addWidget(m_list, 1);

    // The foot: the output folder, then the project file's acts.
    m_foot = new QWidget(m_column);
    m_foot->setObjectName(QStringLiteral("integrationStageFoot"));
    auto* footLayout = new QVBoxLayout(m_foot);
    keepItsOwnSizes(footLayout, parts::kStageColumnFootInset, 0, parts::kStageColumnFootInset, 0,
                    parts::kStageColumnGap);
    auto* output = new QVBoxLayout();
    keepItsOwnSizes(output, 0, 0, 0, 0, parts::kStageColumnFootGap);
    if (foot.outputLabel != nullptr)
    {
        foot.outputLabel->setParent(m_foot);
        output->addWidget(foot.outputLabel);
    }
    auto* outputRow = new QHBoxLayout();
    keepItsOwnSizes(outputRow, 0, 0, 0, 0, parts::kStageColumnFootGap);
    if (foot.outputEdit != nullptr)
    {
        foot.outputEdit->setParent(m_foot);
        outputRow->addWidget(foot.outputEdit, 1);
        if (foot.outputLabel != nullptr)
        {
            foot.outputLabel->setBuddy(foot.outputEdit);
        }
    }
    if (foot.outputBrowse != nullptr)
    {
        foot.outputBrowse->setParent(m_foot);
        outputRow->addWidget(foot.outputBrowse);
    }
    output->addLayout(outputRow);
    footLayout->addLayout(output);

    auto* acts = new QGridLayout();
    keepItsOwnSizes(acts, 0, 0, 0, 0, parts::kStageColumnFootGap);
    QPushButton* const importButton =
        makeFootButton(tr("Import..."), QStringLiteral("stageFootImport"), foot.importProject, m_foot);
    QPushButton* const exportButton =
        makeFootButton(tr("Export..."), QStringLiteral("stageFootExport"), foot.exportProject, m_foot);
    QPushButton* const recentButton =
        makeFootButton(tr("Recent"), QStringLiteral("stageFootRecent"),
                       foot.recentProjects != nullptr ? foot.recentProjects->menuAction() : nullptr, m_foot);
    QPushButton* const presetsButton =
        makeFootButton(tr("Presets"), QStringLiteral("stageFootPresets"),
                       foot.presets != nullptr ? foot.presets->menuAction() : nullptr, m_foot);
    QPushButton* const resetButton =
        makeFootButton(tr("Reset Settings..."), QStringLiteral("stageFootReset"), foot.resetSettings, m_foot);
    if (foot.importProject != nullptr)
    {
        connect(importButton, &QPushButton::clicked, foot.importProject, &QAction::trigger);
    }
    if (foot.exportProject != nullptr)
    {
        connect(exportButton, &QPushButton::clicked, foot.exportProject, &QAction::trigger);
    }
    if (foot.resetSettings != nullptr)
    {
        connect(resetButton, &QPushButton::clicked, foot.resetSettings, &QAction::trigger);
    }
    if (foot.recentProjects != nullptr)
    {
        recentButton->setMenu(foot.recentProjects);
    }
    if (foot.presets != nullptr)
    {
        presetsButton->setMenu(foot.presets);
    }
    acts->addWidget(importButton, 0, 0);
    acts->addWidget(exportButton, 0, 1);
    acts->addWidget(recentButton, 1, 0);
    acts->addWidget(presetsButton, 1, 1);
    acts->addWidget(resetButton, 2, 0, 1, 2);
    footLayout->addLayout(acts);
    m_footButtons = {importButton, exportButton, recentButton, presetsButton, resetButton};
    columnLayout->addWidget(m_foot);
    bodyLayout->addWidget(m_column);

    m_pages = new QStackedWidget(body);
    m_pages->setObjectName(QStringLiteral("integrationStagePages"));
    for (QWidget* const page : m_page)
    {
        if (page != nullptr)
        {
            m_pages->addWidget(page);
        }
    }
    bodyLayout->addWidget(m_pages, 1);

    // The rows by their names alone, until their states are told.
    std::vector<an::ui::StageRow> rows;
    rows.reserve(kStageRows.size());
    for (const StageRowSpec& spec : kStageRows)
    {
        an::ui::StageRow row;
        row.id = QString::fromLatin1(spec.id);
        row.title = stageRowTitle(row.id);
        rows.push_back(std::move(row));
    }
    m_list->setRows(rows);
    if (QWidget* const shown = m_page[static_cast<std::size_t>(m_current)])
    {
        m_pages->setCurrentWidget(shown);
    }
    m_list->setSelectedId(stageRowOfPage(m_current));
    connect(m_list, &an::ui::AnStageList::selectedStageChanged, this,
            [this](const QString& rowId) { rowSelected(rowId); });
    // A page shown by other means - a widget of it revealed - is the page selected.
    connect(m_pages, &QStackedWidget::currentChanged, this, [this](int) {
        const QWidget* const shown = m_pages->currentWidget();
        for (int page = 0; page < kStagePageCount; ++page)
        {
            if (m_page[static_cast<std::size_t>(page)] == shown && shown != nullptr)
            {
                setCurrentPage(static_cast<StagePage>(page));
                return;
            }
        }
    });
}

StageListNavigator::~StageListNavigator() = default;

QWidget* StageListNavigator::widget() const
{
    return m_body;
}

QWidget* StageListNavigator::page(const StagePage page) const
{
    const int at = static_cast<int>(page);
    return at >= 0 && at < kStagePageCount ? m_page[static_cast<std::size_t>(at)] : nullptr;
}

StagePage StageListNavigator::currentPage() const
{
    return m_current;
}

void StageListNavigator::setCurrentPage(const StagePage page)
{
    const int at = static_cast<int>(page);
    if (at < 0 || at >= kStagePageCount)
    {
        return;
    }
    // A row that shows the page already keeps the selection: normalization and drizzle show the stacking page.
    if (stagePageOfRow(m_list->selectedId()) != page)
    {
        m_list->setSelectedId(stageRowOfPage(page));
        return;
    }
    if (QWidget* const shown = m_page[static_cast<std::size_t>(at)]; shown != nullptr && m_pages != nullptr)
    {
        m_pages->setCurrentWidget(shown);
    }
    if (m_current != page)
    {
        m_current = page;
        emit currentPageChanged(page);
    }
}

void StageListNavigator::setFrameCounts(const std::array<int, kFrameSourcePageCount>& /*counts*/) {}

void StageListNavigator::setIssues(const std::array<StagePageIssues, kStagePageCount>& /*issues*/) {}

void StageListNavigator::refreshTheme()
{
    if (!m_body.isNull())
    {
        m_body->update();
        m_list->viewport()->update();
    }
}

void StageListNavigator::setRows(const std::vector<an::ui::StageRow>& rows)
{
    if (m_list->rows() == rows)
    {
        return;
    }
    if (static_cast<int>(rows.size()) != m_list->rowCount())
    {
        m_list->setRows(rows);
        return;
    }
    // One row at a time, so a row that did not change is not laid out again.
    const std::vector<an::ui::StageRow> shown = m_list->rows();
    for (std::size_t row = 0; row < rows.size(); ++row)
    {
        if (shown[row].id != rows[row].id)
        {
            m_list->setRows(rows);
            return;
        }
    }
    for (std::size_t row = 0; row < rows.size(); ++row)
    {
        if (!(shown[row] == rows[row]))
        {
            m_list->updateRow(rows[row]);
        }
    }
}

an::ui::AnStageList* StageListNavigator::list() const noexcept
{
    return m_list;
}

QWidget* StageListNavigator::column() const noexcept
{
    return m_column;
}

QWidget* StageListNavigator::foot() const noexcept
{
    return m_foot;
}

QList<QAbstractButton*> StageListNavigator::footButtons() const
{
    return m_footButtons;
}

QString StageListNavigator::currentRow() const
{
    return m_list->selectedId();
}

void StageListNavigator::rowSelected(const QString& rowId)
{
    const std::optional<StagePage> page = stagePageOfRow(rowId);
    if (!page.has_value())
    {
        return;
    }
    if (QWidget* const shown = m_page[static_cast<std::size_t>(*page)]; shown != nullptr)
    {
        m_pages->setCurrentWidget(shown);
    }
    if (m_current != *page)
    {
        m_current = *page;
        emit currentPageChanged(*page);
    }
}

} // namespace ImageIntegration
