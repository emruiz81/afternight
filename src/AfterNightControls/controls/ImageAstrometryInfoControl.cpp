// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ImageAstrometryInfoControl.h"

#include "components/AnCardWidget.h"
#include "controls/ImageAstrometryInfoFormatter.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <vector>

namespace
{

void addSummaryRow(QFormLayout* layout, const QString& title, QLabel*& valueLabel, QWidget* parent)
{
    auto* keyLabel = new QLabel(title, parent);
    keyLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    valueLabel = new QLabel(QStringLiteral("-"), parent);
    valueLabel->setWordWrap(true);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addRow(keyLabel, valueLabel);
}

} // namespace

namespace an::ui
{

ImageAstrometryInfoControl::ImageAstrometryInfoControl(ImageViewControl* control)
    : QDialog(control), AfterNightAppListener(an::ui::appContext() ? an::ui::appContext()->getAppEventBus() : nullptr),
      ImageViewControlListener(control)
{
    m_currentViewControl = control;

    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);

    const auto& tokenSet = an::ui::tokens();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg, tokenSet.spacing.lg);
    rootLayout->setSpacing(tokenSet.spacing.md);

    auto* headerWidget = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(tokenSet.spacing.xs);

    auto* titleLabel = new QLabel(tr("Astrometry Info"), headerWidget);
    titleLabel->setFont(tokenSet.typography.heading1);
    {
        QPalette pal = titleLabel->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
        titleLabel->setPalette(pal);
    }
    headerLayout->addWidget(titleLabel);

    auto* descriptionLabel = new QLabel(tr("Persistent summary of the current image's astrometric solution, including "
                                           "field center, projection, image scale, and distortion metadata."),
                                        headerWidget);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setFont(tokenSet.typography.body);
    {
        QPalette pal = descriptionLabel->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        descriptionLabel->setPalette(pal);
    }
    headerLayout->addWidget(descriptionLabel);
    rootLayout->addWidget(headerWidget);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
    rootLayout->addWidget(m_statusLabel);

    m_summaryCard = new an::ui::AnCardWidget(QString(), this);
    auto* summaryLayout = new QFormLayout();
    summaryLayout->setHorizontalSpacing(tokenSet.spacing.md);
    summaryLayout->setVerticalSpacing(tokenSet.spacing.sm);
    addSummaryRow(summaryLayout, tr("Field center RA"), m_fieldCenterRaLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Field center Dec"), m_fieldCenterDecLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("CRVAL RA"), m_crvalRaLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("CRVAL Dec"), m_crvalDecLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Image scale"), m_imageScaleLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Rotation"), m_rotationLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Field of view"), m_fieldOfViewLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Projection"), m_projectionLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Equinox"), m_equinoxLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("SIP distortion"), m_sipLabel, m_summaryCard->contentWidget());
    addSummaryRow(summaryLayout, tr("Solver"), m_solverLabel, m_summaryCard->contentWidget());
    m_summaryCard->setContentLayout(summaryLayout);
    rootLayout->addWidget(m_summaryCard, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    buttonLayout->addWidget(m_closeButton);
    rootLayout->addLayout(buttonLayout);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);

    resize(520, 520);
    refreshWindowTitle();
    recalculate();
}

void ImageAstrometryInfoControl::onImageLoaded(ImageViewControl* ctrl)
{
    Q_UNUSED(ctrl);
    recalculate();
}

void ImageAstrometryInfoControl::onImageUpdated(ImageViewControl* ctrl)
{
    Q_UNUSED(ctrl);
    recalculate();
}

void ImageAstrometryInfoControl::onImageRenamed(ImageViewControl* ctrl)
{
    Q_UNUSED(ctrl);
    refreshWindowTitle();
}

void ImageAstrometryInfoControl::onImageViewControlDestroyed(ImageViewControl* ctrl)
{
    ImageViewControlListener::onImageViewControlDestroyed(ctrl);

    if (ctrl == m_currentViewControl)
    {
        auto* context = an::ui::appContext();
        auto* viewRegistry = context ? context->getViewRegistry() : nullptr;
        attachToViewControl(viewRegistry ? viewRegistry->getActiveImageViewControl() : nullptr);
    }
}

void ImageAstrometryInfoControl::onActiveViewControlChanged(ImageViewControl* ctrl)
{
    attachToViewControl(ctrl);
}

ImageAstrometryInfoControl::~ImageAstrometryInfoControl() = default;

void ImageAstrometryInfoControl::attachToViewControl(ImageViewControl* control)
{
    if (m_currentViewControl == control)
    {
        refreshWindowTitle();
        recalculate();
        return;
    }

    m_currentViewControl = control;
    setViewControl(control);
    refreshWindowTitle();
    recalculate();
}

void ImageAstrometryInfoControl::refreshWindowTitle()
{
    const QString image_name =
        m_currentViewControl ? QString::fromUtf8(m_currentViewControl->getUniqueName()) : tr("No active image");
    setWindowTitle(tr("Astrometry Info <%1>").arg(image_name));
}

void ImageAstrometryInfoControl::recalculate()
{
    if (!m_currentViewControl || !m_currentViewControl->hasImage())
    {
        m_statusLabel->setText(tr("No active image"));
        m_statusLabel->setProperty("anStatusTone", QStringLiteral("warning"));
        m_statusLabel->style()->unpolish(m_statusLabel);
        m_statusLabel->style()->polish(m_statusLabel);
        m_statusLabel->update();
        for (QLabel* label :
             {m_fieldCenterRaLabel, m_fieldCenterDecLabel, m_crvalRaLabel, m_crvalDecLabel, m_imageScaleLabel,
              m_rotationLabel, m_fieldOfViewLabel, m_projectionLabel, m_equinoxLabel, m_sipLabel, m_solverLabel})
        {
            setRowValue(label, QStringLiteral("-"));
        }
        return;
    }

    // The cards are read under a lease that never waits; while a writer holds the image the solver and the equinox
    // read as unknown until its next update.
    std::vector<an::CodecCard> cards;
    if (an::Result<an::consumer::AssetReadLease> lease = m_currentViewControl->document().tryRead(); lease.hasValue())
    {
        lease->visit([&cards](const auto& typed) {
            const auto stated = typed.metadata().codec().cards();
            cards.assign(stated.begin(), stated.end());
        });
    }
    const an::ui::ImageAstrometryInfoSummary summary =
        an::ui::buildImageAstrometryInfoSummary(m_currentViewControl->imageSolution(), cards);

    m_statusLabel->setText(summary.statusMessage);
    m_statusLabel->setProperty("anStatusTone",
                               summary.hasSolution ? QStringLiteral("secondary") : QStringLiteral("warning"));
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->update();

    setRowValue(m_fieldCenterRaLabel, summary.fieldCenterRa);
    setRowValue(m_fieldCenterDecLabel, summary.fieldCenterDec);
    setRowValue(m_crvalRaLabel, summary.crvalRa);
    setRowValue(m_crvalDecLabel, summary.crvalDec);
    setRowValue(m_imageScaleLabel, summary.imageScale);
    setRowValue(m_rotationLabel, summary.rotation);
    setRowValue(m_fieldOfViewLabel, summary.fieldOfView);
    setRowValue(m_projectionLabel, summary.projection);
    setRowValue(m_equinoxLabel, summary.equinox);
    setRowValue(m_sipLabel, summary.sip);
    setRowValue(m_solverLabel, summary.solver);
}

void ImageAstrometryInfoControl::setRowValue(QLabel* label, const QString& value) const
{
    if (!label)
    {
        return;
    }

    label->setText(value.trimmed().isEmpty() ? QStringLiteral("-") : value);
}

} // namespace an::ui
