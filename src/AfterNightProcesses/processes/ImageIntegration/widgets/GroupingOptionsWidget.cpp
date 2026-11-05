// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GroupingOptionsWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include "components/AnCardWidget.h"
#include <QLabel>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(GroupingOptionsWidget)

class GroupingOptionsWidget::Impl
{
public:
    GroupingOptionsWidget* q;

    QCheckBox* chkFilter = nullptr;
    QCheckBox* chkObject = nullptr;
    QCheckBox* chkSession = nullptr;
    QCheckBox* chkIsoGain = nullptr;
    QCheckBox* chkOffset = nullptr;
    QCheckBox* chkExposure = nullptr;
    QCheckBox* chkTemperature = nullptr;
    QCheckBox* chkCustomTag = nullptr;
    QLabel* lblExpTolerance = nullptr;
    QDoubleSpinBox* spinExpTolerance = nullptr;
    QLabel* lblTempTolerance = nullptr;
    QDoubleSpinBox* spinTempTolerance = nullptr;

    // Do not rely on QWidget::isHidden() to decide which options to clear:
    // widgets are hidden until their parent is shown, which would clear defaults at startup/tests.
    bool showFilter = true;
    bool showObject = true;
    bool showSession = true;
    bool showIsoGain = true;
    bool showOffset = true;
    bool showExposure = true;
    bool showTemperature = true;
    bool showCustomTag = true;

    bool blockSignals = false;

    explicit Impl(GroupingOptionsWidget* parent) : q(parent)
    {
        setupUi();
    }

    void setupUi()
    {
        auto* layout = new QGridLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* group = new an::ui::AnCardWidget(GroupingOptionsWidget::tr("Group By"), q);
        auto* groupLayout = new QGridLayout();
        group->setContentLayout(groupLayout);

        // Resolution grouping is mandatory in V2; do not expose it as a user option.
        chkFilter = createCheckBox(GroupingOptionsWidget::tr("Filter"), "chkFilter");
        chkObject = createCheckBox(GroupingOptionsWidget::tr("Object"), "chkObject");
        chkSession = createCheckBox(GroupingOptionsWidget::tr("Session"), "chkSession");
        chkIsoGain = createCheckBox(GroupingOptionsWidget::tr("ISO / Gain"), "chkIsoGain");
        chkOffset = createCheckBox(GroupingOptionsWidget::tr("Offset"), "chkOffset");
        chkExposure = createCheckBox(GroupingOptionsWidget::tr("Exposure"), "chkExposure");
        chkTemperature = createCheckBox(GroupingOptionsWidget::tr("Temperature"), "chkTemperature");
        chkCustomTag = createCheckBox(GroupingOptionsWidget::tr("Custom Tag"), "chkCustomTag");

        lblExpTolerance = new QLabel(GroupingOptionsWidget::tr("Tol (s)"), q);
        lblExpTolerance->setObjectName("lblExpTolerance");
        spinExpTolerance = new QDoubleSpinBox(q);
        spinExpTolerance->setObjectName("spinExpTolerance");
        spinExpTolerance->setRange(0.0, 36000.0);
        spinExpTolerance->setSingleStep(0.1);
        spinExpTolerance->setDecimals(1);
        spinExpTolerance->setValue(3.0);
        spinExpTolerance->setToolTip(
            GroupingOptionsWidget::tr("Group exposures within this tolerance. 0 = exact match."));
        QObject::connect(spinExpTolerance, &QDoubleSpinBox::valueChanged, [this](double) {
            if (!blockSignals)
            {
                emit q->optionsChanged(collectOptions());
            }
        });
        QObject::connect(chkExposure, &QCheckBox::toggled, [this](bool checked) {
            if (spinExpTolerance)
            {
                spinExpTolerance->setEnabled(checked);
            }
        });

        lblTempTolerance = new QLabel(GroupingOptionsWidget::tr("Tol (C)"), q);
        lblTempTolerance->setObjectName("lblTempTolerance");
        spinTempTolerance = new QDoubleSpinBox(q);
        spinTempTolerance->setObjectName("spinTempTolerance");
        spinTempTolerance->setRange(0.0, 10.0);
        spinTempTolerance->setSingleStep(0.1);
        spinTempTolerance->setDecimals(1);
        spinTempTolerance->setValue(2.0);
        spinTempTolerance->setToolTip(
            GroupingOptionsWidget::tr("Group temperatures within this tolerance. 0 = exact match."));
        QObject::connect(spinTempTolerance, &QDoubleSpinBox::valueChanged, [this](double) {
            if (!blockSignals)
            {
                emit q->optionsChanged(collectOptions());
            }
        });
        QObject::connect(chkTemperature, &QCheckBox::toggled, [this](bool checked) {
            if (spinTempTolerance)
            {
                spinTempTolerance->setEnabled(checked);
            }
        });
        QObject::connect(chkIsoGain, &QCheckBox::toggled, [this](bool checked) {
            if (chkOffset)
            {
                chkOffset->setEnabled(checked);
            }
        });

        chkFilter->setToolTip(GroupingOptionsWidget::tr("Group frames by filter name (e.g., L, Ha, OIII)."));
        chkObject->setToolTip(GroupingOptionsWidget::tr("Group frames by object/target name from metadata."));
        chkSession->setToolTip(GroupingOptionsWidget::tr(
            "Group frames by session. Uses the user session if set, otherwise the capture date."));
        chkIsoGain->setToolTip(GroupingOptionsWidget::tr("Group frames by ISO/Gain from metadata."));
        chkOffset->setToolTip(
            GroupingOptionsWidget::tr("Also group by offset (only applies when ISO/Gain is enabled)."));
        chkExposure->setToolTip(GroupingOptionsWidget::tr("Group frames by exposure time from metadata."));
        lblExpTolerance->setToolTip(GroupingOptionsWidget::tr("Exposure grouping tolerance in seconds."));
        spinExpTolerance->setToolTip(
            GroupingOptionsWidget::tr("Group exposures within this tolerance. 0 = exact match."));
        chkTemperature->setToolTip(
            GroupingOptionsWidget::tr("Group frames by sensor temperature from metadata (when available)."));
        lblTempTolerance->setToolTip(GroupingOptionsWidget::tr("Temperature grouping tolerance in Celsius."));
        spinTempTolerance->setToolTip(
            GroupingOptionsWidget::tr("Group temperatures within this tolerance. 0 = exact match."));
        chkCustomTag->setToolTip(GroupingOptionsWidget::tr("Group frames by the custom tag field."));

        // Align all "Group By" checkboxes to the left (single column).
        auto* v = new QVBoxLayout();
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        v->addWidget(chkFilter);
        v->addWidget(chkObject);
        v->addWidget(chkSession);

        // ISO/Gain + Offset row (offset on the right)
        auto* isoRow = new QHBoxLayout();
        isoRow->setContentsMargins(0, 0, 0, 0);
        isoRow->setSpacing(6);
        isoRow->addWidget(chkIsoGain);
        isoRow->addWidget(chkOffset);
        isoRow->addStretch();
        auto* isoRowContainer = new QWidget(group);
        isoRowContainer->setLayout(isoRow);
        v->addWidget(isoRowContainer);

        // Exposure + tolerance row (tolerance on the right)
        auto* expRow = new QHBoxLayout();
        expRow->setContentsMargins(0, 0, 0, 0);
        expRow->setSpacing(6);
        expRow->addWidget(chkExposure);
        expRow->addWidget(lblExpTolerance);
        expRow->addWidget(spinExpTolerance);
        expRow->addStretch();
        auto* expRowContainer = new QWidget(group);
        expRowContainer->setLayout(expRow);
        v->addWidget(expRowContainer);

        v->addWidget(chkCustomTag);

        // Temperature + tolerance row (tolerance on the right)
        auto* tempRow = new QHBoxLayout();
        tempRow->setContentsMargins(0, 0, 0, 0);
        tempRow->setSpacing(6);
        tempRow->addWidget(chkTemperature);
        tempRow->addWidget(lblTempTolerance);
        tempRow->addWidget(spinTempTolerance);
        tempRow->addStretch();
        auto* tempRowContainer = new QWidget(group);
        tempRowContainer->setLayout(tempRow);
        v->addWidget(tempRowContainer);

        groupLayout->addLayout(v, 0, 0);

        layout->addWidget(group);

        // Initial defaults from struct
        updateUiFromOptions(GroupingOptions{});
    }

    QCheckBox* createCheckBox(const QString& text, const QString& objectName)
    {
        auto* cb = new QCheckBox(text, q);
        cb->setObjectName(objectName);
        QObject::connect(cb, &QCheckBox::toggled, [this](bool) {
            if (!blockSignals)
            {
                emit q->optionsChanged(collectOptions());
            }
        });
        return cb;
    }

    GroupingOptions collectOptions() const
    {
        GroupingOptions opts;
        opts.byResolution = true;
        opts.byFilter = chkFilter->isChecked();
        opts.byObject = chkObject->isChecked();
        opts.bySession = chkSession->isChecked();
        opts.byIsoGain = chkIsoGain->isChecked();
        opts.byOffset = chkOffset->isChecked();
        opts.byExposure = chkExposure->isChecked();
        opts.exposureToleranceSeconds = spinExpTolerance ? spinExpTolerance->value() : 0.0;
        opts.byTemperature = chkTemperature->isChecked();
        opts.temperatureToleranceC = spinTempTolerance ? spinTempTolerance->value() : 0.0;
        opts.byCustomTag = chkCustomTag->isChecked();
        return opts;
    }

    void updateUiFromOptions(const GroupingOptions& opts)
    {
        blockSignals = true;
        chkFilter->setChecked(opts.byFilter);
        chkObject->setChecked(opts.byObject);
        chkSession->setChecked(opts.bySession);
        chkIsoGain->setChecked(opts.byIsoGain);
        chkOffset->setChecked(opts.byOffset);
        chkExposure->setChecked(opts.byExposure);
        chkTemperature->setChecked(opts.byTemperature);
        if (spinExpTolerance)
        {
            spinExpTolerance->setValue(opts.exposureToleranceSeconds);
            spinExpTolerance->setEnabled(opts.byExposure);
        }
        if (spinTempTolerance)
        {
            spinTempTolerance->setValue(opts.temperatureToleranceC);
            spinTempTolerance->setEnabled(opts.byTemperature);
        }
        chkOffset->setEnabled(opts.byIsoGain);
        chkCustomTag->setChecked(opts.byCustomTag);
        clearHiddenOptions();
        blockSignals = false;
    }

    void clearHiddenOptions()
    {
        if (!showFilter)
            chkFilter->setChecked(false);
        if (!showObject)
            chkObject->setChecked(false);
        if (!showSession)
            chkSession->setChecked(false);
        if (!showIsoGain)
            chkIsoGain->setChecked(false);
        if (!showOffset)
            chkOffset->setChecked(false);
        if (!showExposure)
            chkExposure->setChecked(false);
        if (!showTemperature)
            chkTemperature->setChecked(false);
        if (!showCustomTag)
            chkCustomTag->setChecked(false);
    }

    void updateVisibility(FrameType type)
    {
        // Defaults: Show all
        showFilter = true;
        showObject = true;
        showSession = true;
        showIsoGain = true;
        showOffset = true;
        showExposure = true;
        showTemperature = true;
        showCustomTag = true;
        if (lblExpTolerance)
            lblExpTolerance->setVisible(true);
        if (spinExpTolerance)
            spinExpTolerance->setVisible(true);
        if (lblTempTolerance)
            lblTempTolerance->setVisible(true);
        if (spinTempTolerance)
            spinTempTolerance->setVisible(true);

        switch (type)
        {
        case FrameType::Bias:
            // Bias: IsoGain, CustomTag.
            // Hide: Filter, Object, Session, Exposure, Temperature.
            showFilter = false;
            showObject = false;
            showSession = false;
            showExposure = false;
            showTemperature = false;
            if (lblExpTolerance)
                lblExpTolerance->setVisible(false);
            if (spinExpTolerance)
                spinExpTolerance->setVisible(false);
            if (lblTempTolerance)
                lblTempTolerance->setVisible(false);
            if (spinTempTolerance)
                spinTempTolerance->setVisible(false);
            break;
        case FrameType::Dark:
            // Dark: IsoGain, Exposure, Temperature, Offset, CustomTag.
            // Hide: Filter, Object, Session.
            showFilter = false;
            showObject = false;
            showSession = false;
            break;
        case FrameType::Flat:
            // Flat: IsoGain, Offset, Filter, Object, Session, CustomTag.
            // Hide: Exposure, Temperature. (Based on FlatGroupKey analysis)
            showExposure = false;
            showTemperature = false;
            if (lblExpTolerance)
                lblExpTolerance->setVisible(false);
            if (spinExpTolerance)
                spinExpTolerance->setVisible(false);
            if (lblTempTolerance)
                lblTempTolerance->setVisible(false);
            if (spinTempTolerance)
                spinTempTolerance->setVisible(false);
            break;
        case FrameType::Light:
            // All except temperature (not in light keys)
            showTemperature = false;
            if (lblTempTolerance)
                lblTempTolerance->setVisible(false);
            if (spinTempTolerance)
                spinTempTolerance->setVisible(false);
            break;
        }

        chkFilter->setVisible(showFilter);
        chkObject->setVisible(showObject);
        chkSession->setVisible(showSession);
        chkIsoGain->setVisible(showIsoGain);
        chkOffset->setVisible(showOffset);
        chkExposure->setVisible(showExposure);
        chkTemperature->setVisible(showTemperature);
        chkCustomTag->setVisible(showCustomTag);

        blockSignals = true;
        clearHiddenOptions();
        blockSignals = false;
    }
};

GroupingOptionsWidget::GroupingOptionsWidget(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>(this)) {}

GroupingOptionsWidget::~GroupingOptionsWidget() = default;

void GroupingOptionsWidget::setFrameType(FrameType type)
{
    m_impl->updateVisibility(type);
}

void GroupingOptionsWidget::setOptions(const GroupingOptions& options)
{
    m_impl->updateUiFromOptions(options);
}

GroupingOptions GroupingOptionsWidget::currentOptions() const
{
    return m_impl->collectOptions();
}
