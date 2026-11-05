// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The transfer curves the histogram-transformation and generalized-stretch widgets draw over their histograms: each is
/// the curve the engine evaluates for the widget's values - the midtones transfer of its shadows, midtones and
/// highlights, and the generalized stretch, or the screen transfer of the channel its listener names - drawn where the
/// engine puts it.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "widgets/HistogramGHTWidget.h"
#include "widgets/HistogramMTFWidget.h"

#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>
#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QResizeEvent>
#include <QScrollArea>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace
{

constexpr int kWidth = 200;
constexpr int kHeight = 100;

void resizeTo(QWidget& widget, const QSize& size)
{
    const QSize old = widget.size();
    widget.resize(size);
    QResizeEvent event(size, old);
    QCoreApplication::sendEvent(&widget, &event);
}

/// Where the drawn image has nothing other than the background within two rows of where a curve puts a column, or
/// something more than ten rows from it in the same column; empty when the curve is drawn where it belongs.
[[nodiscard]] std::string curveMismatches(const QImage& image, const QColor& background,
                                          const std::function<double(double)>& curve)
{
    const auto drawn = [&](const int x, const int y) {
        const QColor colour = image.pixelColor(x, y);
        return std::abs(colour.red() - background.red()) + std::abs(colour.green() - background.green()) +
                   std::abs(colour.blue() - background.blue()) >
               60;
    };
    std::string mismatches;
    for (int x = 35; x < kWidth - 20; x += 15)
    {
        const double expected = kHeight * (1.0 - curve(static_cast<double>(x) / kWidth));
        bool near = false;
        bool far = false;
        for (int y = 0; y < kHeight; ++y)
        {
            if (!drawn(x, y))
            {
                continue;
            }
            if (std::abs(y - expected) <= 2.5)
            {
                near = true;
            }
            else if (std::abs(y - expected) > 10.0)
            {
                far = true;
            }
        }
        if (!near || far)
        {
            mismatches += " column " + std::to_string(x) + (near ? " drew elsewhere too;" : " drew nothing near;");
        }
    }
    return mismatches;
}

/// A listener that states the stretch the widget draws.
class StretchListener : public HistogramGHTWidgetListener
{
public:
    void onProtectShadowsChanged(double) override {}
    void onSymmetryPointChanged(double) override {}
    void onProtectHightlightsChanged(double) override {}
    void onSettingsUpdated() override {}
    void onSPPickerUpdated(double) override {}

    an::GeneralizedStretchParameters stretchParameters() override
    {
        return parameters;
    }

    std::uint32_t transferCurveChannel() const override
    {
        return channel;
    }

    an::GeneralizedStretchParameters parameters;
    std::uint32_t channel = 0u;
};

} // namespace

TEST(TransferCurveTest, TheMidtonesWidgetDrawsTheEnginesMidtonesTransfer)
{
    QScrollArea area;
    HistogramMTFWidget widget(&area);
    widget.setGridEnabled(false);
    widget.setGuideLineEnabled(false);
    resizeTo(widget, QSize(kWidth, kHeight));
    widget.setValues(0.1, 0.25, 0.9);

    const an::MidtonesTransferParameters parameters{.midtones = 0.25, .shadows = 0.1, .highlights = 0.9};
    EXPECT_EQ(curveMismatches(widget.grab().toImage(), widget.getBackgroundColor(),
                              [&](double x) { return *an::evaluateMidtonesTransfer(x, parameters); }),
              "");
}

TEST(TransferCurveTest, TheGeneralizedStretchWidgetDrawsTheEnginesStretch)
{
    QScrollArea area;
    HistogramGHTWidget widget(&area);
    StretchListener listener;
    listener.parameters.function = an::StretchFunction::GeneralizedHyperbolic;
    listener.parameters.amount = 4.0;
    listener.parameters.bias = 0.5;
    listener.parameters.symmetryPoint = 0.2;
    widget.setListener(&listener);
    widget.setGridEnabled(false);
    widget.setGuideLineEnabled(false);
    resizeTo(widget, QSize(kWidth, kHeight));

    EXPECT_EQ(curveMismatches(widget.grab().toImage(), widget.getBackgroundColor(),
                              [&](double x) { return *an::evaluateGeneralizedStretch(x, listener.parameters); }),
              "");
    widget.setListener(nullptr);
}

// A screen transfer draws the triplet of the channel the listener names.
TEST(TransferCurveTest, AScreenTransferDrawsTheChannelTheListenerNames)
{
    QScrollArea area;
    HistogramGHTWidget widget(&area);
    StretchListener listener;
    listener.parameters.function = an::StretchFunction::ScreenTransfer;
    listener.parameters.screenTransfer = {an::MidtonesTransferParameters{.midtones = 0.5},
                                          an::MidtonesTransferParameters{.midtones = 0.1, .shadows = 0.05},
                                          an::MidtonesTransferParameters{.midtones = 0.9}};
    listener.channel = 1u;
    widget.setListener(&listener);
    widget.setGridEnabled(false);
    widget.setGuideLineEnabled(false);
    resizeTo(widget, QSize(kWidth, kHeight));

    EXPECT_EQ(curveMismatches(widget.grab().toImage(), widget.getBackgroundColor(),
                              [&](double x) { return *an::evaluateGeneralizedStretch(x, listener.parameters, 1u); }),
              "");
    widget.setListener(nullptr);
}
