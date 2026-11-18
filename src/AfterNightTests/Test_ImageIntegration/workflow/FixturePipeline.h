// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A pipeline of the suites' own for the cases that follow a run through a front end: two stages - bias and
/// registration - that read no image and write none. Each reports the progress it was scripted with, one report at a
/// time, and waits before each for the case to let it pass (`RunGate`), so a case sees the front end at every step of
/// the run and never races it. The manifest that goes with it names the two stages and an output folder, where the
/// run leaves its record as any run does.

#include "processes/ImageIntegration/workflow/CancellationToken.h"
#include "processes/ImageIntegration/workflow/ProgressReporter.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/IWorkflowStage.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace an::tests::integration
{

/// What lets a scripted run go on: a stage waits before each of its reports until the case allows one more, or all.
class RunGate
{
public:
    /// Lets that many further reports pass.
    void allow(const int more = 1)
    {
        m_allowed.fetch_add(more);
    }

    /// Lets every report pass from now on: the run goes to its end.
    void open()
    {
        m_open.store(true);
    }

    /// How many reports the run has made.
    [[nodiscard]] int passed() const
    {
        return m_passed.load();
    }

    /// Waits for the case; whether the run goes on - false once it was cancelled.
    [[nodiscard]] bool pass(CancellationToken& token)
    {
        while (!m_open.load() && m_passed.load() >= m_allowed.load())
        {
            if (token.isCancellationRequested())
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (token.isCancellationRequested())
        {
            return false;
        }
        m_passed.fetch_add(1);
        return true;
    }

private:
    std::atomic<int> m_allowed{0};
    std::atomic<int> m_passed{0};
    std::atomic<bool> m_open{false};
};

/// A stage that makes the reports it was given, each once the gate lets it, and then succeeds; cancelled, it says so.
class ScriptedStage final : public IWorkflowStage
{
public:
    ScriptedStage(QString name, const WorkflowStage stage, std::vector<WorkflowProgress> reports,
                  std::shared_ptr<RunGate> gate)
        : m_name(std::move(name)), m_stage(stage), m_reports(std::move(reports)), m_gate(std::move(gate))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return m_name;
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return m_stage;
    }

    StageResult execute(const WorkflowManifest& /*manifest*/, CancellationToken& token,
                        ProgressReporter& reporter) override
    {
        // Every report reaches the front end: none is held back as too close to the one before.
        reporter.setMinimumInterval(std::chrono::milliseconds(0));
        for (WorkflowProgress report : m_reports)
        {
            if (!m_gate->pass(token))
            {
                return StageResult::cancelled();
            }
            report.currentStage = m_stage;
            reporter.report(report);
        }
        return token.isCancellationRequested() ? StageResult::cancelled() : StageResult::success();
    }

private:
    QString m_name;
    WorkflowStage m_stage;
    std::vector<WorkflowProgress> m_reports;
    std::shared_ptr<RunGate> m_gate;
};

/// A report of a stage: how far the stage has come over its groups, what it does, and the part of it it is at.
[[nodiscard]] inline WorkflowProgress scriptedReport(const float stageFraction, const int group, const int groups,
                                                     const QString& operation, const QString& file = {},
                                                     const int subStep = 0, const int subSteps = 0,
                                                     const QString& subStepName = {})
{
    WorkflowProgress progress;
    progress.currentGroup = group;
    progress.totalGroups = groups;
    progress.stageProgress = stageFraction;
    progress.overallProgress = stageFraction;
    progress.currentOperation = operation;
    progress.currentFile = file;
    progress.currentSubStep = subStep;
    progress.totalSubSteps = subSteps;
    progress.subStepName = subStepName;
    return progress;
}

/// How many reports the fixture pipeline makes: two of its bias stage, then three of its registration.
inline constexpr int kFixtureReports = 5;

/// The fixture pipeline: a bias stage of two reports, then a registration of three, the last two in the parts
/// "Detect" and "Match" of its one group.
[[nodiscard]] inline std::function<std::unique_ptr<WorkflowExecutor>()> fixturePipeline(std::shared_ptr<RunGate> gate)
{
    return [gate]() {
        auto executor = std::make_unique<WorkflowExecutor>();
        executor->addStage(std::make_unique<ScriptedStage>(
            QStringLiteral("BiasStacking"), WorkflowStage::Bias,
            std::vector<WorkflowProgress>{
                scriptedReport(0.5f, 1, 1, QStringLiteral("Stacking bias"), QStringLiteral("bias_01.fit")),
                scriptedReport(1.0f, 1, 1, QStringLiteral("Saving master bias"))},
            gate));
        executor->addStage(std::make_unique<ScriptedStage>(
            QStringLiteral("LightsRegistration"), WorkflowStage::LightsRegistration,
            std::vector<WorkflowProgress>{
                scriptedReport(0.25f, 1, 1, QStringLiteral("Reading lights"), QStringLiteral("light_01.fit")),
                scriptedReport(0.5f, 1, 1, QStringLiteral("Detecting stars"), QStringLiteral("light_02.fit"), 1, 2,
                               QStringLiteral("Detect")),
                scriptedReport(0.5f, 1, 1, QStringLiteral("Matching stars"), QStringLiteral("light_02.fit"), 2, 2,
                               QStringLiteral("Match"))},
            gate));
        return executor;
    };
}

/// The file a fixture run that stacks leaves in its output folder.
inline constexpr char kFixtureStackedFile[] = "stacked_fixture.fit";

/// A stacking stage of the suites' own: one report, once the gate lets it, then a file written into the run's output
/// folder and named in the stage's result as a stack names its master. The file holds no image: it is there to be
/// found.
class ScriptedStackingStage final : public IWorkflowStage
{
public:
    explicit ScriptedStackingStage(std::shared_ptr<RunGate> gate) : m_gate(std::move(gate)) {}

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("LightsStacking");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::LightsStacking;
    }

    StageResult execute(const WorkflowManifest& manifest, CancellationToken& token, ProgressReporter& reporter) override
    {
        reporter.setMinimumInterval(std::chrono::milliseconds(0));
        if (!m_gate->pass(token))
        {
            return StageResult::cancelled();
        }
        WorkflowProgress report = scriptedReport(0.5f, 1, 1, QStringLiteral("Stacking"));
        report.currentStage = WorkflowStage::LightsStacking;
        reporter.report(report);

        IntegrationGroupResult group;
        group.groupKey = QStringLiteral("fixture-integration");
        group.outputFilename = QString::fromLatin1(kFixtureStackedFile);
        group.outputPath = QDir(manifest.outputDirectory).filePath(group.outputFilename);
        QFile stacked(group.outputPath);
        if (!stacked.open(QIODevice::WriteOnly) || stacked.write("stack") < 0)
        {
            return StageResult::failure(QStringLiteral("The fixture's stacked file was not written"));
        }
        stacked.close();
        group.success = true;
        StageResult result = StageResult::success();
        result.groupResults.push_back(std::move(group));
        return result;
    }

private:
    std::shared_ptr<RunGate> m_gate;
};

/// The fixture pipeline with a stack at its end: its two stages, then a stacking stage of one report that leaves
/// `kFixtureStackedFile` in the output folder, so the run has a master to name. Six reports in all.
[[nodiscard]] inline std::function<std::unique_ptr<WorkflowExecutor>()>
fixturePipelineWithAStack(std::shared_ptr<RunGate> gate)
{
    return [gate]() {
        std::unique_ptr<WorkflowExecutor> executor = fixturePipeline(gate)();
        executor->addStage(std::make_unique<ScriptedStackingStage>(gate));
        return executor;
    };
}

/// The manifest the fixture pipeline runs: its two stages, by the names a manifest's stage sequence gives them, the
/// folder the run leaves its record in, and a registration and a stacking group of no frame, which a run asks every
/// manifest for.
[[nodiscard]] inline WorkflowManifest fixtureManifest(const QString& outputDirectory)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = outputDirectory;
    manifest.stageSequence = QStringList{QStringLiteral("Bias"), QStringLiteral("LightsRegistration")};

    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("fixture-registration")};
    manifest.registrationGroups.push_back(registration);

    WorkflowManifest::IntegrationGroup integration;
    integration.groupKey = IntegrationGroupKey{GroupKey{QStringLiteral("fixture-integration")}};
    integration.registrationGroupKey = registration.groupKey;
    integration.outputFilename = QString::fromLatin1(kFixtureStackedFile);
    manifest.integrationGroups.push_back(integration);
    return manifest;
}

/// The manifest of the fixture pipeline with a stack at its end.
[[nodiscard]] inline WorkflowManifest fixtureManifestWithAStack(const QString& outputDirectory)
{
    WorkflowManifest manifest = fixtureManifest(outputDirectory);
    manifest.stageSequence.append(QStringLiteral("LightsStacking"));
    return manifest;
}

} // namespace an::tests::integration
