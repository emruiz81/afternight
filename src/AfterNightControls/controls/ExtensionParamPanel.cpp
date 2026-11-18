// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/ExtensionParamPanel.h"

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/ExtensionUiSchemaUtils.h"
#include "controls/ProcessCompanions.h"
#include "controls/QSliderSpinBox.h"
#include "controls/RTPreviewHistogramToolbar.h"
#include "theme/AnDesignTokens.h"
#include "widgets/CurvesWidget.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ToneCurveModel.h"
#include "widgets/VectorSlopeWidget.h"

#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>
#include <cmath>
#include <utility>

#include <verdigris/wobjectimpl.h>

namespace an::ui::extensions
{

W_OBJECT_IMPL(ExtensionParamPanel)

namespace
{

using an::ui::extensions::schema_utils::SchemaCondition;

const QString kHistogramResolution6Bit = QStringLiteral("6-bit (64)");
const QString kHistogramResolution7Bit = QStringLiteral("7-bit (128)");
const QString kHistogramResolution8Bit = QStringLiteral("8-bit (256)");
const QString kHistogramResolution9Bit = QStringLiteral("9-bit (512)");
const QString kHistogramResolution10Bit = QStringLiteral("10-bit (1024)");
const QString kHistogramResolution12Bit = QStringLiteral("12-bit (4096)");
const QString kHistogramResolution14Bit = QStringLiteral("14-bit (16K)");
const QString kHistogramResolution16Bit = QStringLiteral("16-bit (64K)");
const QString kHistogramResolution100Levels = QStringLiteral("100 levels");
const QString kHistogramResolution1000Levels = QStringLiteral("1000 levels");
const QString kHistogramResolution10000Levels = QStringLiteral("10000 levels");

void populateHistogramResolutionCombo(QComboBox* combo)
{
    if (!combo)
    {
        return;
    }

    const QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItems({kHistogramResolution6Bit, kHistogramResolution7Bit, kHistogramResolution8Bit,
                     kHistogramResolution9Bit, kHistogramResolution10Bit, kHistogramResolution12Bit,
                     kHistogramResolution14Bit, kHistogramResolution16Bit, kHistogramResolution100Levels,
                     kHistogramResolution1000Levels, kHistogramResolution10000Levels});
    combo->setCurrentText(kHistogramResolution8Bit);
}

void applyParamReadoutLabelTheme(QLabel* label)
{
    if (!label)
    {
        return;
    }

    label->setFont(an::ui::tokens().typography.caption);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, an::ui::tokens().colors.textSecondary);
    label->setPalette(palette);
}

an::ui::AnInfoBanner::Tone bannerTone(const PanelNode& node, const bool custom_variant)
{
    const QString tone = node.field(u"tone").toString().trimmed().toLower();
    if (tone == QLatin1String("success"))
    {
        return an::ui::AnInfoBanner::Tone::Success;
    }
    if (tone == QLatin1String("warning"))
    {
        return an::ui::AnInfoBanner::Tone::Warning;
    }
    if (tone == QLatin1String("error"))
    {
        return an::ui::AnInfoBanner::Tone::Error;
    }

    return custom_variant ? an::ui::AnInfoBanner::Tone::Warning : an::ui::AnInfoBanner::Tone::Info;
}

int decimalsForStep(const double step)
{
    if (step <= 0.0)
    {
        return 4;
    }

    int decimals = 0;
    double scaled = step;
    while (decimals < 6 && std::abs(scaled - std::round(scaled)) > 1.0e-9)
    {
        scaled *= 10.0;
        ++decimals;
    }
    return (std::max)(2, decimals);
}

// A node's field as the process window has always read its entry: a number, a flag or a text where it states one of
// that type, the fallback otherwise.
double numberField(const PanelNode& node, const QString& name, const double fallback)
{
    const QCborValue value = node.field(name);
    if (value.isDouble())
    {
        return value.toDouble();
    }
    if (value.isInteger())
    {
        return static_cast<double>(value.toInteger());
    }
    return fallback;
}

bool flagField(const PanelNode& node, const QString& name, const bool fallback)
{
    const QCborValue value = node.field(name);
    return value.isBool() ? value.toBool() : fallback;
}

QString textField(const PanelNode& node, const QString& name, const QString& fallback = QString())
{
    const QCborValue value = node.field(name);
    return value.isString() ? value.toString() : fallback;
}

QCborArray listField(const PanelNode& node, const QString& name)
{
    const QCborValue value = node.field(name);
    return value.isArray() ? value.toArray() : QCborArray();
}

bool sliderTrackingEnabled(const PanelNode& node)
{
    if (node.has(u"tracking"))
    {
        return flagField(node, QStringLiteral("tracking"), true);
    }
    if (node.has(u"slider_tracking"))
    {
        return flagField(node, QStringLiteral("slider_tracking"), true);
    }
    return true;
}

QString tooltipText(const PanelNode& node)
{
    return textField(node, QStringLiteral("tooltip")).trimmed();
}

void applyTooltip(QWidget* widget, const QString& tooltip)
{
    if (widget && !tooltip.isEmpty())
    {
        widget->setToolTip(tooltip);
    }
}

QString buttonRole(const PanelNode& node)
{
    for (const char* name : {"button_role", "role", "variant"})
    {
        const QString role = textField(node, QString::fromLatin1(name)).trimmed().toLower();
        if (!role.isEmpty())
        {
            return role;
        }
    }
    return QStringLiteral("secondary");
}

QString buttonRole(const QCborMap& action)
{
    for (const char* name : {"button_role", "role", "variant"})
    {
        const QString role = action.value(QString::fromLatin1(name)).toString().trimmed().toLower();
        if (!role.isEmpty())
        {
            return role;
        }
    }
    return QStringLiteral("secondary");
}

// The schema's interpolation names: "cubic" is the natural cubic spline, and anything else Akima's.
an::CurveInterpolation curveInterpolationFromString(QString value)
{
    value = value.trimmed().toLower();
    if (value == QLatin1String("linear"))
    {
        return an::CurveInterpolation::Linear;
    }
    if (value == QLatin1String("cubic"))
    {
        return an::CurveInterpolation::NaturalCubic;
    }
    if (value == QLatin1String("steffen"))
    {
        return an::CurveInterpolation::Steffen;
    }
    return an::CurveInterpolation::Akima;
}

QCborArray defaultCurvePointArray()
{
    return QCborArray{QCborArray{0.0, 0.0}, QCborArray{0.5, 0.5}, QCborArray{1.0, 1.0}};
}

std::optional<double> cborNumber(const QCborValue& value)
{
    if (value.isDouble())
    {
        const double number = value.toDouble();
        return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
    }
    if (value.isInteger())
    {
        return static_cast<double>(value.toInteger());
    }
    if (value.isString())
    {
        bool ok = false;
        const double number = value.toString().trimmed().toDouble(&ok);
        if (ok && std::isfinite(number))
        {
            return number;
        }
    }
    return std::nullopt;
}

QColor colorFromCborValue(const QCborValue& value, const QColor& fallback)
{
    if (value.isString())
    {
        const QColor color(value.toString().trimmed());
        return color.isValid() ? color : fallback;
    }
    if (value.isArray())
    {
        const QCborArray array = value.toArray();
        if (array.size() >= 3)
        {
            const auto channel = [&array](const qsizetype index, const int fallbackChannel) {
                const QCborValue part = array.at(index);
                const double number = part.isDouble()    ? part.toDouble()
                                      : part.isInteger() ? static_cast<double>(part.toInteger())
                                                         : static_cast<double>(fallbackChannel);
                return std::clamp(panel_rows::clampToInt(number), 0, 255);
            };
            const int red = channel(0, fallback.red());
            const int green = channel(1, fallback.green());
            const int blue = channel(2, fallback.blue());
            const int alpha = array.size() >= 4 ? channel(3, fallback.alpha()) : 255;
            return QColor(red, green, blue, alpha);
        }
    }
    return fallback;
}

std::vector<an::ui::VectorSlopeWidget::VectorDefinition> vectorSlopeDefinitions(const PanelNode& node)
{
    const QCborArray vectors_value = listField(node, QStringLiteral("vectors"));
    if (vectors_value.isEmpty())
    {
        return an::ui::VectorSlopeWidget::defaultHueWheelVectors();
    }

    std::vector<an::ui::VectorSlopeWidget::VectorDefinition> definitions;
    definitions.reserve(static_cast<size_t>(vectors_value.size()));
    const auto defaults = an::ui::VectorSlopeWidget::defaultHueWheelVectors();
    for (qsizetype i = 0; i < vectors_value.size(); ++i)
    {
        const QCborMap vector_obj = vectors_value.at(i).toMap();
        const auto& fallback = defaults.at(static_cast<size_t>(i) % defaults.size());
        const auto text = [&vector_obj](const char* key, const QString& fallbackText) {
            const QCborValue value = vector_obj.value(QString::fromLatin1(key));
            return value.isString() ? value.toString() : fallbackText;
        };
        const auto number = [&vector_obj](const char* key, const double fallbackNumber) {
            const QCborValue value = vector_obj.value(QString::fromLatin1(key));
            return value.isDouble()    ? value.toDouble()
                   : value.isInteger() ? static_cast<double>(value.toInteger())
                                       : fallbackNumber;
        };
        an::ui::VectorSlopeWidget::VectorDefinition definition;
        definition.key = text("key", fallback.key).trimmed();
        definition.hue_param = text("hue_param", fallback.hue_param).trimmed();
        definition.saturation_param = text("saturation_param", fallback.saturation_param).trimmed();
        definition.property_name = text("property_name", definition.key).trimmed().toLatin1();
        definition.base_hue_degrees = number("base_hue_degrees", number("base_hue", fallback.base_hue_degrees));
        definition.saturation_scale = number("saturation_scale", fallback.saturation_scale);
        definition.color = colorFromCborValue(vector_obj.value(QStringLiteral("color")), fallback.color);
        if (!definition.key.isEmpty() && !definition.hue_param.isEmpty() && !definition.saturation_param.isEmpty())
        {
            definitions.push_back(std::move(definition));
        }
    }

    return definitions.empty() ? an::ui::VectorSlopeWidget::defaultHueWheelVectors() : definitions;
}

bool appendCurvePoint(const QCborValue& value, std::vector<std::pair<double, double>>* points)
{
    if (!points)
    {
        return false;
    }

    std::optional<double> x;
    std::optional<double> y;
    if (value.isArray())
    {
        const QCborArray point = value.toArray();
        if (point.size() >= 2)
        {
            x = cborNumber(point.at(0));
            y = cborNumber(point.at(1));
        }
    }
    else if (value.isMap())
    {
        const QCborMap point = value.toMap();
        x = cborNumber(point.value(QStringLiteral("x")));
        y = cborNumber(point.value(QStringLiteral("y")));
        if (!x.has_value())
        {
            x = cborNumber(point.value(QStringLiteral("input")));
        }
        if (!y.has_value())
        {
            y = cborNumber(point.value(QStringLiteral("output")));
        }
    }

    if (!x.has_value() || !y.has_value())
    {
        return false;
    }

    points->push_back({std::clamp(*x, 0.0, 1.0), std::clamp(*y, 0.0, 1.0)});
    return true;
}

std::vector<std::pair<double, double>> normalizedCurvePoints(const QCborValue& value)
{
    std::vector<std::pair<double, double>> points;
    const QCborArray source = value.isArray() ? value.toArray() : defaultCurvePointArray();
    points.reserve(static_cast<std::size_t>(source.size()) + 2);
    for (const QCborValue& point : source)
    {
        appendCurvePoint(point, &points);
    }

    if (points.size() < 2)
    {
        points = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
    }

    std::sort(points.begin(), points.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });

    std::vector<std::pair<double, double>> cleaned;
    cleaned.reserve(points.size() + 2);
    for (const auto& point : points)
    {
        if (!cleaned.empty() && point.first <= cleaned.back().first + 1.0e-6)
        {
            cleaned.back().second = point.second;
            continue;
        }
        cleaned.push_back(point);
    }

    if (cleaned.empty())
    {
        cleaned = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
    }
    if (cleaned.front().first > 1.0e-6)
    {
        cleaned.insert(cleaned.begin(), {0.0, 0.0});
    }
    else
    {
        cleaned.front().first = 0.0;
    }
    if (cleaned.back().first < 1.0 - 1.0e-6)
    {
        cleaned.push_back({1.0, 1.0});
    }
    else
    {
        cleaned.back().first = 1.0;
    }

    if (cleaned.size() < 2)
    {
        return {{0.0, 0.0}, {1.0, 1.0}};
    }
    return cleaned;
}

an::ui::ToneCurveModel curveFromCurvePoints(const QCborValue& value, const an::CurveInterpolation interpolation)
{
    const std::vector<std::pair<double, double>> points = normalizedCurvePoints(value);
    std::vector<an::CurveControlPoint> nodes;
    nodes.reserve(points.size());
    for (const auto& [x, y] : points)
    {
        nodes.push_back(an::CurveControlPoint{x, y});
    }
    return an::ui::ToneCurveModel(std::move(nodes), interpolation);
}

QCborArray curvePointsFromEditor(CurvesWidget* editor)
{
    if (!editor)
    {
        return defaultCurvePointArray();
    }

    const an::ui::ToneCurveModel curve = editor->getCurve();
    if (curve.isEmpty())
    {
        return defaultCurvePointArray();
    }

    QCborArray points;
    for (const an::CurveControlPoint& point : curve.points())
    {
        points.append(QCborArray{point.input, point.output});
    }
    return points;
}

/// Whether a value is one a control holds: a boolean, a finite number or a text.
bool isPanelValue(const QCborValue& value)
{
    if (value.isBool() || value.isInteger() || value.isString())
    {
        return true;
    }
    return value.isDouble() && std::isfinite(value.toDouble());
}

/// The kinds whose value the panel reads, and how.
bool holdsDoubleValue(const QString& kind)
{
    return kind == QLatin1String("float");
}

bool holdsComboValue(const QString& kind)
{
    return kind == QLatin1String("choice") || kind == QLatin1String("view_selector");
}

} // namespace

// ---- The rows a process draws its own controls in.

namespace panel_rows
{

void applyRowLabelTheme(QLabel* label)
{
    if (!label)
    {
        return;
    }

    label->setFont(an::ui::tokens().typography.body);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, an::ui::tokens().colors.textPrimary);
    label->setPalette(palette);
}

void setUpRowLabel(QLabel* label)
{
    if (!label)
    {
        return;
    }

    applyRowLabelTheme(label);
    label->setMinimumWidth(kLabelColumnWidth);
    label->setMaximumWidth(kLabelColumnWidth);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    label->setWordWrap(false);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

void setUpRowGrid(QGridLayout* layout)
{
    if (!layout)
    {
        return;
    }

    const auto& spacing = an::ui::tokens().spacing;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(spacing.md);
    layout->setVerticalSpacing(spacing.xs);
    layout->setColumnMinimumWidth(0, kLabelColumnWidth);
    layout->setColumnStretch(0, 0);
    layout->setColumnStretch(1, 1);
}

bool valuesEqual(const QVariant& left, const QVariant& right)
{
    if (left.userType() == QMetaType::Bool || right.userType() == QMetaType::Bool)
    {
        return left.toBool() == right.toBool();
    }

    bool left_ok = false;
    bool right_ok = false;
    const double left_number = left.toDouble(&left_ok);
    const double right_number = right.toDouble(&right_ok);
    if (left_ok && right_ok)
    {
        return std::abs(left_number - right_number) < 1.0e-9;
    }

    return left.toString() == right.toString();
}

int clampToInt(const double value)
{
    return static_cast<int>(std::clamp(value, static_cast<double>(INT_MIN), static_cast<double>(INT_MAX)));
}

} // namespace panel_rows

an::ui::PreviewHistogramResolution previewHistogramResolutionNamed(const QString& name)
{
    using an::ui::PreviewHistogramResolution;
    if (name == kHistogramResolution6Bit)
    {
        return PreviewHistogramResolution::Bits6;
    }
    if (name == kHistogramResolution7Bit)
    {
        return PreviewHistogramResolution::Bits7;
    }
    if (name == kHistogramResolution9Bit)
    {
        return PreviewHistogramResolution::Bits9;
    }
    if (name == kHistogramResolution10Bit)
    {
        return PreviewHistogramResolution::Bits10;
    }
    if (name == kHistogramResolution12Bit)
    {
        return PreviewHistogramResolution::Bits12;
    }
    if (name == kHistogramResolution14Bit)
    {
        return PreviewHistogramResolution::Bits14;
    }
    if (name == kHistogramResolution16Bit)
    {
        return PreviewHistogramResolution::Bits16;
    }
    if (name == kHistogramResolution100Levels)
    {
        return PreviewHistogramResolution::Levels100;
    }
    if (name == kHistogramResolution1000Levels)
    {
        return PreviewHistogramResolution::Levels1000;
    }
    if (name == kHistogramResolution10000Levels)
    {
        return PreviewHistogramResolution::Levels10000;
    }
    return PreviewHistogramResolution::Bits8;
}

an::ui::HistogramResolution histogramResolutionNamed(const QString& name)
{
    return an::ui::histogramResolutionOf(previewHistogramResolutionNamed(name));
}

// ---- What a panel holds once built.

struct ExtensionParamPanel::Built
{
    struct Entry
    {
        QString id;
        QString kind;
        QPointer<QWidget> widget;
        QVariant defaultValue;
    };

    struct Conditional
    {
        QPointer<QWidget> widget;
        QString paramId;
        QVariant equals;
        bool hasEquals = false;
        QVariant notEquals;
        bool hasNotEquals = false;
        std::optional<SchemaCondition> visibleCondition;
        std::optional<SchemaCondition> enabledCondition;
    };

    struct MixLabel
    {
        QPointer<QLabel> label;
        QString sourceParamId;
        QString leftLabel;
        QString rightLabel;
    };

    struct ValueRange
    {
        double min = 0.0;
        double max = 0.0;
        QString text;
    };

    struct ValueLabel
    {
        QPointer<QLabel> label;
        QString sourceParamId;
        int decimals = 2;
        QString prefix;
        QString suffix;
        QString separator = QStringLiteral(" - ");
        std::vector<ValueRange> ranges;
    };

    /// What a patch reaches by an id: the control's outer widget (its row, its panel, its section), the label of a
    /// row, and the widget whose text a `text` patch sets.
    struct Patchable
    {
        QPointer<QWidget> outer;
        QPointer<QLabel> label;
    };

    std::vector<Entry> entries;
    std::vector<Conditional> conditionals;
    std::vector<MixLabel> mixLabels;
    std::vector<ValueLabel> valueLabels;
    std::vector<std::pair<QString, QPointer<an::ui::VectorSlopeWidget>>> slopes;
    std::vector<ExtensionPanelHistogram> histograms;
    std::map<QString, QPointer<QPushButton>> actions;
    std::map<QString, QPointer<an::ui::AnInfoBanner>> banners;
    std::map<QString, Patchable> patchables;
    std::map<const PanelNode*, QPointer<QWidget>> byNode;
    std::vector<std::unique_ptr<CurveListener>> curveListeners;
    std::vector<QPointer<QWidget>> widgets;
    bool visibleContent = false;

    [[nodiscard]] const Entry* entry(const QString& id) const
    {
        for (const Entry& candidate : entries)
        {
            if (candidate.id == id)
            {
                return &candidate;
            }
        }
        return nullptr;
    }
};

/// Hears one curve's changes for the panel, which keeps one a curve so that a change names its curve.
class ExtensionParamPanel::CurveListener final : public CurvesWidgetListener
{
public:
    CurveListener(ExtensionParamPanel* panel, QString id) : m_panel(panel), m_id(std::move(id)) {}

    void onCurvesUpdated(const an::ui::ToneCurveModel&) override
    {
        if (m_panel)
        {
            m_panel->onCurveChanged(m_id);
        }
    }

private:
    QPointer<ExtensionParamPanel> m_panel;
    QString m_id;
};

ExtensionParamPanel::ExtensionParamPanel(QWidget* parent) : QWidget(parent), m_built(std::make_unique<Built>())
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(an::ui::tokens().spacing.sm);
}

ExtensionParamPanel::~ExtensionParamPanel()
{
    // The curves keep a listener of the panel's; they go with the panel's children, after it.
    for (CurvesWidget* editor : curveEditors())
    {
        editor->setListener(nullptr);
    }
}

void ExtensionParamPanel::setEventDelivery(const EventDelivery delivery)
{
    m_delivery = delivery;
}

void ExtensionParamPanel::setHost(ExtensionPanelHost host)
{
    m_host = std::move(host);
}

void ExtensionParamPanel::addLeadingWidget(QWidget* widget)
{
    if (!widget)
    {
        return;
    }
    m_leading.push_back(widget);
    m_layout->addWidget(widget);
}

const std::optional<ExtensionPanelDescription>& ExtensionParamPanel::description() const noexcept
{
    return m_description;
}

bool ExtensionParamPanel::hasVisibleContent() const noexcept
{
    const bool leading = std::any_of(m_leading.begin(), m_leading.end(),
                                     [](const QPointer<QWidget>& widget) { return !widget.isNull(); });
    return leading || m_built->visibleContent;
}

void ExtensionParamPanel::clear()
{
    for (CurvesWidget* editor : curveEditors())
    {
        editor->setListener(nullptr);
    }
    m_waiting.clear();
    m_heldCurve.clear();
    m_changedWhileHeld = false;
    m_description.reset();
    m_built = std::make_unique<Built>();
    m_leading.clear();
    while (QLayoutItem* item = m_layout->takeAt(0))
    {
        if (QLayout* childLayout = item->layout())
        {
            while (QLayoutItem* nested = childLayout->takeAt(0))
            {
                delete nested->widget();
                delete nested;
            }
        }
        delete item->widget();
        delete item;
    }
}

bool ExtensionParamPanel::setDescription(const ExtensionPanelDescription& description, std::string* error)
{
    if (!validatePanelDescription(description, error))
    {
        return false;
    }
    for (const PanelNode& node : description.nodes)
    {
        const PanelKindSpec* kind = panelKind(node.kind);
        if (kind != nullptr && kind->vocabulary == PanelVocabulary::Widgets)
        {
            if (error)
            {
                *error = "a " + node.kind.toStdString() +
                         " is a widget of a settings tree, which this panel does not "
                         "draw";
            }
            return false;
        }
    }

    // The earlier description's controls go; the leading widgets stay, at the top.
    std::vector<QPointer<QWidget>> leading = std::move(m_leading);
    for (const QPointer<QWidget>& widget : leading)
    {
        if (widget)
        {
            m_layout->removeWidget(widget);
            widget->setParent(nullptr);
        }
    }
    clear();
    for (const QPointer<QWidget>& widget : leading)
    {
        if (widget)
        {
            widget->setParent(this);
            addLeadingWidget(widget);
        }
    }

    ++m_quiet;
    m_description = description;
    build(*m_description);
    --m_quiet;
    updateReadouts();
    updateConditions();
    return true;
}

void ExtensionParamPanel::build(const ExtensionPanelDescription& description)
{
    Built& built = *m_built;
    const bool custom = description.variant == PanelVariant::Custom;
    const auto& spacing = an::ui::tokens().spacing;
    QVBoxLayout* panel_layout = m_layout;
    QWidget* param_panel = this;
    bool custom_header_emitted = false;
    std::vector<QVBoxLayout*> tab_page_layouts;

    const auto registerConditionalWidget = [&built](QWidget* widget, const PanelNode& source) {
        if (!widget)
        {
            return;
        }
        Built::Conditional conditional;
        conditional.widget = widget;

        const QString visible_expression = textField(source, QStringLiteral("visible_when")).trimmed();
        if (!visible_expression.isEmpty())
        {
            conditional.visibleCondition = an::ui::extensions::schema_utils::parseSchemaCondition(visible_expression);
        }

        const QCborValue condition_value = source.field(u"enabled_when");
        if (condition_value.isString())
        {
            const QString enabled_expression = condition_value.toString().trimmed();
            if (!enabled_expression.isEmpty())
            {
                conditional.enabledCondition =
                    an::ui::extensions::schema_utils::parseSchemaCondition(enabled_expression);
            }
        }
        else if (condition_value.isMap())
        {
            const QCborMap condition = condition_value.toMap();
            const QString param_id = condition.value(QStringLiteral("param")).toString().trimmed();
            if (!param_id.isEmpty())
            {
                conditional.paramId = param_id;
                if (condition.contains(QStringLiteral("equals")))
                {
                    conditional.equals = condition.value(QStringLiteral("equals")).toVariant();
                    conditional.hasEquals = true;
                }
                if (condition.contains(QStringLiteral("not_equals")))
                {
                    conditional.notEquals = condition.value(QStringLiteral("not_equals")).toVariant();
                    conditional.hasNotEquals = true;
                }
            }
        }

        if (conditional.visibleCondition.has_value() || conditional.enabledCondition.has_value() ||
            conditional.hasEquals || conditional.hasNotEquals)
        {
            built.conditionals.push_back(std::move(conditional));
        }
    };

    // What a patch reaches by its id, and what each node built.
    const auto patchable = [&built](const PanelNode* node, const QString& id, QWidget* outer, QLabel* label = nullptr) {
        built.byNode[node] = outer;
        if (!id.isEmpty())
        {
            built.patchables[id] = Built::Patchable{outer, label};
        }
    };

    const auto createSectionLayout = [&](const PanelNode& section, QVBoxLayout* parent_layout) -> QVBoxLayout* {
        const QString label = textField(section, QStringLiteral("label"), section.id());
        const QString title = textField(section, QStringLiteral("text"), label);
        const QString presentation = textField(section, QStringLiteral("presentation")).trimmed().toLower();
        const QString section_id = section.id();
        QVBoxLayout* target_layout = parent_layout ? parent_layout : panel_layout;
        QWidget* section_parent = target_layout->parentWidget() ? target_layout->parentWidget() : param_panel;
        const bool use_collapsible = custom || presentation == QLatin1String("collapsible");
        if (use_collapsible)
        {
            auto* collapsible = new an::ui::AnCollapsibleSection(title, section_parent);
            collapsible->setObjectName(QStringLiteral("anNativeParamSectionContainer"));
            collapsible->setProperty("anSectionId", section_id);
            auto* content_layout = new QVBoxLayout();
            content_layout->setContentsMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm);
            content_layout->setSpacing(spacing.sm);
            collapsible->setContentLayout(content_layout);
            collapsible->setExpanded(true);
            registerConditionalWidget(collapsible, section);
            target_layout->addWidget(collapsible);
            patchable(&section, section_id, collapsible);
            return content_layout;
        }

        auto* card = new an::ui::AnCardWidget(title, section_parent);
        card->setObjectName(QStringLiteral("anNativeParamSectionContainer"));
        card->setProperty("anSectionId", section_id);
        card->setProperty("anCardCompact", true);
        card->setProperty("anCardBodyPadded", true);
        auto* content_layout = new QVBoxLayout();
        content_layout->setContentsMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm);
        content_layout->setSpacing(spacing.sm);
        card->setContentLayout(content_layout);
        content_layout->setContentsMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm);
        registerConditionalWidget(card, section);
        target_layout->addWidget(card);
        patchable(&section, section_id, card);
        return content_layout;
    };

    const auto emitCustomHeader = [&]() {
        if (!custom || custom_header_emitted)
        {
            return;
        }
        custom_header_emitted = true;

        // A line that sets the custom look's panel apart from the standard one.
        auto* separator = new QFrame(param_panel);
        separator->setObjectName(QStringLiteral("anCustomPanelSeparator"));
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setFixedHeight(2);
        {
            QPalette pal = separator->palette();
            pal.setColor(QPalette::WindowText, an::ui::tokens().colors.accent().primary);
            separator->setPalette(pal);
        }
        panel_layout->addWidget(separator);
        built.visibleContent = true;
    };

    // Each node is placed in the layout of its container: the top of the panel, a page of tabs or a section.
    using Placement = ExtensionPanelHost::Placement;
    std::function<void(const PanelNode&, const Placement&)> buildNode;
    const auto buildChildren = [&buildNode](const PanelNode& group, QVBoxLayout* layout) {
        const Placement place = [layout]() { return layout; };
        for (const PanelNode& child : group.children)
        {
            buildNode(child, place);
        }
    };

    const auto makeRow = [&](const PanelNode& node, QGridLayout** row_layout) {
        auto* row = new QWidget(param_panel);
        row->setProperty("anParamId", node.id());
        *row_layout = new QGridLayout(row);
        panel_rows::setUpRowGrid(*row_layout);
        return row;
    };

    const auto finishValueRow = [&](const PanelNode& node, QWidget* row, QLabel* title, QWidget* value_widget,
                                    const Placement& place, const QVariant& default_value) {
        const QString id = node.id();
        value_widget->setProperty("anParamId", id);
        place()->addWidget(row);
        registerConditionalWidget(row, node);
        built.entries.push_back({id, node.kind, value_widget, default_value});
        patchable(&node, id, row, title);
        built.visibleContent = true;
    };

    buildNode = [&](const PanelNode& node, const Placement& place) {
        const QString& type = node.kind;
        const QString id = node.id();
        const QString label = textField(node, QStringLiteral("label"), id);
        const QString text = textField(node, QStringLiteral("text"), label);

        if (type == QLatin1String("tabs"))
        {
            emitCustomHeader();
            auto* tabs = new QTabWidget(param_panel);
            tabs->setObjectName(QStringLiteral("anNativeParamTabs"));
            tabs->setProperty("anParamId", id);
            tabs->setDocumentMode(false);
            tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            // A set the list opened for a page with none before it has no conditions of its own.
            registerConditionalWidget(tabs, node);
            place()->addWidget(tabs);
            patchable(&node, id, tabs);
            built.visibleContent = true;
            for (const PanelNode& page : node.children)
            {
                auto* page_widget = new QWidget(tabs);
                page_widget->setObjectName(QStringLiteral("anNativeParamTabPage"));
                page_widget->setProperty("anParamId", page.id());
                auto* page_layout = new QVBoxLayout(page_widget);
                page_layout->setContentsMargins(0, spacing.sm, 0, 0);
                page_layout->setSpacing(spacing.sm);
                const QString page_label = textField(page, QStringLiteral("label"), page.id());
                tabs->addTab(page_widget, textField(page, QStringLiteral("text"), page_label));
                tab_page_layouts.push_back(page_layout);
                patchable(&page, page.id(), page_widget);
                buildChildren(page, page_layout);
            }
            return;
        }

        if (type == QLatin1String("section"))
        {
            if (node.opened)
            {
                // A section the list's reading opened for the control after it is made when that control is placed,
                // after the control's own widgets, as the window made it: the Tab key reaches the control first.
                QVBoxLayout* opened_layout = nullptr;
                const Placement opened_place = [&, place]() {
                    if (opened_layout == nullptr)
                    {
                        emitCustomHeader();
                        opened_layout = createSectionLayout(node, place());
                        built.visibleContent = true;
                    }
                    return opened_layout;
                };
                for (const PanelNode& child : node.children)
                {
                    buildNode(child, opened_place);
                }
                return;
            }
            emitCustomHeader();
            QVBoxLayout* section_layout = createSectionLayout(node, place());
            built.visibleContent = true;
            buildChildren(node, section_layout);
            return;
        }

        if (type == QLatin1String("percentage_mix_label") || type == QLatin1String("value_description_label"))
        {
            const bool mix = type == QLatin1String("percentage_mix_label");
            auto* readout_row = new QWidget(param_panel);
            readout_row->setProperty("anParamId", id);
            auto* readout_layout = new QGridLayout(readout_row);
            panel_rows::setUpRowGrid(readout_layout);

            auto* label_widget = new QLabel(readout_row);
            label_widget->setObjectName(mix ? QStringLiteral("anNativeParamPercentageMixLabel")
                                            : QStringLiteral("anNativeParamValueDescriptionLabel"));
            label_widget->setProperty("anParamId", id);
            label_widget->setText(text);
            applyParamReadoutLabelTheme(label_widget);
            const QString tooltip = tooltipText(node);
            applyTooltip(readout_row, tooltip);
            applyTooltip(label_widget, tooltip);
            readout_layout->addWidget(label_widget, 0, 1);
            place()->addWidget(readout_row);
            registerConditionalWidget(readout_row, node);
            patchable(&node, id, readout_row, label_widget);

            if (mix)
            {
                Built::MixLabel mix_label;
                mix_label.label = label_widget;
                mix_label.sourceParamId = textField(node, QStringLiteral("source")).trimmed();
                mix_label.leftLabel = textField(node, QStringLiteral("left_label"), QStringLiteral("Ha"));
                mix_label.rightLabel = textField(node, QStringLiteral("right_label"), QStringLiteral("OIII"));
                built.mixLabels.push_back(std::move(mix_label));
            }
            else
            {
                Built::ValueLabel value_label;
                value_label.label = label_widget;
                value_label.sourceParamId = textField(node, QStringLiteral("source")).trimmed();
                value_label.decimals =
                    std::clamp(panel_rows::clampToInt(numberField(node, QStringLiteral("decimals"), 2.0)), 0, 6);
                value_label.prefix = textField(node, QStringLiteral("prefix"));
                value_label.suffix = textField(node, QStringLiteral("suffix"));
                value_label.separator = textField(node, QStringLiteral("separator"), QStringLiteral(" - "));
                for (const QCborValue& range_value : listField(node, QStringLiteral("ranges")))
                {
                    const QCborArray range = range_value.toArray();
                    if (range.size() < 3)
                    {
                        continue;
                    }

                    Built::ValueRange range_item;
                    range_item.min =
                        range.at(0).isInteger() ? static_cast<double>(range.at(0).toInteger()) : range.at(0).toDouble();
                    range_item.max =
                        range.at(1).isInteger() ? static_cast<double>(range.at(1).toInteger()) : range.at(1).toDouble();
                    range_item.text = range.at(2).toString();
                    value_label.ranges.push_back(std::move(range_item));
                }
                built.valueLabels.push_back(std::move(value_label));
            }
            built.visibleContent = true;
            return;
        }

        if (type == QLatin1String("info"))
        {
            if (isPanelCredits(node))
            {
                // The credits are shown with the process's description, not as a banner.
                return;
            }

            auto* banner = new an::ui::AnInfoBanner(text, bannerTone(node, custom), param_panel);
            banner->setObjectName(QStringLiteral("anNativeParamInfoBanner"));
            place()->addWidget(banner);
            if (!id.isEmpty())
            {
                built.banners[id] = banner;
            }
            patchable(&node, id, banner);
            built.visibleContent = true;
            return;
        }

        if (type == QLatin1String("manual_mask_editor") || type == QLatin1String("target_selector"))
        {
            QWidget* hosted = m_host.buildHosted ? m_host.buildHosted(node, place) : nullptr;
            if (hosted)
            {
                registerConditionalWidget(hosted, node);
                patchable(&node, id, hosted);
                built.visibleContent = true;
            }
            return;
        }

        if (type == QLatin1String("curve_editor"))
        {
            auto* editor_panel = new QWidget(param_panel);
            editor_panel->setObjectName(QStringLiteral("anNativeParamCurveEditorPanel"));
            editor_panel->setProperty("anParamId", id);
            const QString tooltip = tooltipText(node);
            applyTooltip(editor_panel, tooltip);

            auto* editor_layout = new QVBoxLayout(editor_panel);
            editor_layout->setContentsMargins(0, 0, 0, 0);
            editor_layout->setSpacing(spacing.sm);

            auto* scroll_area = new QScrollArea(editor_panel);
            scroll_area->setObjectName(QStringLiteral("anNativeParamCurveEditorScrollArea"));
            scroll_area->setMinimumSize(QSize(350, 250));
            scroll_area->setWidgetResizable(true);
            applyTooltip(scroll_area, tooltip);

            auto* curve_editor = new CurvesWidget(scroll_area);
            curve_editor->setObjectName(QStringLiteral("anNativeParamCurveEditor"));
            curve_editor->setProperty("anParamId", id);
            curve_editor->setMinimumSize(QSize(348, 248));
            curve_editor->setClippingInfoEnabled(flagField(node, QStringLiteral("clipping_info"), true));
            curve_editor->setGridEnabled(flagField(node, QStringLiteral("grid"), true));
            curve_editor->setGuideLineEnabled(flagField(node, QStringLiteral("guide"), true));
            curve_editor->setHistogramEnabled(flagField(node, QStringLiteral("histogram"), true));
            curve_editor->setAreaModeEnabled(flagField(node, QStringLiteral("histogram_areas"), false));
            curve_editor->setCurve(curveFromCurvePoints(
                node.field(u"default"), curveInterpolationFromString(textField(node, QStringLiteral("interpolation"),
                                                                               QStringLiteral("akima")))));
            built.curveListeners.push_back(std::make_unique<CurveListener>(this, id));
            curve_editor->setListener(built.curveListeners.back().get());
            curve_editor->installEventFilter(this);
            applyTooltip(curve_editor, tooltip);
            scroll_area->setWidget(curve_editor);
            editor_layout->addWidget(scroll_area, 1);

            auto* toolbar = new QWidget(editor_panel);
            auto* toolbar_layout = new QHBoxLayout(toolbar);
            toolbar_layout->setContentsMargins(spacing.sm, 0, spacing.sm, 0);
            toolbar_layout->setSpacing(spacing.xs);

            auto* zoom_in = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(zoom_in, tr("Zoom in"), QStringLiteral("zoom_in"));
            toolbar_layout->addWidget(zoom_in);
            QObject::connect(zoom_in, &QToolButton::clicked, curve_editor,
                             [curve_editor]() { curve_editor->zoomIn(); });

            auto* zoom_out = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(zoom_out, tr("Zoom out"), QStringLiteral("zoom_out"));
            toolbar_layout->addWidget(zoom_out);
            QObject::connect(zoom_out, &QToolButton::clicked, curve_editor,
                             [curve_editor]() { curve_editor->zoomOut(); });

            auto* zoom_reset = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(zoom_reset, tr("Zoom reset"),
                                                                QStringLiteral("zoom_fit"));
            toolbar_layout->addWidget(zoom_reset);
            QObject::connect(zoom_reset, &QToolButton::clicked, curve_editor,
                             [curve_editor]() { curve_editor->resetZoom(); });

            auto* divider = new QFrame(toolbar);
            divider->setFrameShape(QFrame::VLine);
            divider->setFrameShadow(QFrame::Sunken);
            toolbar_layout->addWidget(divider);

            auto* grid = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(grid, tr("Show grid"), QStringLiteral("grid"));
            grid->setCheckable(true);
            grid->setChecked(curve_editor->getGridEnabled());
            toolbar_layout->addWidget(grid);
            QObject::connect(grid, &QToolButton::clicked, curve_editor,
                             [curve_editor](const bool checked) { curve_editor->setGridEnabled(checked); });

            auto* guide = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(guide, tr("Show guide"), QStringLiteral("stat_line"));
            guide->setCheckable(true);
            guide->setChecked(curve_editor->getGuideLineEnabled());
            toolbar_layout->addWidget(guide);
            QObject::connect(guide, &QToolButton::clicked, curve_editor,
                             [curve_editor](const bool checked) { curve_editor->setGuideLineEnabled(checked); });

            auto* areas = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(areas, tr("Draw histogram areas"),
                                                                QStringLiteral("area_chart"));
            areas->setCheckable(true);
            areas->setChecked(curve_editor->getAreaModeEnabled());
            toolbar_layout->addWidget(areas);
            QObject::connect(areas, &QToolButton::clicked, curve_editor,
                             [curve_editor](const bool checked) { curve_editor->setAreaModeEnabled(checked); });

            auto* histogram = new QToolButton(toolbar);
            an::ui::processes::rt_preview_ui::initToolbarButton(histogram, tr("Show histogram"),
                                                                QStringLiteral("histogram"));
            histogram->setCheckable(true);
            histogram->setChecked(curve_editor->getHistogramEnabled());
            toolbar_layout->addWidget(histogram);
            QObject::connect(histogram, &QToolButton::clicked, this, [this, curve_editor](const bool checked) {
                curve_editor->setHistogramEnabled(checked);
                if (checked && m_host.curveHistogramShown)
                {
                    m_host.curveHistogramShown(curve_editor);
                }
            });

            toolbar_layout->addStretch(1);
            editor_layout->addWidget(toolbar, 0);

            place()->addWidget(editor_panel);
            registerConditionalWidget(editor_panel, node);
            built.entries.push_back({id, type, curve_editor, node.field(u"default").toVariant()});
            patchable(&node, id, editor_panel);
            built.visibleContent = true;
            return;
        }

        if (type == QLatin1String("rt_histogram"))
        {
            auto* histogram_panel = new QWidget(param_panel);
            histogram_panel->setObjectName(QStringLiteral("anNativeParamRtHistogramPanel"));
            histogram_panel->setProperty("anParamId", id);
            const QString tooltip = tooltipText(node);
            applyTooltip(histogram_panel, tooltip);

            auto* histogram_layout = new QVBoxLayout(histogram_panel);
            histogram_layout->setContentsMargins(0, 0, 0, 0);
            histogram_layout->setSpacing(spacing.sm);

            auto* scroll_area = new QScrollArea(histogram_panel);
            scroll_area->setObjectName(QStringLiteral("anNativeParamRtHistogramScrollArea"));
            const int minimum_height =
                (std::max)(120, panel_rows::clampToInt(numberField(node, QStringLiteral("height"), 220.0)));
            scroll_area->setMinimumSize(QSize(350, minimum_height));
            scroll_area->setWidgetResizable(true);
            applyTooltip(scroll_area, tooltip);

            // The first real-time histogram is the preview's, made by the process with each frame; any other is
            // counted from what the preview shows.
            const bool frame_histogram =
                std::none_of(built.histograms.begin(), built.histograms.end(),
                             [](const ExtensionPanelHistogram& existing) { return existing.frame; });
            HistogramWidget* histogram = nullptr;
            bool made_by_host = false;
            if (frame_histogram && m_host.makeFrameHistogram)
            {
                histogram = m_host.makeFrameHistogram(scroll_area);
                made_by_host = histogram != nullptr;
            }
            if (!histogram)
            {
                histogram = new HistogramWidget(scroll_area);
            }
            histogram->setObjectName(QStringLiteral("anNativeParamRtHistogram"));
            histogram->setProperty("anParamId", id);
            histogram->setMinimumSize(QSize(348, (std::max)(118, minimum_height - 2)));
            histogram->setClippingInfoEnabled(flagField(node, QStringLiteral("clipping_info"), true));
            histogram->setGridEnabled(flagField(node, QStringLiteral("grid"), true));
            histogram->setLocalChannelNormalization(
                flagField(node, QStringLiteral("local_channel_normalization"), false));
            histogram->setAreaModeEnabled(flagField(node, QStringLiteral("histogram_areas"), false));
            histogram->setMouseTracking(true);
            histogram->setParentScrollArea(scroll_area);
            histogram->setInteractiveZoomEnabled(true);
            applyTooltip(histogram, tooltip);
            scroll_area->setWidget(histogram);
            histogram_layout->addWidget(scroll_area, 1);

            auto* toolbar = new QWidget(histogram_panel);
            auto* toolbar_layout = new QHBoxLayout(toolbar);
            toolbar_layout->setContentsMargins(0, 0, 0, 0);
            toolbar_layout->setSpacing(spacing.xs);
            const auto toolbar_elements = an::ui::processes::rt_preview_ui::buildHistogramToolbar(
                toolbar, toolbar_layout,
                {
                    .translationContext = "ExtensionNativeProcessWindow",
                    .includeShowClippingInfo = flagField(node, QStringLiteral("show_clipping_info_control"), true),
                    .includeShowHistogram = false,
                    .localChannelNormalizationChecked =
                        flagField(node, QStringLiteral("local_channel_normalization"), false),
                    .showHistogramAreasChecked = flagField(node, QStringLiteral("histogram_areas"), false),
                });
            histogram_layout->addWidget(toolbar, 0);

            populateHistogramResolutionCombo(toolbar_elements.plotResolution);
            const QString resolution_label = toolbar_elements.plotResolution
                                                 ? toolbar_elements.plotResolution->currentText()
                                                 : kHistogramResolution8Bit;
            if (made_by_host)
            {
                if (m_host.frameHistogramResolutionChosen)
                {
                    m_host.frameHistogramResolutionChosen(resolution_label);
                }
            }
            else
            {
                histogram->setHistogramResolution(histogramResolutionNamed(resolution_label));
            }

            const QPointer<HistogramWidget> histogram_ptr(histogram);
            const auto onHistogram = [histogram_ptr](auto apply) {
                return [histogram_ptr, apply](const bool checked) {
                    if (histogram_ptr)
                    {
                        apply(histogram_ptr.data(), checked);
                    }
                };
            };
            if (toolbar_elements.zoomIn)
            {
                QObject::connect(toolbar_elements.zoomIn, &QToolButton::clicked, histogram,
                                 onHistogram([](HistogramWidget* widget, bool) { widget->zoomIn(); }));
            }
            if (toolbar_elements.zoomOut)
            {
                QObject::connect(toolbar_elements.zoomOut, &QToolButton::clicked, histogram,
                                 onHistogram([](HistogramWidget* widget, bool) { widget->zoomOut(); }));
            }
            if (toolbar_elements.zoomReset)
            {
                QObject::connect(toolbar_elements.zoomReset, &QToolButton::clicked, histogram,
                                 onHistogram([](HistogramWidget* widget, bool) { widget->resetZoom(); }));
            }
            if (toolbar_elements.gridButton)
            {
                QObject::connect(
                    toolbar_elements.gridButton, &QToolButton::clicked, histogram,
                    onHistogram([](HistogramWidget* widget, const bool checked) { widget->setGridEnabled(checked); }));
            }
            if (toolbar_elements.localChannelNormalization)
            {
                QObject::connect(toolbar_elements.localChannelNormalization, &QToolButton::clicked, histogram,
                                 onHistogram([](HistogramWidget* widget, const bool checked) {
                                     widget->setLocalChannelNormalization(checked);
                                 }));
            }
            if (toolbar_elements.showClippingInfo)
            {
                QObject::connect(toolbar_elements.showClippingInfo, &QToolButton::clicked, histogram,
                                 onHistogram([](HistogramWidget* widget, const bool checked) {
                                     widget->setClippingInfoEnabled(checked);
                                 }));
            }
            if (toolbar_elements.showHistogramAreas)
            {
                QObject::connect(toolbar_elements.showHistogramAreas, &QToolButton::clicked, histogram,
                                 onHistogram([](HistogramWidget* widget, const bool checked) {
                                     widget->setAreaModeEnabled(checked);
                                 }));
            }
            if (toolbar_elements.plotResolution && made_by_host)
            {
                // The preview's histogram is made by the next frame at the new resolution.
                QObject::connect(toolbar_elements.plotResolution, &QComboBox::currentTextChanged, histogram,
                                 [this](const QString& chosen) {
                                     if (m_host.frameHistogramResolutionChosen)
                                     {
                                         m_host.frameHistogramResolutionChosen(chosen);
                                     }
                                 });
            }
            else if (toolbar_elements.plotResolution)
            {
                QObject::connect(toolbar_elements.plotResolution, &QComboBox::currentTextChanged, histogram,
                                 [this, histogram_ptr](const QString& chosen) {
                                     if (histogram_ptr)
                                     {
                                         histogram_ptr->setHistogramResolution(histogramResolutionNamed(chosen));
                                         if (m_host.histogramResolutionChosen)
                                         {
                                             m_host.histogramResolutionChosen(histogram_ptr.data());
                                         }
                                     }
                                 });
            }

            place()->addWidget(histogram_panel);
            registerConditionalWidget(histogram_panel, node);
            built.histograms.push_back({id, histogram, scroll_area, toolbar_elements.plotResolution, made_by_host});
            patchable(&node, id, histogram_panel);
            built.visibleContent = true;
            return;
        }

        if (type == QLatin1String("vector_slope"))
        {
            auto* graph_panel = new QWidget(param_panel);
            graph_panel->setObjectName(QStringLiteral("anNativeParamVectorSlopePanel"));
            graph_panel->setProperty("anParamId", id);
            const QString tooltip = tooltipText(node);
            applyTooltip(graph_panel, tooltip);

            auto* graph_layout = new QVBoxLayout(graph_panel);
            graph_layout->setContentsMargins(0, 0, 0, 0);
            graph_layout->setSpacing(spacing.sm);

            QLabel* title = nullptr;
            if (!label.isEmpty())
            {
                title = new QLabel(label, graph_panel);
                title->setObjectName(QStringLiteral("anNativeParamVectorSlopeLabel"));
                applyParamReadoutLabelTheme(title);
                applyTooltip(title, tooltip);
                graph_layout->addWidget(title);
            }

            auto* graph = new an::ui::VectorSlopeWidget(graph_panel);
            graph->setProperty("anParamId", id);
            graph->setGraphHeight(panel_rows::clampToInt(numberField(node, QStringLiteral("height"), 220.0)));
            graph->setVectorDefinitions(vectorSlopeDefinitions(node));
            applyTooltip(graph, tooltip);
            graph_layout->addWidget(graph);

            place()->addWidget(graph_panel);
            registerConditionalWidget(graph_panel, node);
            built.slopes.emplace_back(id, graph);
            patchable(&node, id, graph_panel, title);
            built.visibleContent = true;
            return;
        }

        if (type == QLatin1String("action"))
        {
            QGridLayout* row_layout = nullptr;
            QWidget* row = makeRow(node, &row_layout);
            auto* button = new QPushButton(label, row);
            button->setObjectName(QStringLiteral("anNativeParamActionButton"));
            button->setProperty("anParamActionId", id);
            button->setProperty("anProcessButtonRole", buttonRole(node));
            const QString tooltip = tooltipText(node);
            applyTooltip(row, tooltip);
            applyTooltip(button, tooltip);
            QObject::connect(button, &QPushButton::clicked, this,
                             [this, id]() { say(id, QStringLiteral("clicked"), QCborValue(true), false); });
            row_layout->addWidget(button, 0, 1);
            place()->addWidget(row);
            registerConditionalWidget(row, node);
            built.actions[id] = button;
            patchable(&node, id, row);
            built.visibleContent = true;
            return;
        }

        // A parameter: a row of its label and its control.
        QGridLayout* row_layout = nullptr;
        QWidget* row = makeRow(node, &row_layout);
        QLabel* title = nullptr;
        const QString tooltip = tooltipText(node);

        if (type == QLatin1String("float"))
        {
            title = new QLabel(label, row);
            title->setObjectName(QStringLiteral("anNativeParamLabel"));
            panel_rows::setUpRowLabel(title);
            applyTooltip(row, tooltip);
            applyTooltip(title, tooltip);
            row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

            auto* controls = new QWidget(row);
            auto* controls_layout = new QHBoxLayout(controls);
            controls_layout->setContentsMargins(0, 0, 0, 0);
            controls_layout->setSpacing(spacing.sm);

            auto* spin = new QDoubleSpinBox(controls);
            spin->setObjectName(QStringLiteral("anNativeParamDoubleSpinBox"));
            applyTooltip(spin, tooltip);
            spin->setMinimum(numberField(node, QStringLiteral("min"), -1.0e6));
            spin->setMaximum(numberField(node, QStringLiteral("max"), 1.0e6));
            spin->setSingleStep(numberField(node, QStringLiteral("step"), 0.01));
            spin->setDecimals(decimalsForStep(spin->singleStep()));

            auto* slider = new QSlider(Qt::Horizontal, controls);
            slider->setObjectName(QStringLiteral("anNativeParamDoubleSlider"));
            applyTooltip(slider, tooltip);
            slider->setTracking(sliderTrackingEnabled(node));
            controls_layout->addWidget(spin, 0);
            controls_layout->addWidget(slider, 1);
            row_layout->addWidget(controls, 0, 1);

            auto* control = new QSliderDoubleSpinBox(spin, slider, row);
            control->setValue(numberField(node, QStringLiteral("default"), 0.0));
            QObject::connect(control, &QSliderDoubleSpinBox::valueChanged, this,
                             [this, id](double) { onUserChange(id, true); });
            if (m_host.interactionWatcher)
            {
                m_host.interactionWatcher->watchInteraction(*control);
            }
            finishValueRow(node, row, title, spin, place, node.field(u"default").toVariant());
            return;
        }

        if (type == QLatin1String("int"))
        {
            title = new QLabel(label, row);
            title->setObjectName(QStringLiteral("anNativeParamLabel"));
            panel_rows::setUpRowLabel(title);
            applyTooltip(row, tooltip);
            applyTooltip(title, tooltip);
            row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

            auto* controls = new QWidget(row);
            auto* controls_layout = new QHBoxLayout(controls);
            controls_layout->setContentsMargins(0, 0, 0, 0);
            controls_layout->setSpacing(spacing.sm);

            auto* spin = new QSpinBox(controls);
            spin->setObjectName(QStringLiteral("anNativeParamSpinBox"));
            applyTooltip(spin, tooltip);
            spin->setMinimum(panel_rows::clampToInt(numberField(node, QStringLiteral("min"), -1000)));
            spin->setMaximum(panel_rows::clampToInt(numberField(node, QStringLiteral("max"), 1000)));
            spin->setSingleStep((std::max)(1, panel_rows::clampToInt(numberField(node, QStringLiteral("step"), 1))));

            auto* slider = new QSlider(Qt::Horizontal, controls);
            slider->setObjectName(QStringLiteral("anNativeParamSlider"));
            applyTooltip(slider, tooltip);
            slider->setTracking(sliderTrackingEnabled(node));
            controls_layout->addWidget(spin, 0);
            controls_layout->addWidget(slider, 1);
            row_layout->addWidget(controls, 0, 1);

            auto* control = new QSliderSpinBox(spin, slider, row);
            control->setValue(panel_rows::clampToInt(numberField(node, QStringLiteral("default"), 0)));
            QObject::connect(control, &QSliderSpinBox::valueChanged, this, [this, id](int) { onUserChange(id, true); });
            if (m_host.interactionWatcher)
            {
                m_host.interactionWatcher->watchInteraction(*control);
            }
            finishValueRow(node, row, title, spin, place, node.field(u"default").toVariant());
            return;
        }

        if (type == QLatin1String("bool"))
        {
            title = new QLabel(label, row);
            title->setObjectName(QStringLiteral("anNativeParamLabel"));
            panel_rows::setUpRowLabel(title);

            auto* checkbox = new QCheckBox(row);
            checkbox->setObjectName(QStringLiteral("anNativeParamCheckBox"));
            applyTooltip(row, tooltip);
            applyTooltip(title, tooltip);
            applyTooltip(checkbox, tooltip);
            checkbox->setChecked(flagField(node, QStringLiteral("default"), false));
            QObject::connect(checkbox, &QCheckBox::checkStateChanged, this,
                             [this, id](Qt::CheckState) { onUserChange(id, false); });
            row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
            row_layout->addWidget(checkbox, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
            finishValueRow(node, row, title, checkbox, place, node.field(u"default").toVariant());
            return;
        }

        if (type == QLatin1String("choice"))
        {
            title = new QLabel(label, row);
            title->setObjectName(QStringLiteral("anNativeParamLabel"));
            panel_rows::setUpRowLabel(title);
            applyTooltip(row, tooltip);
            applyTooltip(title, tooltip);
            row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

            auto* combo = new QComboBox(row);
            combo->setObjectName(QStringLiteral("anNativeParamComboBox"));
            applyTooltip(combo, tooltip);
            for (const QCborValue& option_value : listField(node, QStringLiteral("options")))
            {
                const QCborArray option = option_value.toArray();
                if (option.size() >= 2)
                {
                    combo->addItem(option.at(0).toString(), option.at(1).toVariant());
                }
            }
            const QString default_value = node.field(u"default").toVariant().toString();
            for (int index = 0; index < combo->count(); ++index)
            {
                if (combo->itemData(index).toString() == default_value)
                {
                    combo->setCurrentIndex(index);
                    break;
                }
            }
            QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                             [this, id](int) { onUserChange(id, false); });

            const QCborArray inline_actions = listField(node, QStringLiteral("inline_actions"));
            if (inline_actions.isEmpty())
            {
                row_layout->addWidget(combo, 0, 1);
            }
            else
            {
                auto* controls = new QWidget(row);
                auto* controls_layout = new QHBoxLayout(controls);
                controls_layout->setContentsMargins(0, 0, 0, 0);
                controls_layout->setSpacing(spacing.sm);
                combo->setParent(controls);
                controls_layout->addWidget(combo, 1);
                for (const QCborValue& action_value : inline_actions)
                {
                    const QCborMap action = action_value.toMap();
                    const QString action_id = action.value(QStringLiteral("id")).toString().trimmed();
                    if (action_id.isEmpty())
                    {
                        continue;
                    }
                    const QCborValue action_label = action.value(QStringLiteral("label"));
                    auto* button =
                        new QPushButton(action_label.isString() ? action_label.toString() : action_id, controls);
                    button->setObjectName(QStringLiteral("anNativeParamActionButton"));
                    button->setProperty("anParamActionId", action_id);
                    button->setProperty("anProcessButtonRole", buttonRole(action));
                    applyTooltip(button, action.value(QStringLiteral("tooltip")).toString().trimmed());
                    QObject::connect(button, &QPushButton::clicked, this, [this, action_id]() {
                        say(action_id, QStringLiteral("clicked"), QCborValue(true), false);
                    });
                    controls_layout->addWidget(button, 0);
                    built.actions[action_id] = button;
                }
                row_layout->addWidget(controls, 0, 1);
            }
            finishValueRow(node, row, title, combo, place, node.field(u"default").toVariant());
            return;
        }

        if (type == QLatin1String("view_selector"))
        {
            title = new QLabel(label, row);
            title->setObjectName(QStringLiteral("anNativeParamLabel"));
            panel_rows::setUpRowLabel(title);
            applyTooltip(row, tooltip);
            applyTooltip(title, tooltip);
            row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

            auto* combo = new QComboBox(row);
            combo->setObjectName(QStringLiteral("anNativeParamViewSelector"));
            applyTooltip(combo, tooltip);
            combo->setMinimumWidth(150);
            combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            const QString default_value = textField(node, QStringLiteral("default"));
            const QString placeholder = textField(node, QStringLiteral("placeholder"), tr("Select image"));
            combo->addItem(placeholder, default_value);
            combo->setCurrentIndex(0);
            QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                             [this, id](int) { onUserChange(id, false); });
            row_layout->addWidget(combo, 0, 1);
            finishValueRow(node, row, title, combo, place, default_value);
            return;
        }

        if (type == QLatin1String("string"))
        {
            title = new QLabel(label, row);
            title->setObjectName(QStringLiteral("anNativeParamLabel"));
            panel_rows::setUpRowLabel(title);
            applyTooltip(row, tooltip);
            applyTooltip(title, tooltip);
            row_layout->addWidget(title, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

            auto* edit = new QLineEdit(row);
            edit->setObjectName(QStringLiteral("anNativeParamLineEdit"));
            applyTooltip(edit, tooltip);
            edit->setText(textField(node, QStringLiteral("default")));
            edit->setPlaceholderText(textField(node, QStringLiteral("placeholder"), label));
            QObject::connect(edit, &QLineEdit::textChanged, this,
                             [this, id](const QString&) { onUserChange(id, true); });
            row_layout->addWidget(edit, 0, 1);
            finishValueRow(node, row, title, edit, place, node.field(u"default").toVariant());
            return;
        }

        delete row;
    };

    const Placement top = [panel_layout]() { return panel_layout; };
    for (const PanelNode& node : description.nodes)
    {
        buildNode(node, top);
    }

    if (hasVisibleContent())
    {
        for (QVBoxLayout* tab_layout : tab_page_layouts)
        {
            if (tab_layout)
            {
                tab_layout->addStretch(1);
            }
        }
        panel_layout->addStretch(1);
    }
}

// ---- What the panel holds.

QCborMap ExtensionParamPanel::values() const
{
    QCborMap map;
    for (const Built::Entry& entry : m_built->entries)
    {
        if (holdsDoubleValue(entry.kind))
        {
            if (const auto* spin = qobject_cast<const QDoubleSpinBox*>(entry.widget.data()))
            {
                map.insert(entry.id, spin->value());
            }
        }
        else if (entry.kind == QLatin1String("int"))
        {
            if (const auto* spin = qobject_cast<const QSpinBox*>(entry.widget.data()))
            {
                map.insert(entry.id, spin->value());
            }
        }
        else if (entry.kind == QLatin1String("bool"))
        {
            if (const auto* checkbox = qobject_cast<const QCheckBox*>(entry.widget.data()))
            {
                map.insert(entry.id, checkbox->isChecked());
            }
        }
        else if (holdsComboValue(entry.kind))
        {
            if (const auto* combo = qobject_cast<const QComboBox*>(entry.widget.data()))
            {
                map.insert(entry.id, combo->currentData().toString());
            }
        }
        else if (entry.kind == QLatin1String("string"))
        {
            if (const auto* edit = qobject_cast<const QLineEdit*>(entry.widget.data()))
            {
                map.insert(entry.id, edit->text());
            }
        }
        else if (entry.kind == QLatin1String("curve_editor"))
        {
            if (auto* editor = dynamic_cast<CurvesWidget*>(entry.widget.data()))
            {
                map.insert(entry.id, curvePointsFromEditor(editor));
            }
        }
    }
    return map;
}

bool ExtensionParamPanel::setValue(const QString& id, const QCborValue& value)
{
    if (id.isEmpty())
    {
        return false;
    }
    const Built::Entry* entry = m_built->entry(id);
    if (entry == nullptr)
    {
        return false;
    }

    if (holdsDoubleValue(entry->kind))
    {
        auto* spin = qobject_cast<QDoubleSpinBox*>(entry->widget.data());
        if (!spin)
        {
            return false;
        }
        const double next_value = value.toDouble(spin->value());
        const bool changed = std::abs(spin->value() - next_value) > 1.0e-12;
        spin->setValue(next_value);
        return changed;
    }

    if (entry->kind == QLatin1String("int"))
    {
        auto* spin = qobject_cast<QSpinBox*>(entry->widget.data());
        if (!spin)
        {
            return false;
        }
        const int next_value = panel_rows::clampToInt(static_cast<double>(value.toInteger(spin->value())));
        const bool changed = spin->value() != next_value;
        spin->setValue(next_value);
        return changed;
    }

    if (entry->kind == QLatin1String("bool"))
    {
        auto* checkbox = qobject_cast<QCheckBox*>(entry->widget.data());
        if (!checkbox)
        {
            return false;
        }
        const bool next_value = value.toBool(checkbox->isChecked());
        const bool changed = checkbox->isChecked() != next_value;
        checkbox->setChecked(next_value);
        return changed;
    }

    if (holdsComboValue(entry->kind))
    {
        auto* combo = qobject_cast<QComboBox*>(entry->widget.data());
        if (!combo)
        {
            return false;
        }

        const QVariant next_value = value.toVariant();
        for (int index = 0; index < combo->count(); ++index)
        {
            if (panel_rows::valuesEqual(combo->itemData(index), next_value))
            {
                const bool changed = combo->currentIndex() != index;
                combo->setCurrentIndex(index);
                return changed;
            }
        }
        return false;
    }

    if (entry->kind == QLatin1String("string"))
    {
        auto* edit = qobject_cast<QLineEdit*>(entry->widget.data());
        if (!edit)
        {
            return false;
        }
        const QString next_value = value.toString(edit->text());
        const bool changed = edit->text() != next_value;
        edit->setText(next_value);
        return changed;
    }

    if (entry->kind == QLatin1String("curve_editor"))
    {
        auto* editor = dynamic_cast<CurvesWidget*>(entry->widget.data());
        if (!editor)
        {
            return false;
        }

        const QCborArray before = curvePointsFromEditor(editor);
        editor->setCurve(curveFromCurvePoints(value, editor->getCurve().interpolation()));
        return before != curvePointsFromEditor(editor);
    }

    return false;
}

QStringList ExtensionParamPanel::setValues(const QCborMap& updates)
{
    QStringList changed;
    ++m_quiet;
    for (auto it = updates.begin(); it != updates.end(); ++it)
    {
        const QString id = it.key().toString();
        if (setValue(id, it.value()))
        {
            changed.push_back(id);
        }
    }
    --m_quiet;
    if (!changed.isEmpty())
    {
        updateReadouts();
        updateConditions();
    }
    return changed;
}

QStringList ExtensionParamPanel::resetToDefaults()
{
    QStringList away;
    ++m_quiet;
    for (const Built::Entry& entry : m_built->entries)
    {
        if (holdsDoubleValue(entry.kind))
        {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(entry.widget.data()))
            {
                if (std::abs(spin->value() - entry.defaultValue.toDouble()) > 1.0e-12)
                {
                    away.push_back(entry.id);
                }
                spin->setValue(entry.defaultValue.toDouble());
            }
        }
        else if (entry.kind == QLatin1String("int"))
        {
            if (auto* spin = qobject_cast<QSpinBox*>(entry.widget.data()))
            {
                if (spin->value() != entry.defaultValue.toInt())
                {
                    away.push_back(entry.id);
                }
                spin->setValue(entry.defaultValue.toInt());
            }
        }
        else if (entry.kind == QLatin1String("bool"))
        {
            if (auto* checkbox = qobject_cast<QCheckBox*>(entry.widget.data()))
            {
                if (checkbox->isChecked() != entry.defaultValue.toBool())
                {
                    away.push_back(entry.id);
                }
                checkbox->setChecked(entry.defaultValue.toBool());
            }
        }
        else if (entry.kind == QLatin1String("choice"))
        {
            if (auto* combo = qobject_cast<QComboBox*>(entry.widget.data()))
            {
                const QString default_value = entry.defaultValue.toString();
                for (int i = 0; i < combo->count(); ++i)
                {
                    if (combo->itemData(i).toString() == default_value)
                    {
                        if (combo->currentIndex() != i)
                        {
                            away.push_back(entry.id);
                        }
                        combo->setCurrentIndex(i);
                        break;
                    }
                }
            }
        }
        else if (entry.kind == QLatin1String("string"))
        {
            if (auto* edit = qobject_cast<QLineEdit*>(entry.widget.data()))
            {
                if (edit->text() != entry.defaultValue.toString())
                {
                    away.push_back(entry.id);
                }
                edit->setText(entry.defaultValue.toString());
            }
        }
        else if (entry.kind == QLatin1String("curve_editor"))
        {
            if (auto* editor = dynamic_cast<CurvesWidget*>(entry.widget.data()))
            {
                editor->setCurve(curveFromCurvePoints(QCborValue::fromVariant(entry.defaultValue),
                                                      editor->getCurve().interpolation()));
            }
        }
    }
    --m_quiet;
    updateReadouts();
    updateConditions();
    return away;
}

void ExtensionParamPanel::registerHostedValue(const QString& id, const QString& kind, QWidget* widget,
                                              const QVariant& defaultValue)
{
    if (id.isEmpty() || widget == nullptr)
    {
        return;
    }
    m_built->entries.push_back({id, kind, widget, defaultValue});
}

QWidget* ExtensionParamPanel::control(const QString& id) const
{
    const Built::Entry* entry = m_built->entry(id);
    return entry ? entry->widget.data() : nullptr;
}

QString ExtensionParamPanel::kindOf(const QString& id) const
{
    const Built::Entry* entry = m_built->entry(id);
    return entry ? entry->kind : QString();
}

QVariant ExtensionParamPanel::defaultOf(const QString& id) const
{
    const Built::Entry* entry = m_built->entry(id);
    return entry ? entry->defaultValue : QVariant();
}

QWidget* ExtensionParamPanel::widgetOf(const PanelNode& node) const
{
    const auto it = m_built->byNode.find(&node);
    return it != m_built->byNode.end() ? it->second.data() : nullptr;
}

QPushButton* ExtensionParamPanel::actionButton(const QString& id) const
{
    const auto it = m_built->actions.find(id);
    return it != m_built->actions.end() ? it->second.data() : nullptr;
}

an::ui::AnInfoBanner* ExtensionParamPanel::banner(const QString& id) const
{
    const auto it = m_built->banners.find(id);
    return it != m_built->banners.end() ? it->second.data() : nullptr;
}

std::vector<CurvesWidget*> ExtensionParamPanel::curveEditors() const
{
    std::vector<CurvesWidget*> editors;
    if (!m_built)
    {
        return editors;
    }
    for (const Built::Entry& entry : m_built->entries)
    {
        if (entry.kind == QLatin1String("curve_editor"))
        {
            if (auto* editor = dynamic_cast<CurvesWidget*>(entry.widget.data()))
            {
                editors.push_back(editor);
            }
        }
    }
    return editors;
}

const std::vector<ExtensionPanelHistogram>& ExtensionParamPanel::histograms() const noexcept
{
    return m_built->histograms;
}

bool ExtensionParamPanel::curveHeld() const noexcept
{
    return !m_heldCurve.isNull();
}

// ---- What follows the parameters.

void ExtensionParamPanel::updateReadouts()
{
    const Built& built = *m_built;
    if (built.mixLabels.empty() && built.valueLabels.empty() && built.slopes.empty())
    {
        return;
    }
    const QCborMap params = values();
    for (const Built::MixLabel& mix_label : built.mixLabels)
    {
        if (!mix_label.label || mix_label.sourceParamId.isEmpty())
        {
            continue;
        }

        const double right_fraction = std::clamp(params.value(mix_label.sourceParamId).toDouble(0.0), 0.0, 1.0);
        const int right_percent = panel_rows::clampToInt(std::round(right_fraction * 100.0));
        const int left_percent = (std::max)(0, 100 - right_percent);
        mix_label.label->setText(QStringLiteral("%1% %2, %3% %4")
                                     .arg(left_percent)
                                     .arg(mix_label.leftLabel)
                                     .arg(right_percent)
                                     .arg(mix_label.rightLabel));
    }

    for (const Built::ValueLabel& value_label : built.valueLabels)
    {
        if (!value_label.label || value_label.sourceParamId.isEmpty())
        {
            continue;
        }

        const double value = params.value(value_label.sourceParamId).toDouble(0.0);
        QString description;
        for (const Built::ValueRange& range : value_label.ranges)
        {
            if (value >= range.min && value <= range.max)
            {
                description = range.text;
                break;
            }
        }

        QString text = QStringLiteral("%1%2%3")
                           .arg(value_label.prefix)
                           .arg(value, 0, 'f', value_label.decimals)
                           .arg(value_label.suffix);
        if (!description.isEmpty())
        {
            text += value_label.separator + description;
        }
        value_label.label->setText(text);
    }

    for (const auto& [id, graph] : built.slopes)
    {
        if (graph)
        {
            graph->setVectorsFromParams(params);
        }
    }
}

void ExtensionParamPanel::updateConditions()
{
    if (m_built->conditionals.empty())
    {
        return;
    }

    const QCborMap params = values();
    for (const Built::Conditional& conditional : m_built->conditionals)
    {
        if (!conditional.widget)
        {
            continue;
        }

        if (conditional.visibleCondition.has_value())
        {
            conditional.widget->setVisible(
                an::ui::extensions::schema_utils::evaluateSchemaCondition(*conditional.visibleCondition, params));
        }

        bool enabled = true;
        if (conditional.enabledCondition.has_value())
        {
            enabled = enabled &&
                      an::ui::extensions::schema_utils::evaluateSchemaCondition(*conditional.enabledCondition, params);
        }

        const QVariant actual = params.value(conditional.paramId).toVariant();
        if (conditional.hasEquals)
        {
            enabled = enabled && panel_rows::valuesEqual(actual, conditional.equals);
        }
        if (conditional.hasNotEquals)
        {
            enabled = enabled && !panel_rows::valuesEqual(actual, conditional.notEquals);
        }
        conditional.widget->setEnabled(enabled);
    }
}

// ---- What a person does.

void ExtensionParamPanel::onUserChange(const QString& id, const bool coalescable)
{
    if (m_quiet > 0)
    {
        return;
    }
    updateReadouts();
    updateConditions();
    say(id, QStringLiteral("value_changed"), values().value(id), coalescable);
}

void ExtensionParamPanel::onCurveChanged(const QString& id)
{
    if (m_quiet > 0)
    {
        return;
    }
    updateReadouts();
    if (m_heldCurve)
    {
        // The conditions wait for the curve's release, which says the change once more.
        m_changedWhileHeld = true;
    }
    else
    {
        updateConditions();
    }
    say(id, QStringLiteral("value_changed"), values().value(id), true);
}

bool ExtensionParamPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched && event && watched->objectName() == QLatin1String("anNativeParamCurveEditor"))
    {
        const QString id = watched->property("anParamId").toString();
        switch (event->type())
        {
        case QEvent::MouseButtonPress: {
            const auto* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->button() == Qt::LeftButton)
            {
                // A curve dragged is previewed as a dragged slider is, and settled when let go.
                m_heldCurve = watched;
                m_changedWhileHeld = false;
                say(id, QStringLiteral("pressed"), QCborValue(), false);
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            const auto* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->button() == Qt::LeftButton && m_heldCurve == watched)
            {
                const bool changed = m_changedWhileHeld;
                m_heldCurve.clear();
                m_changedWhileHeld = false;
                if (changed && m_quiet == 0)
                {
                    updateConditions();
                }
                say(id, QStringLiteral("released"), changed ? values().value(id) : QCborValue(), false);
            }
            break;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ExtensionParamPanel::say(const QString& id, const QString& name, const QCborValue& value, const bool coalescable)
{
    if (m_quiet > 0 || id.isEmpty() || name.isEmpty())
    {
        return;
    }
    if (m_delivery == EventDelivery::EachChange)
    {
        controlEvent(id, name, value, coalescable);
        return;
    }
    if (!coalescable)
    {
        // What waits was done first: it is said first.
        flushWaitingEvents();
        controlEvent(id, name, value, false);
        return;
    }
    const auto waiting = std::find_if(m_waiting.begin(), m_waiting.end(),
                                      [&id](const std::pair<QString, QCborValue>& event) { return event.first == id; });
    if (waiting != m_waiting.end())
    {
        waiting->second = value;
    }
    else
    {
        m_waiting.emplace_back(id, value);
    }
    if (!m_flushScheduled)
    {
        m_flushScheduled = true;
        QTimer::singleShot(0, this, [this]() { flushWaitingEvents(); });
    }
}

void ExtensionParamPanel::flushWaitingEvents()
{
    m_flushScheduled = false;
    std::vector<std::pair<QString, QCborValue>> waiting = std::move(m_waiting);
    m_waiting.clear();
    for (const auto& [id, value] : waiting)
    {
        controlEvent(id, QStringLiteral("value_changed"), value, true);
    }
}

// ---- Patches.

bool ExtensionParamPanel::applyPatches(const std::vector<ExtensionPanelPatch>& patches, std::string* error)
{
    ++m_quiet;
    bool applied = true;
    bool valueChanged = false;
    for (const ExtensionPanelPatch& patch : patches)
    {
        if (!applyPatch(patch, error))
        {
            applied = false;
            break;
        }
        valueChanged = valueChanged || patch.property == QLatin1String("value");
    }
    --m_quiet;
    if (valueChanged)
    {
        updateReadouts();
        updateConditions();
    }
    return applied;
}

bool ExtensionParamPanel::applyPatch(const ExtensionPanelPatch& patch, std::string* error)
{
    const auto refuse = [error, &patch](const std::string& why) {
        if (error)
        {
            *error = "the patch of " + patch.property.toStdString() + " on '" + patch.id.toStdString() + "' " + why;
        }
        return false;
    };
    const auto patchable = m_built->patchables.find(patch.id);
    if (patchable == m_built->patchables.end() || !patchable->second.outer)
    {
        return refuse("names no control of the panel");
    }
    QWidget* outer = patchable->second.outer.data();
    const QString& property = patch.property;

    if (property == QLatin1String("value"))
    {
        if (m_built->entry(patch.id) == nullptr)
        {
            return refuse("names a control that holds no value");
        }
        if (!isPanelValue(patch.value))
        {
            return refuse("is not a value a control holds");
        }
        (void)setValue(patch.id, patch.value);
        return true;
    }
    if (property == QLatin1String("enabled") || property == QLatin1String("visible"))
    {
        if (!patch.value.isBool())
        {
            return refuse("is not true or false");
        }
        if (property == QLatin1String("enabled"))
        {
            outer->setEnabled(patch.value.toBool());
        }
        else
        {
            outer->setVisible(patch.value.toBool());
        }
        return true;
    }
    if (property == QLatin1String("tooltip"))
    {
        if (!patch.value.isString())
        {
            return refuse("is not a text");
        }
        outer->setToolTip(patch.value.toString());
        if (QWidget* control = this->control(patch.id))
        {
            control->setToolTip(patch.value.toString());
        }
        return true;
    }
    if (property == QLatin1String("text"))
    {
        if (!patch.value.isString())
        {
            return refuse("is not a text");
        }
        const QString text = patch.value.toString();
        if (patchable->second.label)
        {
            patchable->second.label->setText(text);
        }
        else if (auto* bannerWidget = dynamic_cast<an::ui::AnInfoBanner*>(outer))
        {
            bannerWidget->setMessage(text);
        }
        else if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(outer))
        {
            card->setTitle(text);
        }
        else if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(outer))
        {
            section->setTitle(text);
        }
        else if (QPushButton* button = actionButton(patch.id))
        {
            button->setText(text);
        }
        else if (auto* tabs =
                     qobject_cast<QTabWidget*>(outer->parentWidget() ? outer->parentWidget()->parentWidget() : nullptr))
        {
            tabs->setTabText(tabs->indexOf(outer), text);
        }
        else
        {
            return refuse("names a control with no text");
        }
        return true;
    }
    if (property == QLatin1String("choices"))
    {
        auto* combo = qobject_cast<QComboBox*>(control(patch.id));
        if (combo == nullptr || kindOf(patch.id) != QLatin1String("choice"))
        {
            return refuse("names a control that is not a choice");
        }
        if (!patch.value.isArray() || patch.value.toArray().isEmpty())
        {
            return refuse("states no entries: a choice needs at least one");
        }
        std::vector<std::pair<QString, QVariant>> entries;
        for (const QCborValue& option : patch.value.toArray())
        {
            if (!option.isArray() || option.toArray().size() < 2 || !option.toArray().at(0).isString() ||
                !isPanelValue(option.toArray().at(1)))
            {
                return refuse("states an entry that is not a [label, value] pair");
            }
            entries.emplace_back(option.toArray().at(0).toString(), option.toArray().at(1).toVariant());
        }
        const QVariant current = combo->currentData();
        const QSignalBlocker blocker(combo);
        combo->clear();
        int keep = 0;
        for (const auto& [label, value] : entries)
        {
            combo->addItem(label, value);
            if (panel_rows::valuesEqual(value, current))
            {
                keep = combo->count() - 1;
            }
        }
        combo->setCurrentIndex(keep);
        return true;
    }
    return refuse("is of a property the panel does not patch");
}

// ---- The look.

void ExtensionParamPanel::refreshTheme()
{
    if (m_layout)
    {
        m_layout->setSpacing(an::ui::tokens().spacing.sm);
        m_layout->setContentsMargins(0, 0, 0, 0);
    }
    for (auto* label : findChildren<QLabel*>(QStringLiteral("anNativeParamLabel")))
    {
        panel_rows::applyRowLabelTheme(label);
    }
    for (auto* label : findChildren<QLabel*>(QStringLiteral("anNativeParamPercentageMixLabel")))
    {
        applyParamReadoutLabelTheme(label);
    }
    for (auto* label : findChildren<QLabel*>(QStringLiteral("anNativeParamValueDescriptionLabel")))
    {
        applyParamReadoutLabelTheme(label);
    }
    for (auto* widget : findChildren<QWidget*>())
    {
        if (auto* card = dynamic_cast<an::ui::AnCardWidget*>(widget))
        {
            card->refreshTheme();
        }
        else if (auto* bannerWidget = dynamic_cast<an::ui::AnInfoBanner*>(widget))
        {
            bannerWidget->refreshTheme();
        }
        else if (auto* section = dynamic_cast<an::ui::AnCollapsibleSection*>(widget))
        {
            section->refreshTheme();
        }
    }
}

} // namespace an::ui::extensions
