// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProfiledStarMaskCreatorControl.h"
#include "controls/ImageViewControl.h"
#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QLayout>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <memory>
#include <optional>
#include <utility>

namespace
{

/// The session every Controls operation runs in, or null when the application has none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

/// A new grey Float32 image of the frame's size, every sample zero.
[[nodiscard]] an::Result<an::Image<float>> blankMask(const an::EngineContext& engine, const std::uint32_t width,
                                                     const std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, engine.limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the frame's size cannot hold a mask");
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "a mask of the frame's size cannot be described");
    }
    return an::Image<float>::create(engine, *descriptor, an::StorageArrangement::Planar,
                                    an::StorageInitialization::Zeroed);
}

} // namespace

namespace an::ui
{

an::Result<an::consumer::ImageAsset> makeProfiledStarMask(const an::consumer::EngineSession& session,
                                                          std::span<const an::DetectedStar> stars,
                                                          const std::uint32_t width, const std::uint32_t height,
                                                          const ProfiledStarMaskRequest& request, std::stop_token stop)
{
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::Image<float>> mask = blankMask(scope->engine(), width, height);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        const std::optional<an::ImageWriteAccess<float>> access = mask->write();
        if (!access.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the mask holds no samples");
        }
        const an::ImageLayout layout = access->view().layout();
        const an::Result<an::StarMaskResult> painted =
            request.shaped ? an::paintShapedStarMask(scope->execution(), stars,
                                                     an::StarMaskShapeRequest{.shape = request.shape,
                                                                              .minimumSize = 0.0,
                                                                              .maximumSize = 100.0,
                                                                              .sizeScale = request.sizeScale,
                                                                              .sizeIncrease = request.sizeIncrease},
                                                     an::SampleType::Float32, access->view().data(), layout)
                           : an::paintEllipticalStarMask(scope->execution(), stars,
                                                         an::StarMaskEllipseRequest{.sizeScale = request.scaleFactor},
                                                         an::SampleType::Float32, access->view().data(), layout);
        if (!painted.hasValue())
        {
            return painted.status();
        }
    }

    // Solid ellipses with softened edges are blurred into a second image by a Gaussian of that standard deviation.
    if (!request.shaped && request.softenEdges > 0.0 && !stars.empty())
    {
        const an::Result<an::GaussianPlan> plan =
            an::planGaussianBlur(an::GaussianRequest{.sigmaX = request.softenEdges, .sigmaY = request.softenEdges});
        if (!plan.hasValue())
        {
            return plan.status();
        }
        an::Result<an::Image<float>> softened = blankMask(scope->engine(), width, height);
        if (!softened.hasValue())
        {
            return softened.status();
        }
        {
            const std::optional<an::ImageView<const float>> source = std::as_const(*mask).view();
            const std::optional<an::ImageWriteAccess<float>> destination = softened->write();
            if (!source.has_value() || !destination.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "the mask holds no samples");
            }
            const an::ImageLayout sourceLayout = source->layout();
            const an::ImageLayout destinationLayout = destination->view().layout();
            const an::Result<an::SpatialFilterResult> blurred =
                an::blurImageGaussian(scope->execution(), an::SampleType::Float32, source->data(), sourceLayout,
                                      destination->view().data(), destinationLayout, *plan);
            if (!blurred.hasValue())
            {
                return blurred.status();
            }
        }
        mask = std::move(softened);
    }
    scope->end();
    return an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*mask)));
}

an::StarMaskShape starMaskShapeFromText(const QString& text)
{
    if (text == "Bell")
    {
        return an::StarMaskShape::Bell;
    }
    else if (text == "Truncated Bell")
    {
        return an::StarMaskShape::TruncatedBell;
    }
    else if (text == "Linear")
    {
        return an::StarMaskShape::Linear;
    }
    else if (text == "Truncated Linear")
    {
        return an::StarMaskShape::TruncatedLinear;
    }
    else if (text == "Cubic")
    {
        return an::StarMaskShape::Cubic;
    }
    else if (text == "Quadratic")
    {
        return an::StarMaskShape::Quadratic;
    }

    return an::StarMaskShape::TruncatedBell;
}

} // namespace an::ui

ProfiledStarMaskCreatorControl::ProfiledStarMaskCreatorControl(StarsFilterControlHandler* parent)
    : QDialog(dynamic_cast<QWidget*>(parent)), StarsFilterControlBase(parent)
{
    m_ui = an::ui::controls::buildProfiledStarMaskCreatorControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ProfiledStarMaskCreatorControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.binaryAreasGroup, &QGroupBox::clicked, this,
                     &ProfiledStarMaskCreatorControl::on_BinaryAreasGroupBoxClicked);
    QObject::connect(m_ui.shapedAreasGroup, &QGroupBox::clicked, this,
                     &ProfiledStarMaskCreatorControl::on_ShapedAreasGroupBoxClicked);

    INIT_D_SPINBOX_CTRL(scaleFactorControl, m_ui.scaleFactorValue, m_ui.scaleFactorSlider,
                        &ProfiledStarMaskCreatorControl::on_scaleFactorChanged);
    INIT_D_SPINBOX_CTRL(softenEdgesControl, m_ui.softenEdgesValue, m_ui.softenEdgesSlider,
                        &ProfiledStarMaskCreatorControl::on_softenEdgesChanged);
    INIT_D_SPINBOX_CTRL(percentPixelIncreaseControl, m_ui.percentPixelIncreaseValue, m_ui.percentPixelIncreaseSlider,
                        &ProfiledStarMaskCreatorControl::on_percentPixelIncreaseChanged);
    INIT_SPINBOX_CTRL(pixelIncreaseControl, m_ui.pixelIncreaseValue, m_ui.pixelIncreaseSlider,
                      &ProfiledStarMaskCreatorControl::on_pixelIncreaseChanged);

    std::string title = fmt::format("Profiled Mask Generation <{}>", m_Process->getViewControl()->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));

    initialize();

    // Keep the dialog at its final content-driven size to avoid control overlap.
    if (auto* root_layout = layout())
    {
        root_layout->setSizeConstraint(QLayout::SetFixedSize);
        root_layout->activate();
    }
    adjustSize();
    setFixedSize(sizeHint());
}

void ProfiledStarMaskCreatorControl::updateFilter() {}

bool ProfiledStarMaskCreatorControl::isPreviewEnabled() const
{
    return false;
}

void ProfiledStarMaskCreatorControl::forceClose() {}

ProfiledStarMaskCreatorControl::~ProfiledStarMaskCreatorControl() = default;

void ProfiledStarMaskCreatorControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply || sb == QDialogButtonBox::Ok)
    {
        generateMask();
        this->close();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        m_ui.binaryAreasGroup->setChecked(true);
        m_ui.shapedAreasGroup->setChecked(false);

        scaleFactorControl->setValue(1.0);
        softenEdgesControl->setValue(0.0);

        percentPixelIncreaseControl->setValue(0.5);
        pixelIncreaseControl->setValue(0);

        m_ui.shapeSelector->setCurrentText("Truncated Bell");
    }
    else if (sb == QDialogButtonBox::Close || sb == QDialogButtonBox::Cancel)
    {
        this->close();
    }
}

void ProfiledStarMaskCreatorControl::on_scaleFactorChanged(double value) {}

void ProfiledStarMaskCreatorControl::on_softenEdgesChanged(double value) {}

void ProfiledStarMaskCreatorControl::on_percentPixelIncreaseChanged(double value) {}

void ProfiledStarMaskCreatorControl::on_pixelIncreaseChanged(int value) {}

void ProfiledStarMaskCreatorControl::on_BinaryAreasGroupBoxClicked(bool on)
{
    if (m_IgnoreLastGroupUpdatedEvent)
        return;

    if (on)
    {
        m_ui.shapedAreasGroup->setChecked(false);
    }
    else
    {
        m_IgnoreLastGroupUpdatedEvent = true;
        m_ui.shapedAreasGroup->setChecked(true);
        m_IgnoreLastGroupUpdatedEvent = false;
    }
}

void ProfiledStarMaskCreatorControl::on_ShapedAreasGroupBoxClicked(bool on)
{
    if (m_IgnoreLastGroupUpdatedEvent)
        return;

    if (on)
    {
        m_ui.binaryAreasGroup->setChecked(false);
    }
    else
    {
        m_IgnoreLastGroupUpdatedEvent = true;
        m_ui.binaryAreasGroup->setChecked(true);
        m_IgnoreLastGroupUpdatedEvent = false;
    }
}

void ProfiledStarMaskCreatorControl::done(int r)
{

    QDialog::done(r);
}

void ProfiledStarMaskCreatorControl::initialize()
{
    m_ui.binaryAreasGroup->setChecked(true);
    m_ui.shapedAreasGroup->setChecked(false);

    m_ui.shapeSelector->addItem("Bell");
    m_ui.shapeSelector->addItem("Truncated Bell");
    m_ui.shapeSelector->addItem("Linear");
    m_ui.shapeSelector->addItem("Truncated Linear");
    m_ui.shapeSelector->addItem("Cubic");
    m_ui.shapeSelector->addItem("Quadratic");

    m_ui.shapeSelector->setCurrentText("Truncated Bell");

    percentPixelIncreaseControl->setValue(0.5);
    pixelIncreaseControl->setValue(0);
}

an::ui::ProfiledStarMaskRequest ProfiledStarMaskCreatorControl::maskRequest() const
{
    an::ui::ProfiledStarMaskRequest request;
    request.shaped = !m_ui.binaryAreasGroup->isChecked();
    request.scaleFactor = scaleFactorControl->getValue();
    request.softenEdges = softenEdgesControl->getValue();
    request.shape = an::ui::starMaskShapeFromText(m_ui.shapeSelector->currentText());
    request.sizeScale = percentPixelIncreaseControl->getValue();
    request.sizeIncrease = static_cast<double>(pixelIncreaseControl->getValue());
    return request;
}

bool ProfiledStarMaskCreatorControl::generateMask()
{
    ImageViewControl* view = m_Process != nullptr ? m_Process->getViewControl() : nullptr;
    const an::consumer::EngineSession* session = currentSession();
    const std::optional<an::ImageDescriptor> descriptor =
        view != nullptr ? view->imageDescriptor() : std::optional<an::ImageDescriptor>{};
    if (view == nullptr || session == nullptr || !descriptor.has_value())
    {
        return false;
    }

    an::ui::StarListPtr stars = m_Process->getCurrentStarList();
    if (!stars)
    {
        stars = std::make_shared<const an::ui::StarList>();
    }
    const std::uint32_t width = descriptor->extent().width();
    const std::uint32_t height = descriptor->extent().height();
    const an::ui::ProfiledStarMaskRequest request = maskRequest();
    const QString name = QString::fromStdString(view->getUniqueName() + " - STARMASK");
    const std::string history = fmt::format("Star Profiler: Starmask image created from '{}'", view->getUniqueName());

    // The mask lands in a new view beside the source, whether this window is still open or not.
    QPointer<ImageViewControl> source(view);
    const auto land = [source, name, history](an::Result<an::consumer::ImageAsset>&& mask) {
        auto* context = an::ui::appContext();
        if (!mask.hasValue())
        {
            if (auto* dialogService = context != nullptr ? context->getDialogService() : nullptr)
            {
                dialogService->notifyErrorMessage(source, QObject::tr("Profiled Mask Generation"),
                                                  QString::fromUtf8(mask.status().message()));
            }
            return;
        }
        an::Result<an::ui::ImageDocument> document =
            an::ui::ImageDocument::create(std::move(*mask), an::ui::DocumentNames{.displayName = name});
        auto* viewFactory = context != nullptr ? context->getViewFactory() : nullptr;
        if (!document.hasValue() || viewFactory == nullptr)
        {
            return;
        }
        [[maybe_unused]] auto* createdView = viewFactory->createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, history, ViewControlMode::standard, source);
    };
    QThreadPool::globalInstance()->start([session, stars = std::move(stars), width, height, request, land]() {
        auto mask = std::make_shared<an::Result<an::consumer::ImageAsset>>(
            an::ui::makeProfiledStarMask(*session, *stars, width, height, request));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(application, [land, mask]() { land(std::move(*mask)); }, Qt::QueuedConnection);
    });
    return true;
}

const an::ui::controls::ProfiledStarMaskCreatorControlUiElements& ProfiledStarMaskCreatorControl::ui() const
{
    return m_ui;
}
