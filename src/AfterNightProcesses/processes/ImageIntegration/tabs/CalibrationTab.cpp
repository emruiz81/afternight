// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationTab.h"
#include "CalibrationTabUiBuilder.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/tabs/BiasCalibrationPanel.h"
#include "processes/ImageIntegration/tabs/CalibrationGroupTable.h"
#include "processes/ImageIntegration/tabs/DarkCalibrationPanel.h"
#include "processes/ImageIntegration/tabs/FlatCalibrationPanel.h"
#include "processes/ImageIntegration/tabs/LightCalibrationPanel.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"
#include "processes/ImageIntegration/widgets/CalibrationCoverageWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationFrameSummaryWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationIssuesWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationPolicyWidget.h"
#include "processes/ImageIntegration/widgets/CalibrationQuickActionsWidget.h"

#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(CalibrationTab)

class CalibrationTab::Impl
{
public:
    CalibrationTab* q;
    an::ui::imageintegration::CalibrationTabUiElements ui;
    IntegrationDataModel* model;
    QUndoStack* undoStack = nullptr;
    QTimer* debounceTimer = nullptr;
    BiasCalibrationPanel* biasPanel = nullptr;
    DarkCalibrationPanel* darkPanel = nullptr;
    FlatCalibrationPanel* flatPanel = nullptr;
    LightCalibrationPanel* lightPanel = nullptr;
    ImageIntegration::CalibrationCoverageWidget* coverageWidget = nullptr;
    ImageIntegration::CalibrationFrameSummaryWidget* frameSummaryWidget = nullptr;
    ImageIntegration::CalibrationIssuesWidget* issuesWidget = nullptr;
    ImageIntegration::CalibrationPolicyWidget* policyWidget = nullptr;
    ImageIntegration::CalibrationQuickActionsWidget* quickActionsWidget = nullptr;

    CalibrationGroupTable* biasTable = nullptr;
    CalibrationGroupTable* darkTable = nullptr;
    CalibrationGroupTable* flatTable = nullptr;
    CalibrationGroupTable* lightTable = nullptr;
    bool applyingThemeStyling = false;

    explicit Impl(CalibrationTab* parent, IntegrationDataModel* m, QUndoStack* undo)
        : q(parent), model(m), undoStack(undo)
    {
        ui = an::ui::imageintegration::buildCalibrationTabUi(q);
        setupPanels();
        setupDebounceTimer();
        setupConnections();
        updatePanels();
    }

    void setupPanels()
    {
        if (!ui.panelLayout)
        {
            return;
        }

        // Right panel widgets
        coverageWidget = new ImageIntegration::CalibrationCoverageWidget(model, q);
        frameSummaryWidget = new ImageIntegration::CalibrationFrameSummaryWidget(model, q);
        issuesWidget = new ImageIntegration::CalibrationIssuesWidget(model, q);
        policyWidget = new ImageIntegration::CalibrationPolicyWidget(model, q, undoStack);
        quickActionsWidget = new ImageIntegration::CalibrationQuickActionsWidget(model, q, undoStack);

        auto addSeparator = [this]() {
            auto* line = new QFrame(q);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            ui.rightPanelLayout->addWidget(line);
        };

        ui.rightPanelLayout->addWidget(coverageWidget);
        addSeparator();
        ui.rightPanelLayout->addWidget(frameSummaryWidget);
        addSeparator();
        ui.rightPanelLayout->addWidget(issuesWidget);
        addSeparator();
        ui.rightPanelLayout->addWidget(policyWidget);
        addSeparator();
        ui.rightPanelLayout->addWidget(quickActionsWidget);
        ui.rightPanelLayout->addStretch(1);

        biasPanel = new BiasCalibrationPanel(model, q, undoStack);
        darkPanel = new DarkCalibrationPanel(model, q, undoStack);
        flatPanel = new FlatCalibrationPanel(model, q, undoStack);
        lightPanel = new LightCalibrationPanel(model, q, undoStack);

        int insertIndex = ui.panelLayout->count();
        if (insertIndex > 0)
        {
            insertIndex -= 1;
        }

        ui.panelLayout->insertWidget(insertIndex++, biasPanel);
        ui.panelLayout->insertWidget(insertIndex++, darkPanel);
        ui.panelLayout->insertWidget(insertIndex++, flatPanel);
        ui.panelLayout->insertWidget(insertIndex++, lightPanel);

        biasTable = q->findChild<CalibrationGroupTable*>(QStringLiteral("tblBiasCalibration"));
        darkTable = q->findChild<CalibrationGroupTable*>(QStringLiteral("tblDarkCalibration"));
        flatTable = q->findChild<CalibrationGroupTable*>(QStringLiteral("tblFlatCalibration"));
        lightTable = q->findChild<CalibrationGroupTable*>(QStringLiteral("tblLightCalibration"));
    }

    void setupDebounceTimer()
    {
        debounceTimer = new QTimer(q);
        debounceTimer->setSingleShot(true);
        debounceTimer->setInterval(200);
        QObject::connect(debounceTimer, &QTimer::timeout, [this]() { updatePanels(); });
    }

    void setupConnections()
    {
        QObject::connect(model, &IntegrationDataModel::framesAdded, q, &CalibrationTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::framesRemoved, q, &CalibrationTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::frameUpdated, q, &CalibrationTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::groupingChanged, q, &CalibrationTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::calibrationChanged, q, &CalibrationTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::settingsChanged, q, &CalibrationTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::modelReset, q, &CalibrationTab::refreshAll);

        if (lightPanel)
        {
            QObject::connect(lightPanel, &LightCalibrationPanel::selectionChanged,
                             [this](const GroupKey& key) { onLightSelectionChanged(key); });
        }
        if (flatPanel)
        {
            QObject::connect(flatPanel, &FlatCalibrationPanel::selectionChanged,
                             [this](const GroupKey& key) { onFlatSelectionChanged(key); });
        }
        if (biasPanel)
        {
            QObject::connect(biasPanel, &BiasCalibrationPanel::selectionChanged,
                             [this](const GroupKey& key) { onBiasSelectionChanged(key); });
            QObject::connect(
                biasPanel, &BiasCalibrationPanel::goToGroupDefinitionRequested, q,
                [this](const GroupKey& key) { emit q->goToGroupDefinitionRequested(FrameType::Bias, key); });
        }
        if (darkPanel)
        {
            QObject::connect(darkPanel, &DarkCalibrationPanel::selectionChanged,
                             [this](const GroupKey& key) { onDarkSelectionChanged(key); });
            QObject::connect(
                darkPanel, &DarkCalibrationPanel::goToGroupDefinitionRequested, q,
                [this](const GroupKey& key) { emit q->goToGroupDefinitionRequested(FrameType::Dark, key); });
        }
        if (flatPanel)
        {
            QObject::connect(
                flatPanel, &FlatCalibrationPanel::goToGroupDefinitionRequested, q,
                [this](const GroupKey& key) { emit q->goToGroupDefinitionRequested(FrameType::Flat, key); });
        }
        if (lightPanel)
        {
            QObject::connect(
                lightPanel, &LightCalibrationPanel::goToGroupDefinitionRequested, q,
                [this](const GroupKey& key) { emit q->goToGroupDefinitionRequested(FrameType::Light, key); });
        }

        // If focus leaves the calibration tables, clear cross-reference highlights.
        // No green reference lingers while no selection is active.
        if (qApp)
        {
            QObject::connect(qApp, &QApplication::focusChanged, q, [this](QWidget* /*old*/, QWidget* now) {
                // Check whether focus is still inside any calibration panel (table,
                // toggle button, or any other child).  Previously only the tables
                // were checked, so clicking a panel's collapse/expand toggle cleared
                // highlights because the toggle button is outside the table widget.
                auto insidePanel = [&](QWidget* panel) -> bool {
                    return panel && now && (now == panel || panel->isAncestorOf(now));
                };

                if (insidePanel(biasPanel) || insidePanel(darkPanel) || insidePanel(flatPanel) ||
                    insidePanel(lightPanel))
                {
                    return;
                }

                clearHighlights();
            });
        }

        // When a collapsed panel is expanded, its table viewport may not
        // repaint highlighted rows because the viewport had zero height when
        // setHighlightedKeys() was originally called.  Schedule a forced
        // repaint after the expand animation finishes.
        auto scheduleTableRepaint = [this]() {
            const int delayMs = biasPanel ? biasPanel->animationDuration() + 50 : 220;
            QTimer::singleShot(delayMs, q, [this]() {
                if (biasTable)
                    biasTable->viewport()->update();
                if (darkTable)
                    darkTable->viewport()->update();
                if (flatTable)
                    flatTable->viewport()->update();
                if (lightTable)
                    lightTable->viewport()->update();
            });
        };

        auto connectExpandRepaint = [&](BaseCalibrationPanel* panel) {
            if (!panel || !panel->toggleButton())
            {
                return;
            }
            QObject::connect(panel->toggleButton(), &QToolButton::toggled, q, [scheduleTableRepaint](bool expanded) {
                if (expanded)
                {
                    scheduleTableRepaint();
                }
            });
        };

        connectExpandRepaint(biasPanel);
        connectExpandRepaint(darkPanel);
        connectExpandRepaint(flatPanel);
        connectExpandRepaint(lightPanel);
    }

    void setupMatcher(CalibrationMatcher& matcher)
    {
        matcher.setBiasGroups(::collectBiasGroups(model));
        matcher.setDarkGroups(::collectDarkGroups(model));
        matcher.setFlatGroups(::collectFlatGroups(model));
        matcher.setOverrides(model->calibrationOverrides());
    }

    void clearOtherTableSelections(CalibrationGroupTable* except)
    {
        auto clearTable = [&](CalibrationGroupTable* table) {
            if (!table || table == except)
            {
                return;
            }
            const QSignalBlocker blocker(table);
            table->clearSelection();
        };

        clearTable(lightTable);
        clearTable(flatTable);
        clearTable(biasTable);
        clearTable(darkTable);
    }

    void onLightSelectionChanged(const GroupKey& lightGroupKey)
    {
        clearHighlights();
        clearOtherTableSelections(lightTable);

        const auto lightKey = ::lightKeyForGroup(model, lightGroupKey);
        if (!lightKey)
        {
            return;
        }

        CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                                   model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
        setupMatcher(matcher);

        const CalibrationMatchingPolicy policy = model->calibrationMatchingPolicy();

        // Find matches (bias is only used when no dark exists, unless CCD mode is enabled)
        const auto flatMatch = matcher.findFlat(*lightKey);

        // Find dark match (needs temp)
        std::optional<double> tempC;
        for (const auto& g : model->lightGroups())
        {
            if (g.key == lightGroupKey)
            {
                tempC = ::averageTemperatureC(g.frames);
                break;
            }
        }
        const auto darkMatch = matcher.findDark(*lightKey, tempC);
        CalibrationMatch biasMatch;
        if (lightTakesBias(darkMatch, policy.subtractBiasFromDark))
        {
            biasMatch = matcher.findBias(*lightKey);
        }

        // Update highlights
        if (biasPanel && !biasMatch.matchedGroupKey.isEmpty())
        {
            if (auto k = GroupKey::fromString(biasMatch.matchedGroupKey))
                biasPanel->setHighlightedGroupKeys({*k});
            else
                biasPanel->setHighlightedGroupKeys({});
        }
        else if (biasPanel)
        {
            biasPanel->setHighlightedGroupKeys({});
        }

        if (darkPanel && !darkMatch.matchedGroupKey.isEmpty())
        {
            if (auto k = GroupKey::fromString(darkMatch.matchedGroupKey))
                darkPanel->setHighlightedGroupKeys({*k});
            else
                darkPanel->setHighlightedGroupKeys({});
        }
        else if (darkPanel)
        {
            darkPanel->setHighlightedGroupKeys({});
        }

        if (flatPanel && !flatMatch.matchedGroupKey.isEmpty())
        {
            if (auto k = GroupKey::fromString(flatMatch.matchedGroupKey))
                flatPanel->setHighlightedGroupKeys({*k});
            else
                flatPanel->setHighlightedGroupKeys({});
        }
        else if (flatPanel)
        {
            flatPanel->setHighlightedGroupKeys({});
        }
    }

    void onFlatSelectionChanged(const GroupKey& flatGroupKey)
    {
        clearHighlights();
        clearOtherTableSelections(flatTable);

        const auto flatKeyAsLight = ::lightKeyForFlatGroup(model, flatGroupKey);
        if (!flatKeyAsLight)
        {
            return;
        }

        CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                                   model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
        setupMatcher(matcher);

        // 1. Highlight Bias/Dark for this Flat. A flat takes its dark alone, in CCD mode too, and the bias only when no
        // dark is applied to it.

        // Find dark match for flat (needs temp)
        std::optional<double> tempC;
        for (const auto& g : model->flatGroups())
        {
            if (g.key == flatGroupKey)
            {
                tempC = ::averageTemperatureC(g.frames);
                break;
            }
        }
        const auto darkMatch = matcher.findDark(*flatKeyAsLight, tempC);
        CalibrationMatch biasMatch;
        if (flatTakesBias(darkMatch))
        {
            biasMatch = matcher.findBias(*flatKeyAsLight);
        }

        if (biasPanel && !biasMatch.matchedGroupKey.isEmpty())
        {
            if (auto k = GroupKey::fromString(biasMatch.matchedGroupKey))
                biasPanel->setHighlightedGroupKeys({*k});
            else
                biasPanel->setHighlightedGroupKeys({});
        }
        else if (biasPanel)
        {
            biasPanel->setHighlightedGroupKeys({});
        }

        if (darkPanel && !darkMatch.matchedGroupKey.isEmpty())
        {
            if (auto k = GroupKey::fromString(darkMatch.matchedGroupKey))
                darkPanel->setHighlightedGroupKeys({*k});
            else
                darkPanel->setHighlightedGroupKeys({});
        }
        else if (darkPanel)
        {
            darkPanel->setHighlightedGroupKeys({});
        }

        // 2. Highlight Lights that use this Flat
        QSet<GroupKey> affectedLights;
        const QString flatGroupKeyStr = flatGroupKey.toString();

        for (const auto& group : model->lightGroups())
        {
            if (group.frames.empty())
                continue;

            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto match = matcher.findFlat(lightKey);

            if (match.matchedGroupKey == flatGroupKeyStr)
            {
                affectedLights.insert(group.key);
            }
        }

        if (lightPanel)
        {
            lightPanel->setHighlightedGroupKeys(affectedLights);
        }
    }

    void onBiasSelectionChanged(const GroupKey& biasGroupKey)
    {
        clearHighlights();
        clearOtherTableSelections(biasTable);

        CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                                   model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
        setupMatcher(matcher);

        const CalibrationMatchingPolicy policy = model->calibrationMatchingPolicy();
        const QString biasKeyStr = biasGroupKey.toString();

        // Highlight Lights using this Bias
        QSet<GroupKey> affectedLights;
        for (const auto& group : model->lightGroups())
        {
            if (group.frames.empty())
                continue;
            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto tempC = ::averageTemperatureC(group.frames);
            const auto darkMatch = matcher.findDark(lightKey, tempC);
            if (!lightTakesBias(darkMatch, policy.subtractBiasFromDark))
            {
                continue;
            }
            if (matcher.findBias(lightKey).matchedGroupKey == biasKeyStr)
            {
                affectedLights.insert(group.key);
            }
        }
        if (lightPanel)
            lightPanel->setHighlightedGroupKeys(affectedLights);

        // Highlight Flats using this Bias
        QSet<GroupKey> affectedFlats;
        for (const auto& group : model->flatGroups())
        {
            if (group.frames.empty())
                continue;
            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto tempC = ::averageTemperatureC(group.frames);
            const auto darkMatch = matcher.findDark(lightKey, tempC);
            if (!flatTakesBias(darkMatch))
            {
                continue;
            }
            if (matcher.findBias(lightKey).matchedGroupKey == biasKeyStr)
            {
                affectedFlats.insert(group.key);
            }
        }
        if (flatPanel)
            flatPanel->setHighlightedGroupKeys(affectedFlats);

        // Highlight Darks? Usually Darks don't use Bias unless we have "Dark - Bias" logic.
        // For now, no cross-highlight to Dark panel from Bias.
        if (darkPanel)
            darkPanel->setHighlightedGroupKeys({});
    }

    void onDarkSelectionChanged(const GroupKey& darkGroupKey)
    {
        clearHighlights();
        clearOtherTableSelections(darkTable);

        CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                                   model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());
        setupMatcher(matcher);

        const QString darkKeyStr = darkGroupKey.toString();

        // Highlight Lights using this Dark
        QSet<GroupKey> affectedLights;
        for (const auto& group : model->lightGroups())
        {
            if (group.frames.empty())
                continue;

            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto tempC = ::averageTemperatureC(group.frames);

            if (matcher.findDark(lightKey, tempC).matchedGroupKey == darkKeyStr)
            {
                affectedLights.insert(group.key);
            }
        }
        if (lightPanel)
            lightPanel->setHighlightedGroupKeys(affectedLights);

        // Highlight Flats using this Dark
        QSet<GroupKey> affectedFlats;
        for (const auto& group : model->flatGroups())
        {
            if (group.frames.empty())
                continue;

            const auto lightKey = LightGroupKey::fromFrame(group.frames.front());
            const auto tempC = ::averageTemperatureC(group.frames);

            if (matcher.findDark(lightKey, tempC).matchedGroupKey == darkKeyStr)
            {
                affectedFlats.insert(group.key);
            }
        }
        if (flatPanel)
            flatPanel->setHighlightedGroupKeys(affectedFlats);

        if (biasPanel)
            biasPanel->setHighlightedGroupKeys({});
    }

    void clearHighlights()
    {
        if (lightPanel)
            lightPanel->setHighlightedGroupKeys({});
        if (flatPanel)
            flatPanel->setHighlightedGroupKeys({});
        if (biasPanel)
            biasPanel->setHighlightedGroupKeys({});
        if (darkPanel)
            darkPanel->setHighlightedGroupKeys({});
    }

    void scheduleRefresh()
    {
        debounceTimer->start();
    }

    void updatePanels()
    {
        if (coverageWidget)
        {
            coverageWidget->refresh();
        }

        if (frameSummaryWidget)
        {
            frameSummaryWidget->refresh();
        }

        if (issuesWidget)
        {
            issuesWidget->refresh();
        }

        if (policyWidget)
        {
            policyWidget->refresh();
        }

        if (quickActionsWidget)
        {
            quickActionsWidget->refresh();
        }

        if (biasPanel)
        {
            biasPanel->refresh();
        }
        if (darkPanel)
        {
            darkPanel->refresh();
        }
        if (flatPanel)
        {
            flatPanel->refresh();
        }
        if (lightPanel)
        {
            lightPanel->refresh();
        }
    }

    void applyThemeStyling()
    {
        if (applyingThemeStyling)
        {
            return;
        }
        applyingThemeStyling = true;

        // Child panels/tables own their own palette-aware rendering; refresh them to
        // pick up updated token colors without forcing recursive polish cycles.
        updatePanels();

        applyingThemeStyling = false;
    }

    void selectGroupInTable(FrameType type, const GroupKey& key)
    {
        if (key.toString().isEmpty())
        {
            return;
        }

        CalibrationGroupTable* table = nullptr;
        switch (type)
        {
        case FrameType::Bias:
            table = biasTable;
            break;
        case FrameType::Dark:
            table = darkTable;
            break;
        case FrameType::Flat:
            table = flatTable;
            break;
        case FrameType::Light:
            table = lightTable;
            break;
        }

        if (!table)
        {
            return;
        }

        table->setFocus(Qt::OtherFocusReason);
        for (int i = 0; i < table->rowCount(); ++i)
        {
            auto* item = table->item(i, 0);
            if (!item)
            {
                continue;
            }
            if (item->data(Qt::UserRole).toString() == key.toString())
            {
                table->selectRow(i);
                table->scrollToItem(item);
                break;
            }
        }
    }
};

CalibrationTab::CalibrationTab(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model, undoStack))
{
}

CalibrationTab::~CalibrationTab()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_impl && m_impl->debounceTimer)
    {
        m_impl->debounceTimer->stop();
    }
}

void CalibrationTab::refreshAll()
{
    if (!m_impl)
    {
        return;
    }
    m_impl->scheduleRefresh();
}

void CalibrationTab::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!event || !m_impl)
    {
        return;
    }

    const auto type = event->type();
    if (type == QEvent::ThemeChange)
    {
        m_impl->applyThemeStyling();
        m_impl->scheduleRefresh();
    }
}

void CalibrationTab::showGroupInTable(FrameType type, const GroupKey& key)
{
    if (!m_impl)
    {
        return;
    }

    // Update immediately so selection works without waiting for debounce.
    m_impl->updatePanels();

    // Selecting right away is usually OK, but ensure tables are fully updated.
    QTimer::singleShot(0, this, [this, type, key]() {
        if (m_impl)
        {
            m_impl->selectGroupInTable(type, key);
        }
    });
}
