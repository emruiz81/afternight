// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/StarsFilterWindow.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>

#include <random>
#include <utility>

namespace
{

/// How far outside a range's two values a figure is still held by it, so a star at a limit the slider rounds is kept.
constexpr double kRangeTolerance = 0.01;

/// A seed nobody stated, for a window a person opens.
[[nodiscard]] std::uint64_t drawSeed()
{
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32u) | static_cast<std::uint64_t>(device());
}

} // namespace

StarsFilterWindow::StarsFilterWindow(StarsFilterControlHandler* parent)
    : QDialog(dynamic_cast<QWidget*>(parent)), StarsFilterControlBase(parent), m_Seed(drawSeed())
{
    this->setAttribute(Qt::WA_DeleteOnClose);
}

StarsFilterWindow::~StarsFilterWindow() = default;

void StarsFilterWindow::attachControls(Controls controls)
{
    m_Controls = std::move(controls);

    if (QDialogButtonBox* buttons = m_Controls.buttons)
    {
        QObject::connect(buttons, &QDialogButtonBox::clicked, this,
                         [this](QAbstractButton* button) { on_dialog_box_button_clicked(button); });
    }
    if (m_Controls.preview != nullptr)
    {
        QObject::connect(m_Controls.preview, &QCheckBox::checkStateChanged, this,
                         [this](Qt::CheckState state) { on_PreviewCheckClicked(state); });
    }
    if (m_Controls.includeNeighbours != nullptr)
    {
        QObject::connect(m_Controls.includeNeighbours, &QCheckBox::checkStateChanged, this,
                         [this](Qt::CheckState state) { on_IncludeNeighborsCheckClicked(state); });
    }

    if (m_Process != nullptr && m_Process->getViewControl() != nullptr)
    {
        const std::string title = fmt::format("Stars Filter <{}>", m_Process->getViewControl()->getUniqueName());
        setWindowTitle(QString::fromUtf8(title));
    }

    an::ui::StarListPtr stars = m_Process != nullptr ? m_Process->getCurrentStarList() : nullptr;
    m_original_star_list = stars ? std::move(stars) : std::make_shared<const an::ui::StarList>();
    updateLimits();

    updateFilteredStars(m_original_star_list->size(), m_original_star_list->size());
}

void StarsFilterWindow::updateFilter()
{
    executeFilter(true);
}

bool StarsFilterWindow::isPreviewEnabled() const
{
    return m_Controls.preview != nullptr && m_Controls.preview->isChecked();
}

void StarsFilterWindow::forceClose()
{
    close();
}

void StarsFilterWindow::setRandomSeed(const std::uint64_t seed)
{
    m_Seed = seed;
}

const an::ui::StarListPtr& StarsFilterWindow::originalStars() const
{
    return m_original_star_list;
}

an::ui::StarFilterRequest StarsFilterWindow::filterRequest() const
{
    an::ui::StarFilterRequest request;
    for (const RangeControls& range : m_Controls.ranges)
    {
        if (range.lower == nullptr || range.upper == nullptr)
        {
            continue;
        }
        const double lower = range.lower->getValue();
        const double upper = range.upper->getValue();
        bool narrowed = false;
        an::ui::StarFilterRange held{.field = range.field};
        if (range.brightnessOrder)
        {
            narrowed = lower != range.lower->getMaximum() || upper != range.upper->getMinimum();
            held.lowest = upper - kRangeTolerance;
            held.highest = lower + kRangeTolerance;
        }
        else
        {
            narrowed = lower != range.lower->getMinimum() || upper != range.upper->getMaximum();
            held.lowest = lower - kRangeTolerance;
            held.highest = upper + kRangeTolerance;
        }
        if (narrowed)
        {
            request.ranges.push_back(held);
        }
    }
    if (m_Controls.randomSelection != nullptr &&
        m_Controls.randomSelection->getValue() != m_Controls.randomSelection->getMaximum())
    {
        request.keepPercent = m_Controls.randomSelection->getValue();
    }
    request.seed = m_Seed;
    request.includeNeighbours = m_Controls.includeNeighbours != nullptr && m_Controls.includeNeighbours->isChecked();
    request.scaleFactor = m_Process != nullptr ? m_Process->getCurrentScaleFactor() : 1.0;
    return request;
}

void StarsFilterWindow::on_dialog_box_button_clicked(QAbstractButton* button)
{
    if (m_Controls.buttons == nullptr)
        return;

    QDialogButtonBox::StandardButton sb = m_Controls.buttons->standardButton(button);

    if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        int ret = QMessageBox::No;
        if (auto* context = an::ui::appContext())
        {
            if (auto* dialogService = context->getDialogService())
            {
                dialogService->messageBoxYesNo(
                    this, QStringLiteral("Apply filter"),
                    QStringLiteral(
                        "This change will discard the filtered stars from the list, if you want those stars back "
                        "you'll need to load the profile again, or run a new detection. Do you want to continue?"),
                    false, &ret);
            }
        }
        if (ret == QMessageBox::Yes)
        {
            executeFilter(false);

            m_KeepChanges = true;
            this->close();
        }
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        updateLimits();

        updateFilteredStars(m_original_star_list->size(), m_original_star_list->size());
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

void StarsFilterWindow::on_PreviewCheckClicked(Qt::CheckState state)
{
    if (state == Qt::Unchecked && m_Process != nullptr)
    {
        m_Process->updateStarSelection(m_original_star_list);
    }

    settingsUpdated();
}

void StarsFilterWindow::on_IncludeNeighborsCheckClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void StarsFilterWindow::done(int r)
{
    // A window closed without applying puts back the stars it started from.
    if (!m_KeepChanges && m_Process != nullptr)
    {
        m_Process->updateStarSelection(m_original_star_list);
        if (m_Process->isForProfiledStarmaskRecombination())
            m_Process->setCurrentStarList(m_original_star_list, true);
    }

    QDialog::done(r);
}

void StarsFilterWindow::updateLimits()
{
    m_IgnoreLastSettingsUpdatedEvent = true;

    for (const RangeControls& range : m_Controls.ranges)
    {
        if (range.lower == nullptr || range.upper == nullptr)
        {
            continue;
        }
        const an::ui::StarFilterLimits limits =
            an::ui::starFilterLimits(*m_original_star_list, range.field).value_or(an::ui::StarFilterLimits{});
        for (QSliderDoubleSpinBox* control : {range.lower, range.upper})
        {
            control->setMinimum(limits.minimum);
            control->setMaximum(limits.maximum);
        }
        range.lower->setValue(range.brightnessOrder ? limits.maximum : limits.minimum);
        range.upper->setValue(range.brightnessOrder ? limits.minimum : limits.maximum);
    }

    if (m_Controls.randomSelection != nullptr)
    {
        m_Controls.randomSelection->setValue(100.0);
    }

    m_IgnoreLastSettingsUpdatedEvent = false;

    settingsUpdated();
}

void StarsFilterWindow::updateFilteredStars(const std::size_t total, const std::size_t filtered)
{
    if (m_Controls.filteredStars == nullptr)
    {
        return;
    }
    auto text = fmt::format("{}/{}", filtered, total);
    m_Controls.filteredStars->setText(QString::fromStdString(text));
}

void StarsFilterWindow::settingsUpdated()
{
    if (!m_IgnoreLastSettingsUpdatedEvent)
        executeFilter(true);
}

void StarsFilterWindow::executeFilter(bool forPreview)
{
    if (m_Process == nullptr)
    {
        return;
    }

    const an::ui::StarListPtr filtered = an::ui::filteredStarList(m_original_star_list, filterRequest());

    updateFilteredStars(m_original_star_list->size(), filtered->size());

    if (forPreview)
    {
        if (isPreviewEnabled())
        {
            m_Process->updateStarSelection(filtered);

            if (m_Process->isForProfiledStarmaskRecombination() && m_Process->isPreviewEnabled())
                m_Process->setCurrentStarList(filtered, true);
        }
    }
    else
    {
        m_Process->setCurrentStarList(filtered, true);
    }
}
