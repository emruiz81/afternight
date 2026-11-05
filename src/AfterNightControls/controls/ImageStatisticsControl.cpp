// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageStatisticsControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <QCoreApplication>
#include <QHeaderView>
#include <QMetaObject>
#include <QPointer>
#include <QTableWidgetItem>
#include <QThreadPool>

#include <array>
#include <utility>

const QString _NORMALIZED_LABEL("Normalized (0.0-1.0)");
const QString _8_BIT_LABEL("8-bit (0-255)");
const QString _16_BIT_LABEL("16-bit (0-65K)");
const QString _32_BIT_LABEL("32-bit (0-4000M)");

namespace
{

/// The session every Controls operation runs in, or null when the application has none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

/// Measures an image in an operation the session admits, under the image's own lease, taken before the admission: an
/// edit holds the image's writer while it waits to be admitted. A measurement someone asked for, once, is not a preview
/// that follows a dialog's parameters, so it is admitted at the session's width rather than within the preview
/// reservation.
[[nodiscard]] an::Result<an::ui::ImageStatisticsReport> measure(const an::consumer::EngineSession& session,
                                                                const an::consumer::ImageAsset& image,
                                                                const bool excludeClipped, std::stop_token stop)
{
    an::Result<an::consumer::AssetReadLease> lease = image.read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    return an::ui::computeImageStatisticsReport(*scope, *lease, excludeClipped);
}

/// A column's heading: grey for one channel; red, green, blue and alpha for colour.
[[nodiscard]] QStringList channelLabels(const std::size_t channels)
{
    if (channels == 1u)
    {
        return {QStringLiteral("Gray")};
    }
    static const std::array<QString, 4> kColour{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue"),
                                                QStringLiteral("Alpha")};
    QStringList labels;
    for (std::size_t channel = 0u; channel < channels; ++channel)
    {
        labels.push_back(channel < kColour.size() ? kColour[channel]
                                                  : QStringLiteral("Channel %1").arg(static_cast<int>(channel) + 1));
    }
    return labels;
}

[[nodiscard]] an::ui::StatisticsScale scaleOf(const QString& label)
{
    if (label == _8_BIT_LABEL)
    {
        return an::ui::StatisticsScale::Bits8;
    }
    if (label == _16_BIT_LABEL)
    {
        return an::ui::StatisticsScale::Bits16;
    }
    if (label == _32_BIT_LABEL)
    {
        return an::ui::StatisticsScale::Bits32;
    }
    return an::ui::StatisticsScale::Normalized;
}

const QStringList kRowLabels = {"Count (px)", "Count (%)", "Mean", "Sigma",    "Median", "Min",     "Max",
                                "StdDev",     "AvgDev",    "BWMV", "Variance", "MAD",    "BG Noise"};

} // namespace

/// Which measurement is the latest, and what they came to. Touched on the GUI thread only.
struct ImageStatisticsControl::RequestState
{
    std::uint64_t latest = 0u;
    std::uint64_t landed = 0u;
    std::uint64_t shown = 0u;
};

ImageStatisticsControl::ImageStatisticsControl(ImageViewControl* control)
    : QDialog(control), ImageViewControlListener(control), m_Requests(std::make_shared<RequestState>())
{
    m_ui = an::ui::controls::buildImageStatisticsControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    m_ui.comboBox->setEditable(false);

    m_ui.comboBox->addItem(_NORMALIZED_LABEL);
    m_ui.comboBox->addItem(_8_BIT_LABEL);
    m_ui.comboBox->addItem(_16_BIT_LABEL);
    m_ui.comboBox->addItem(_32_BIT_LABEL);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.comboBox, &QComboBox::currentTextChanged, this,
                     &ImageStatisticsControl::on_comboBox_currentTextChanged);
    QObject::connect(m_ui.refreshButton, &QPushButton::released, this, &ImageStatisticsControl::on_refreshButtonPushed);
    QObject::connect(m_ui.closeButton, &QPushButton::released, this, &ImageStatisticsControl::on_closeButtonPushed);
    QObject::connect(m_ui.unclipped, &QCheckBox::checkStateChanged, this,
                     &ImageStatisticsControl::on_UnclippedCheckBoxClicked);

    std::string title = fmt::format("Statistics <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));

    initialize();

    m_image_view_control->enableAction(ImageViewControlAction::execute_statistics, false);

    recalculateAsync();
}

void ImageStatisticsControl::onImageLoaded(ImageViewControl* ctrl)
{
    recalculateAsync();
}

void ImageStatisticsControl::onImageUpdated(ImageViewControl* ctrl)
{
    recalculateAsync();
}

void ImageStatisticsControl::onImageRenamed(ImageViewControl* ctrl)
{
    std::string title = fmt::format("Statistics <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));
}

void ImageStatisticsControl::recalculateAsync()
{
    // A measurement on its way is for an image or an option that has moved on.
    m_Stop.request_stop();
    m_Stop = std::stop_source();
    const std::uint64_t request = ++m_Requests->latest;

    std::shared_ptr<const an::consumer::ImageAsset> image =
        m_image_view_control != nullptr ? m_image_view_control->document().asset() : nullptr;
    const an::consumer::EngineSession* session = currentSession();
    if (!image || session == nullptr)
    {
        ++m_Requests->landed;
        m_Report.reset();
        m_ui.tableWidget->clearContents();
        return;
    }

    const bool excludeClipped =
        an::ui::controls::detail::statisticsShouldExcludeClippedSamples(m_ui.unclipped->isChecked());
    QPointer<ImageStatisticsControl> guard(this);
    std::weak_ptr<RequestState> state = m_Requests;
    const auto land = [guard, state, request](an::Result<an::ui::ImageStatisticsReport>&& measured) {
        const std::shared_ptr<RequestState> held = state.lock();
        if (!held)
        {
            return;
        }
        ++held->landed;
        if (request != held->latest || !guard)
        {
            return;
        }
        if (!measured.hasValue())
        {
            guard->m_Report.reset();
            guard->m_ui.tableWidget->clearContents();
            return;
        }
        guard->showReport(std::move(*measured));
    };
    QThreadPool::globalInstance()->start(
        [session, image = std::move(image), excludeClipped, land, stop = m_Stop.get_token()]() {
            auto measured = std::make_shared<an::Result<an::ui::ImageStatisticsReport>>(
                measure(*session, *image, excludeClipped, stop));
            QCoreApplication* application = QCoreApplication::instance();
            if (application == nullptr)
            {
                return;
            }
            QMetaObject::invokeMethod(
                application, [land, measured]() { land(std::move(*measured)); }, Qt::QueuedConnection);
        });
}

const std::optional<an::ui::ImageStatisticsReport>& ImageStatisticsControl::report() const
{
    return m_Report;
}

std::uint64_t ImageStatisticsControl::reportsShown() const
{
    return m_Requests->shown;
}

bool ImageStatisticsControl::isIdle() const
{
    return m_Requests->landed == m_Requests->latest;
}

ImageStatisticsControl::~ImageStatisticsControl()
{
    m_Stop.request_stop();
}

void ImageStatisticsControl::on_comboBox_currentTextChanged(const QString& str)
{
    refillData();
}

void ImageStatisticsControl::on_refreshButtonPushed()
{
    recalculateAsync();
}

void ImageStatisticsControl::on_closeButtonPushed()
{
    this->close();
}

void ImageStatisticsControl::on_UnclippedCheckBoxClicked(Qt::CheckState state)
{
    recalculateAsync();
}

void ImageStatisticsControl::done(int r)
{
    if (m_image_view_control)
    {
        m_image_view_control->enableAction(ImageViewControlAction::execute_statistics, true);
    }

    QDialog::done(r);
}

void ImageStatisticsControl::initialize()
{
    auto horizontalHeader = m_ui.tableWidget->horizontalHeader();
    auto verticalHeader = m_ui.tableWidget->verticalHeader();

    horizontalHeader->setSectionResizeMode(QHeaderView::Stretch);
    verticalHeader->setVisible(true);
    verticalHeader->setSectionResizeMode(QHeaderView::Fixed);

    // Compute minimum width from the longest row label so nothing gets clipped
    const QFontMetrics fm(verticalHeader->font());
    int maxWidth = 0;
    for (const auto& label : kRowLabels)
    {
        maxWidth = (std::max)(maxWidth, fm.horizontalAdvance(label));
    }
    verticalHeader->setFixedWidth(maxWidth + 24);

    m_ui.tableWidget->setAlternatingRowColors(true);
}

void ImageStatisticsControl::showReport(an::ui::ImageStatisticsReport report)
{
    m_Report = std::move(report);
    ++m_Requests->shown;

    m_ui.tableWidget->setRowCount(static_cast<int>(kRowLabels.size()));
    const QStringList columns = channelLabels(m_Report->channels.size());
    m_ui.tableWidget->setColumnCount(static_cast<int>(columns.size()));
    m_ui.tableWidget->setHorizontalHeaderLabels(columns);
    m_ui.tableWidget->setVerticalHeaderLabels(kRowLabels);

    refillData();
}

void ImageStatisticsControl::refillData()
{
    if (!m_Report.has_value())
    {
        return;
    }

    m_ui.tableWidget->clearContents();

    const an::ui::StatisticsScale scale = scaleOf(m_ui.comboBox->currentText());
    const int decimals = scale == an::ui::StatisticsScale::Normalized ? 7 : 3;

    for (int channel = 0; channel < static_cast<int>(m_Report->channels.size()); ++channel)
    {
        const an::ui::ImageStatisticsChannel& figures = m_Report->channels[static_cast<std::size_t>(channel)];

        m_ui.tableWidget->setItem(0, channel,
                                  new QTableWidgetItem(tr("%1").arg(static_cast<qulonglong>(figures.count))));
        const double countPercent =
            figures.pixels > 0u ? static_cast<double>(figures.count) * 100.0 / static_cast<double>(figures.pixels)
                                : 0.0;
        m_ui.tableWidget->setItem(1, channel, new QTableWidgetItem(tr("%1").arg(countPercent, 0, 'f', 7)));

        const std::array<double, an::ui::kStatisticsFigureRows> values =
            an::ui::scaledStatisticsFigures(figures, scale);
        for (std::size_t row = 0u; row < values.size(); ++row)
        {
            m_ui.tableWidget->setItem(static_cast<int>(row) + 2, channel,
                                      new QTableWidgetItem(tr("%1").arg(values[row], 0, 'f', decimals)));
        }
    }
}
