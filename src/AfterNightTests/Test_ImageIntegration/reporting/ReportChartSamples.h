// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Series for the report's charts, and a page that shows every chart on them.
///
/// The charts take plain series, so a suite needs no run to draw one: these are the lights of an invented session -
/// five filters over two nights, cloud over part of the first - with every state a light can end in. The figures
/// come from a fixed sequence, so a chart of them is the same on every host and in every run.
///
/// The page is how a person looks at the charts before the report has a document to put them in. Its stylesheet is
/// a stand-in for the report's own: it gives the charts' classes their colours in both themes, and the colours are
/// stated here as values so a case can hold their contrast.

#include "processes/ImageIntegration/reporting/ReportCharts.h"

#include <QString>

#include <array>
#include <vector>

namespace ReportChartSamples
{
/// One light of the invented session.
struct SampleLight
{
    QString group;
    int groupIndex = 0;
    int night = 0;

    /// Its number within its group, from one.
    int number = 0;

    double startMinutes = 0.0;
    double exposureMinutes = 0.0;

    /// Its measures; not a number where it states none.
    double fwhm = 0.0;
    double eccentricity = 0.0;
    double stars = 0.0;
    double snr = 0.0;
    double background = 0.0;
    double weight = 0.0;
    double rejected = 0.0;
    double shiftX = 0.0;
    double shiftY = 0.0;

    ImageIntegration::Reporting::ChartState state = ImageIntegration::Reporting::ChartState::Used;
};

/// The lights of a session: `groups` filters taken one after another on each of `nights` nights, `perGroup` lights
/// of each in all. Some are left out: a stretch of the first group's first night fails registration, and the first
/// group has one light excluded by the user that states no measure, one dropped by the stack and one excluded on a
/// restack.
[[nodiscard]] std::vector<SampleLight> sampleLights(int groups, int perGroup, int nights);

/// How many of some lights are in a state other than used.
[[nodiscard]] std::size_t leftOutOf(const std::vector<SampleLight>& lights);

/// The lights of one group.
[[nodiscard]] std::vector<SampleLight> lightsOfGroup(const std::vector<SampleLight>& lights, int groupIndex);

/// What a description says when a chart has nothing to draw.
[[nodiscard]] QString nothingToDraw();

// ---- Each chart on the lights.

[[nodiscard]] ImageIntegration::Reporting::ConditionsChart conditionsOf(const std::vector<SampleLight>& lights,
                                                                        int nights);
[[nodiscard]] ImageIntegration::Reporting::SegmentedBar usedAndLeftOutOf(const std::vector<SampleLight>& lights);
[[nodiscard]] ImageIntegration::Reporting::RangeChart rangeOf(const std::vector<SampleLight>& lights, int groups);
[[nodiscard]] ImageIntegration::Reporting::ScatterChart scatterOf(const std::vector<SampleLight>& lights);
[[nodiscard]] ImageIntegration::Reporting::DistributionChart weightsOf(const std::vector<SampleLight>& lights);
[[nodiscard]] ImageIntegration::Reporting::FrameMeasureChart measuresOf(const std::vector<SampleLight>& lights,
                                                                        const QString& id);
[[nodiscard]] ImageIntegration::Reporting::DitherChart ditherOf(const std::vector<SampleLight>& lights, int nights);
[[nodiscard]] ImageIntegration::Reporting::SampleDistributionChart samplesOf(int channels);
[[nodiscard]] ImageIntegration::Reporting::CoverageBar coverage();
[[nodiscard]] ImageIntegration::Reporting::TimelineChart timeline();
[[nodiscard]] ImageIntegration::Reporting::BarsChart bars();
[[nodiscard]] ImageIntegration::Reporting::RunsChart runs();

/// The legend of the states some lights are in.
[[nodiscard]] std::vector<ImageIntegration::Reporting::ChartLegendItem>
stateLegendOf(const std::vector<SampleLight>& lights);

/// The legend of the nights of a dither chart.
[[nodiscard]] std::vector<ImageIntegration::Reporting::ChartLegendItem> nightLegend(int nights);

// ---- The page.

/// One chart of the page, with the words around it.
struct SampleChart
{
    QString title;

    /// What the chart is drawn of.
    QString note;

    /// The chart's markup, and its legend's when it has one.
    QString markup;
    QString legend;

    /// Whether the chart takes the page's whole width and scrolls sideways on a phone.
    bool wide = false;
};

/// Every chart, on the session of five filters and on the cases a report meets: one master, many nights, a group
/// too large to mark light by light, a run that stopped, and nothing to draw.
[[nodiscard]] std::vector<SampleChart> everySampleChart();

/// The colours the stand-in stylesheet gives one theme.
struct StandInTheme
{
    const char* name;

    /// The surfaces a chart sits on: a card, and the page a master's inner panels take.
    const char* card;
    const char* background;

    /// The three inks of text.
    const char* ink;
    const char* ink2;
    const char* ink3;

    /// The two series.
    const char* series;
    const char* series2;

    /// The status colours.
    const char* success;
    const char* warning;
    const char* error;

    /// Lines that carry no value: the grid and the axes.
    const char* grid;
    const char* axis;
};

/// The stand-in stylesheet's two themes: dark, then light.
[[nodiscard]] const std::array<StandInTheme, 2>& standInThemes();

/// The stand-in stylesheet: the themes' colours as custom properties and a rule for each class the charts use.
[[nodiscard]] QString standInStylesheet();

/// A document that shows every sample chart, well-formed XML and HTML at once. It follows the reader's theme and
/// takes `?theme=light` or `?theme=dark`; its script shows the tips and does nothing else.
[[nodiscard]] QString samplePage();
} // namespace ReportChartSamples
