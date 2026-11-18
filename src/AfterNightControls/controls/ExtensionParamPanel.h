// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The controls of an extension's panel, built from its description.
///
/// `ExtensionParamPanel` turns an `ExtensionPanelDescription` into controls: each kind its own, drawn as the process
/// window has always drawn a parameter and the settings section a widget of its tree. It keeps what the panel holds -
/// every parameter's value and default, the readouts that follow a parameter, the conditions that show or enable a
/// control - and answers its values, takes the values and patches it is given without a word, and says what a person
/// did through its one event, `controlEvent`. It names no window and no extension: the process that shows it draws the
/// kinds that need its image (the target, the manual mask) through its host's builder, makes the preview's histogram,
/// and hears a held slider through its interaction watcher.

#include "AfterNightControlsExport.h"

#include "controls/ExtensionPanelDescription.h"

#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <verdigris/wobjectdefs.h>

class CurvesWidget;
class HistogramWidget;
class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace an::ui
{
class AnInfoBanner;
class IPreviewInteractionWatcher;
enum class HistogramResolution : std::uint8_t;
enum class PreviewHistogramResolution : std::uint8_t;
} // namespace an::ui

namespace an::ui::extensions
{

/// A row of a panel as a process draws one of its own controls in it: the label's column, its spacing, its type.
namespace panel_rows
{
/// The width of the labels' column.
inline constexpr int kLabelColumnWidth = 204;

/// Lays a row out: a label's column of `kLabelColumnWidth`, the control's column taking the rest.
AFTERNIGHTCONTROLS_API void setUpRowGrid(QGridLayout* layout);

/// A row's label: its type and colour, its fixed width, one line.
AFTERNIGHTCONTROLS_API void setUpRowLabel(QLabel* label);

/// A row's label's type and colour, read from the tokens in force.
AFTERNIGHTCONTROLS_API void applyRowLabelTheme(QLabel* label);

/// Whether two values of a control are the same: booleans as booleans, numbers within 1e-9, anything else as text.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool valuesEqual(const QVariant& left, const QVariant& right);

/// A number as an `int`, clamped to its range.
[[nodiscard]] AFTERNIGHTCONTROLS_API int clampToInt(double value);
} // namespace panel_rows

/// The resolution a real-time histogram's combo box names.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::ui::PreviewHistogramResolution
previewHistogramResolutionNamed(const QString& name);

/// `previewHistogramResolutionNamed` for a histogram counted from what is shown.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::ui::HistogramResolution histogramResolutionNamed(const QString& name);

/// What the process that shows a panel does for it.
struct ExtensionPanelHost
{
    /// Hears a slider held and let go, so a drag is previewed in interactive frames; may be null.
    an::ui::IPreviewInteractionWatcher* interactionWatcher = nullptr;

    /// Makes the first real-time histogram's widget in its scroll area, whose counts come with each frame; a plain
    /// histogram when absent or when it answers null.
    std::function<HistogramWidget*(QScrollArea* scrollArea)> makeFrameHistogram;

    /// The first real-time histogram's resolution was chosen: as the panel is built, and each time the person picks
    /// another.
    std::function<void(const QString& resolution)> frameHistogramResolutionChosen;

    /// Another histogram's resolution was chosen; its counts are to be made again.
    std::function<void(HistogramWidget* histogram)> histogramResolutionChosen;

    /// A curve's histogram was turned on; its counts are to be made.
    std::function<void(CurvesWidget* editor)> curveHistogramShown;

    /// Where a control is placed: the layout of the group it belongs in, made when first asked for when the group was
    /// opened for it, so that a builder asks once its own widgets are made, as the window always did.
    using Placement = std::function<QVBoxLayout*()>;

    /// Builds a kind the process draws (`PanelKindRole::Hosted`) into the layout it belongs in, and answers the widget
    /// it added, which the node's conditions then show and enable; null leaves the node out.
    std::function<QWidget*(const PanelNode& node, const Placement& place)> buildHosted;
};

/// A real-time histogram of a panel and its toolbar.
struct ExtensionPanelHistogram
{
    QString id;
    QPointer<HistogramWidget> histogram;
    QPointer<QScrollArea> scrollArea;
    QPointer<QComboBox> plotResolution;
    /// Whether it is the first, whose counts come with each frame.
    bool frame = false;
};

/// A change asked of a panel's control.
struct ExtensionPanelPatch
{
    /// The control's id.
    QString id;

    /// What changes: `value`, `enabled`, `visible`, `choices` (a choice's entries), `text` (a label, a title, a
    /// banner's message), `tooltip`.
    QString property;

    QCborValue value;
};

class AFTERNIGHTCONTROLS_API ExtensionParamPanel : public QWidget
{
    W_OBJECT(ExtensionParamPanel)

public:
    /// How the panel's event reaches whoever hears it.
    enum class EventDelivery
    {
        /// Of a burst of changes to one control made in one turn of the event loop, the last is said, once the turn
        /// is over; a press, a check and a pick are said at once, after any change waiting before it. For a panel
        /// whose events cross to another process.
        LastOfABurst,
        /// Every change is said as it is made. For a panel whose process reacts to each change itself, as the native
        /// process window's extension has always heard each one.
        EachChange,
    };

    explicit ExtensionParamPanel(QWidget* parent = nullptr);
    ~ExtensionParamPanel() override;

    void setEventDelivery(EventDelivery delivery);
    void setHost(ExtensionPanelHost host);

    /// Puts a widget of the process's at the top of the panel, before the description's controls; it stays until
    /// `clear`.
    void addLeadingWidget(QWidget* widget);

    /// Builds the controls of a description, once it is valid, after the leading widgets; the controls of an earlier
    /// description go. Nothing is said while it is built.
    ///
    /// @param description the description.
    /// @param error why it was refused.
    /// @return whether it was valid and built; a refused description leaves the panel holding its leading widgets.
    [[nodiscard]] bool setDescription(const ExtensionPanelDescription& description, std::string* error = nullptr);

    /// Lets every control go, the leading widgets with them.
    void clear();

    /// The description built, or none.
    [[nodiscard]] const std::optional<ExtensionPanelDescription>& description() const noexcept;

    /// Whether the panel shows anything: a leading widget or a control.
    [[nodiscard]] bool hasVisibleContent() const noexcept;

    /// The value of every parameter, by id, in the description's order.
    [[nodiscard]] QCborMap values() const;

    /// Sets parameters' values without a word, then lets the readouts and the conditions follow.
    ///
    /// @param updates values by id; an id the panel has not, or a value its control cannot take, changes nothing.
    /// @return the ids whose value changed, in the order of `updates`.
    QStringList setValues(const QCborMap& updates);

    /// Puts every parameter back to its default without a word, then lets the readouts and the conditions follow.
    ///
    /// @return the ids whose value was not its default.
    QStringList resetToDefaults();

    /// Applies changes to controls without a word, refused whole at the first one the panel cannot make.
    [[nodiscard]] bool applyPatches(const std::vector<ExtensionPanelPatch>& patches, std::string* error = nullptr);

    /// Lets the readouts follow the parameters they read: a blend's shares, a value's words, a slope graph's vectors.
    void updateReadouts();

    /// Shows and enables each control as its conditions over the parameters say.
    void updateConditions();

    /// A parameter's control - its spin box, check box, combo box, line or curve - or null.
    [[nodiscard]] QWidget* control(const QString& id) const;

    /// A parameter's kind, or empty.
    [[nodiscard]] QString kindOf(const QString& id) const;

    /// A parameter's default, or an invalid value.
    [[nodiscard]] QVariant defaultOf(const QString& id) const;

    /// An action's button, an inline one included, or null.
    [[nodiscard]] QPushButton* actionButton(const QString& id) const;

    /// A banner of an `info` node, or null.
    [[nodiscard]] an::ui::AnInfoBanner* banner(const QString& id) const;

    /// What was built for a node of the description the panel holds (`description()`): its row, its section, its
    /// banner; null for one that built nothing.
    [[nodiscard]] QWidget* widgetOf(const PanelNode& node) const;

    /// The curves, in order.
    [[nodiscard]] std::vector<CurvesWidget*> curveEditors() const;

    /// The real-time histograms, in order.
    [[nodiscard]] const std::vector<ExtensionPanelHistogram>& histograms() const noexcept;

    /// A widget of a tree the description holds, by its id, or null.
    [[nodiscard]] QWidget* treeWidget(const QString& id) const;

    /// The ids of the tree's widgets, in the order the tree states them.
    [[nodiscard]] QStringList treeWidgetIds() const;

    /// A tree widget's value: whether its kind holds one and, when it does, the value.
    ///
    /// @return false, with why, for a widget of a kind that holds a value and is not built.
    [[nodiscard]] bool treeWidgetValue(const QString& id, QCborValue* value, bool* holds,
                                       std::string* error = nullptr) const;

    /// Registers a value a hosted control holds, as the builder of a hosted kind calls it while it builds: the value is
    /// read and set and reset with the panel's own. The hosted control says its changes itself.
    ///
    /// @param id the parameter's id.
    /// @param kind the kind whose value it holds (`bool` for a check box).
    /// @param widget the control.
    /// @param defaultValue its default.
    void registerHostedValue(const QString& id, const QString& kind, QWidget* widget, const QVariant& defaultValue);

    /// Whether a curve is held now: its changes are previewed and the conditions wait for its release.
    [[nodiscard]] bool curveHeld() const noexcept;

    /// The tokens in force reach the controls the panel styles itself.
    void refreshTheme();

    /// What a person did: `value_changed` with the new value, `clicked` for an action, `pressed` and `released` for a
    /// curve taken and let go (`released` with its value). `coalescable` marks a step of a continuous change - a
    /// slider dragged, a number or a text typed. The panel's one event.
    void controlEvent(const QString& id, const QString& name, const QCborValue& value, bool coalescable)
        W_SIGNAL(controlEvent, id, name, value, coalescable);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Built;
    class CurveListener;

    [[nodiscard]] bool build(const ExtensionPanelDescription& description, std::string* error);
    void clearControls();
    void say(const QString& id, const QString& name, const QCborValue& value, bool coalescable);
    void flushWaitingEvents();
    void onUserChange(const QString& id, bool coalescable);
    void onCurveChanged(const QString& id);
    [[nodiscard]] bool setValue(const QString& id, const QCborValue& value);
    [[nodiscard]] bool applyPatch(const ExtensionPanelPatch& patch, std::string* error);

    std::unique_ptr<Built> m_built;
    std::optional<ExtensionPanelDescription> m_description;
    ExtensionPanelHost m_host;
    EventDelivery m_delivery = EventDelivery::LastOfABurst;
    std::vector<QPointer<QWidget>> m_leading;
    QVBoxLayout* m_layout = nullptr;

    /// Whether the panel is changing its own controls: nothing is said meanwhile.
    int m_quiet = 0;

    /// The coalescable changes waiting for the turn to end, in the order they were first made, each its last value.
    std::vector<std::pair<QString, QCborValue>> m_waiting;
    bool m_flushScheduled = false;

    /// The curve held now, and whether it changed while held.
    QPointer<QObject> m_heldCurve;
    bool m_changedWhileHeld = false;
};

} // namespace an::ui::extensions
