// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "dialogs/CompactHistoryDialog.h"

#include "components/AnInfoBanner.h"
#include "controls/HomeParts.h"
#include "controls/HomeProjectsRow.h"
#include "theme/AnDesignTokens.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <utility>

W_OBJECT_IMPL(an::ui::CompactHistoryDialog)

namespace an::ui
{

namespace
{

/// A size as the dialog says it: in gigabytes to one decimal, as Home writes a project's.
QString sizeText(const std::uint64_t bytes)
{
    return homeSizeText(static_cast<qint64>(bytes));
}

QString stepsText(const std::size_t steps)
{
    return steps == 1u ? QCoreApplication::translate("an::ui::CompactHistoryDialog", "1 step")
                       : QCoreApplication::translate("an::ui::CompactHistoryDialog", "%1 steps").arg(steps);
}

} // namespace

struct CompactHistoryDialog::Impl
{
    std::vector<CompactHistoryImage> images;
    Operation plan;
    Operation compact;

    QRadioButton* lastSteps = nullptr;
    QSpinBox* steps = nullptr;
    QRadioButton* currentState = nullptr;
    QCheckBox* setAside = nullptr;
    HomeText* frees = nullptr;
    AnInfoBanner* warning = nullptr;
    QPushButton* cancel = nullptr;
    QPushButton* compactButton = nullptr;

    // The plan asked for last, and what its answers came to; a plan's answer for an earlier choice is dropped.
    quint64 generation = 0;
    std::size_t answered = 0;
    std::uint64_t planned = 0;
    bool planLanded = false;

    // The compaction on its way.
    bool compacting = false;
    std::size_t compacted = 0;
    std::uint64_t freed = 0;
};

CompactHistoryDialog::CompactHistoryDialog(std::vector<CompactHistoryImage> images, Operation plan, Operation compact,
                                           QWidget* parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    impl.images = std::move(images);
    impl.plan = std::move(plan);
    impl.compact = std::move(compact);
    setObjectName(QStringLiteral("compactHistoryDialog"));
    setWindowTitle(tr("Compact History"));
    setModal(true);

    const DesignTokens& t = tokens();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.spacing.xl, t.spacing.lg, t.spacing.xl, t.spacing.lg);
    column->setSpacing(t.spacing.md);

    // Each image, its steps and their size.
    column->addWidget(new HomeText(HomeText::Role::Strong, tr("The undo history of the project's images"), this));
    for (const QString& row : imageRows())
    {
        auto* line = new HomeText(HomeText::Role::Mono, row, this);
        line->setObjectName(QStringLiteral("compactHistoryImage"));
        line->setElideMode(Qt::ElideMiddle);
        line->setToolTip(row);
        column->addWidget(line);
    }

    // What is kept.
    column->addWidget(new HomeText(HomeText::Role::Strong, tr("Keep"), this));
    auto* keep = new QButtonGroup(this);
    auto* stepsRow = new QHBoxLayout();
    stepsRow->setContentsMargins(0, 0, 0, 0);
    stepsRow->setSpacing(t.spacing.sm);
    impl.lastSteps = new QRadioButton(tr("The last"), this);
    impl.lastSteps->setObjectName(QStringLiteral("compactHistoryKeepLast"));
    impl.lastSteps->setChecked(true);
    keep->addButton(impl.lastSteps);
    stepsRow->addWidget(impl.lastSteps);
    impl.steps = new QSpinBox(this);
    impl.steps->setObjectName(QStringLiteral("compactHistorySteps"));
    impl.steps->setRange(1, 999);
    impl.steps->setValue(kStepsKeptAtFirst);
    impl.steps->setSuffix(tr(" steps"));
    stepsRow->addWidget(impl.steps);
    stepsRow->addStretch(1);
    column->addLayout(stepsRow);
    impl.currentState = new QRadioButton(tr("Only the current state"), this);
    impl.currentState->setObjectName(QStringLiteral("compactHistoryKeepCurrent"));
    keep->addButton(impl.currentState);
    column->addWidget(impl.currentState);
    std::uint64_t setAside = 0;
    for (const CompactHistoryImage& image : impl.images)
    {
        setAside += image.detachedBytes;
    }
    impl.setAside = new QCheckBox(tr("Remove the histories set aside too (%1)").arg(sizeText(setAside)), this);
    impl.setAside->setObjectName(QStringLiteral("compactHistoryRemoveSetAside"));
    impl.setAside->setVisible(setAside > 0u);
    column->addWidget(impl.setAside);

    impl.frees = new HomeText(HomeText::Role::Body, QString(), this);
    impl.frees->setObjectName(QStringLiteral("compactHistoryFrees"));
    column->addWidget(impl.frees);
    column->addStretch(1);

    impl.warning = new AnInfoBanner(tr("Compacting removes the step files of the steps it does not keep and rewrites "
                                       "each history's index. It cannot be undone."),
                                    AnInfoBanner::Tone::Warning, this);
    impl.warning->setObjectName(QStringLiteral("compactHistoryWarning"));
    column->addWidget(impl.warning);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(t.spacing.sm);
    buttons->addStretch(1);
    impl.cancel = new QPushButton(tr("Cancel"), this);
    impl.cancel->setObjectName(QStringLiteral("compactHistoryCancel"));
    impl.cancel->setAutoDefault(false);
    buttons->addWidget(impl.cancel);
    impl.compactButton = new QPushButton(tr("Compact"), this);
    impl.compactButton->setObjectName(QStringLiteral("compactHistoryCompact"));
    impl.compactButton->setProperty("anButtonVariant", QStringLiteral("destructive"));
    buttons->addWidget(impl.compactButton);
    column->addLayout(buttons);

    QObject::connect(impl.cancel, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(impl.compactButton, &QPushButton::clicked, this, [this]() { compactAll(); });
    QObject::connect(impl.lastSteps, &QRadioButton::toggled, this, [this]() { replan(); });
    QObject::connect(impl.steps, &QSpinBox::valueChanged, this, [this]() { replan(); });
    QObject::connect(impl.setAside, &QCheckBox::toggled, this, [this]() { replan(); });
    replan();
}

CompactHistoryDialog::~CompactHistoryDialog() = default;

QStringList CompactHistoryDialog::imageRows() const
{
    QStringList rows;
    for (const CompactHistoryImage& image : m_impl->images)
    {
        QString row =
            image.name + QStringLiteral(" · ") + stepsText(image.steps) + QStringLiteral(" · ") + sizeText(image.bytes);
        if (image.detachedBytes > 0u)
        {
            row += QStringLiteral(" · ") + tr("%1 set aside").arg(sizeText(image.detachedBytes));
        }
        rows.append(row);
    }
    return rows;
}

QRadioButton* CompactHistoryDialog::keepLastSteps() const
{
    return m_impl->lastSteps;
}

QSpinBox* CompactHistoryDialog::stepsToKeep() const
{
    return m_impl->steps;
}

QRadioButton* CompactHistoryDialog::keepCurrentState() const
{
    return m_impl->currentState;
}

QCheckBox* CompactHistoryDialog::removeSetAside() const
{
    return m_impl->setAside;
}

history::HistoryCompaction CompactHistoryDialog::choice() const
{
    history::HistoryCompaction what;
    what.keep = m_impl->currentState->isChecked() ? history::HistoryCompaction::Keep::CurrentState
                                                  : history::HistoryCompaction::Keep::LastSteps;
    what.steps = static_cast<std::size_t>((std::max)(1, m_impl->steps->value()));
    what.removeDetached = !m_impl->setAside->isHidden() && m_impl->setAside->isChecked();
    return what;
}

QString CompactHistoryDialog::freesText() const
{
    return m_impl->frees->text();
}

std::uint64_t CompactHistoryDialog::plannedBytes() const noexcept
{
    return m_impl->planLanded ? m_impl->planned : 0u;
}

QString CompactHistoryDialog::warningText() const
{
    return m_impl->warning->message();
}

QPushButton* CompactHistoryDialog::compactButton() const
{
    return m_impl->compactButton;
}

void CompactHistoryDialog::replan()
{
    Impl& impl = *m_impl;
    if (impl.compacting)
    {
        return;
    }
    impl.steps->setEnabled(impl.lastSteps->isChecked());
    const quint64 generation = ++impl.generation;
    impl.answered = 0;
    impl.planned = 0;
    impl.planLanded = false;
    impl.frees->setText(tr("Measuring what it frees..."));
    impl.compactButton->setEnabled(false);
    if (impl.images.empty() || !impl.plan)
    {
        impl.planLanded = true;
        impl.frees->setText(tr("There is no undo history to compact."));
        return;
    }
    const history::HistoryCompaction what = choice();
    const QPointer<CompactHistoryDialog> self(this);
    const std::size_t count = impl.images.size();
    for (const CompactHistoryImage& image : impl.images)
    {
        impl.plan(image.id, what, [self, generation, count](const history::HistoryCompactionResult& result) {
            if (!self || generation != self->m_impl->generation)
            {
                return;
            }
            Impl& landed = *self->m_impl;
            if (result.ok)
            {
                landed.planned += result.bytesFreed;
            }
            if (++landed.answered < count)
            {
                return;
            }
            landed.planLanded = true;
            landed.frees->setText(landed.planned > 0u ? self->tr("It frees %1.").arg(sizeText(landed.planned))
                                                      : self->tr("It frees nothing: every step is kept."));
            landed.compactButton->setEnabled(landed.planned > 0u);
        });
    }
}

void CompactHistoryDialog::compactAll()
{
    Impl& impl = *m_impl;
    if (impl.compacting || !impl.compact)
    {
        return;
    }
    impl.compacting = true;
    impl.compacted = 0;
    impl.freed = 0;
    for (QWidget* part : {static_cast<QWidget*>(impl.lastSteps), static_cast<QWidget*>(impl.steps),
                          static_cast<QWidget*>(impl.currentState), static_cast<QWidget*>(impl.setAside),
                          static_cast<QWidget*>(impl.compactButton)})
    {
        part->setEnabled(false);
    }
    impl.frees->setText(tr("Compacting..."));
    const history::HistoryCompaction what = choice();
    const QPointer<CompactHistoryDialog> self(this);
    const std::size_t count = impl.images.size();
    for (const CompactHistoryImage& image : impl.images)
    {
        impl.compact(image.id, what, [self, count](const history::HistoryCompactionResult& result) {
            if (!self)
            {
                return;
            }
            Impl& landed = *self->m_impl;
            if (result.ok)
            {
                landed.freed += result.bytesFreed;
            }
            if (++landed.compacted < count)
            {
                return;
            }
            emit self->compacted(static_cast<quint64>(landed.freed));
            self->accept();
        });
    }
}

} // namespace an::ui
