// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the cases that open the processes library's own dialogs share: a dialog opened over a view as the launcher
/// opens one, kept by the case and deleted once everything it started has landed; the event loop turned until a
/// dialog, its preview view and everything else on the thread pool - its histograms' counts among them - have landed;
/// views of an asset and of a fixture scene, let go by the context that registered them; and the histograms a dialog
/// draws.
#pragma once

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/ImageRTPreviewControl.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureImage.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QThreadPool>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::dialogs
{

namespace fixtures = an::tests::fixtures;

/// A view, let go by the context that registered it and deleted with the case, so a dialog opened later lists only
/// the views that still exist.
struct SceneViewDeleter
{
    void operator()(ImageViewControl* view) const
    {
        if (view == nullptr)
        {
            return;
        }
        if (an::ui::IAppContext* context = an::ui::appContext())
        {
            if (an::ui::IViewControlManager* manager = context->getViewControlManager())
            {
                manager->removeViewControl(view, false);
            }
        }
        delete view;
        QCoreApplication::processEvents();
    }
};
using SceneView = std::unique_ptr<ImageViewControl, SceneViewDeleter>;

/// A view of a document over an asset, its history begun as the application begins it; fails the case when the
/// document cannot be made.
[[nodiscard]] inline SceneView assetView(an::consumer::ImageAsset&& asset, const std::string& name)
{
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(asset), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return SceneView();
    }
    SceneView view(new ImageViewControl());
    view->initialize(name, ViewControlMode::standard);
    view->resize(640, 480);
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image opened");
    return view;
}

/// A view of a fixture scene's samples; fails the case when the scene cannot be made.
[[nodiscard]] inline SceneView sceneView(const an::consumer::EngineSession& session,
                                         const fixtures::FixtureImage& scene, const fixtures::FixtureSamples& samples)
{
    an::Result<an::consumer::ImageAsset> asset = fixtures::engine::makeImageAsset(session, scene, samples);
    EXPECT_TRUE(asset.hasValue()) << scene.name << ": " << asset.status().message();
    if (!asset.hasValue())
    {
        return SceneView();
    }
    return assetView(std::move(*asset), scene.name);
}

/// A dialog, deleted once everything it started has landed.
struct DialogDeleter
{
    void operator()(ImageRTPreviewControl* dialog) const
    {
        if (dialog == nullptr)
        {
            return;
        }
        (void)pumpUntil([dialog]() { return dialog->isProcessIdle(); });
        delete dialog;
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
    }
};
using DialogHandle = std::unique_ptr<ImageRTPreviewControl, DialogDeleter>;

/// A dialog of a process over a target, as the launcher opens one, without the delete-on-close the framework sets so
/// the case decides when it goes.
template <typename ProcessT>
[[nodiscard]] DialogHandle openDialog(ImageViewControl& target, const QSize& size = QSize(900, 640))
{
    DialogHandle dialog(new ProcessT(&target));
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    dialog->resize(size);
    return dialog;
}

/// Whether a dialog's runs and its preview view's work have landed and nothing at all runs on the thread pool, where
/// a dialog's histograms are counted.
[[nodiscard]] inline bool dialogIdle(const ImageRTPreviewControl& dialog)
{
    return dialog.isProcessIdle() && QThreadPool::globalInstance()->activeThreadCount() == 0;
}

/// Turns the event loop until a dialog and everything it started have landed and stay landed through a quiet period:
/// a histogram is counted on the thread pool after the preview that asked for it was shown, and drawn from the event
/// loop after that.
[[nodiscard]] inline bool settleDialog(const ImageRTPreviewControl& dialog)
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (!pumpUntil([&dialog]() { return dialogIdle(dialog); }))
        {
            return false;
        }
        pumpQuietly();
        if (dialogIdle(dialog))
        {
            return true;
        }
    }
    return false;
}

/// A child widget of a dialog by the name its layout gives it; fails the case when there is none.
template <typename WidgetT> [[nodiscard]] WidgetT* child(const QWidget& dialog, const char* name)
{
    auto* widget = dialog.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the dialog has no " << name;
    return widget;
}

/// Sets a spin box of a dialog, as a person typing a value does.
inline void setInteger(const QWidget& dialog, const char* name, int value)
{
    if (auto* box = child<QSpinBox>(dialog, name))
    {
        box->setValue(value);
    }
}

/// Sets a floating spin box of a dialog, as a person typing a value does.
inline void setReal(const QWidget& dialog, const char* name, double value)
{
    if (auto* box = child<QDoubleSpinBox>(dialog, name))
    {
        box->setValue(value);
    }
}

/// Checks or clears a check box or a checkable button of a dialog, as a click does.
inline void setChecked(const QWidget& dialog, const char* name, bool checked)
{
    if (auto* button = child<QAbstractButton>(dialog, name))
    {
        button->setChecked(checked);
    }
}

/// Chooses an entry of a combo box of a dialog by its text, as a person picking it does.
inline void choose(const QWidget& dialog, const char* name, const QString& text)
{
    if (auto* box = child<QComboBox>(dialog, name))
    {
        const int index = box->findText(text);
        EXPECT_GE(index, 0) << name << " offers no " << text.toStdString();
        box->setCurrentIndex(index);
    }
}

/// Whether a combo box of a dialog offers a choice at all: a grey target's channel selectors offer the whole image
/// alone.
[[nodiscard]] inline bool offersChoice(const QWidget& dialog, const char* name)
{
    const auto* box = child<QComboBox>(dialog, name);
    return box != nullptr && box->count() > 1;
}

/// The counts every histogram of a dialog draws now, in the order the dialog lays its histograms out; a histogram that
/// draws nothing is left out.
[[nodiscard]] inline std::vector<std::shared_ptr<const an::ui::HistogramDisplayData>>
drawnHistograms(const QWidget& dialog)
{
    std::vector<std::shared_ptr<const an::ui::HistogramDisplayData>> drawn;
    for (QWidget* widget : dialog.findChildren<QWidget*>())
    {
        if (const auto* histogram = dynamic_cast<const HistogramWidget*>(widget))
        {
            std::shared_ptr<const an::ui::HistogramDisplayData> counts = histogram->getHistogram();
            if (counts && counts->isValid())
            {
                drawn.push_back(std::move(counts));
            }
        }
    }
    return drawn;
}

} // namespace an::tests::processes::dialogs
