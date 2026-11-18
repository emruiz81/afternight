// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What an extension's panel holds, as data: its parameters, the groups they sit in and its actions, and nothing about
/// a window.
///
/// Two vocabularies describe an extension's controls. A process window's parameters are a list (`get_params()`): each
/// entry a parameter, an action, a readout or a mark that opens a section or a tab. An extension's settings, and a
/// panel sent through the protocol, are a tree of widgets. A description holds the kinds of both as one tree of nodes:
/// each node is a kind with its fields, and a group holds its children. The list is read into the tree by the rules the
/// process window has always drawn it by - a parameter with no section before it opens one - so that a panel built from
/// the tree is the one the window built from the list.
///
/// Every kind states its fields, what each holds, what an absent one means and how long it may be (`panelKinds()`).
/// A description is untrusted data: `validatePanelDescription` refuses one whole at the first kind it does not know, a
/// field of the wrong type, a text too long, a count past its bound, a range whose least is above its most or a choice
/// with no entries, and says which.

#include "AfterNightControlsExport.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QAnyStringView>
#include <QStringView>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace an::ui::extensions
{

/// What a field of a kind holds.
enum class PanelFieldType : std::uint8_t
{
    Text, ///< plain text: no control character but a tab or a line break; a limit counts its characters
    Number, ///< a finite real number
    Integer, ///< a whole number an `int` holds, written with no fraction
    Boolean, ///< true or false
    Scalar, ///< a control's value: a boolean, a finite number or a text
    Condition, ///< an expression over the parameters (`mode == fast`), or a map naming `param` and `equals` or
               ///< `not_equals`
    Options, ///< a choice's entries: `[label, value]` pairs, or texts that are their own value
    Integers, ///< a list of whole numbers
    Points, ///< a curve's points: `[x, y]` pairs or maps of `x` and `y`
    Ranges, ///< `[least, most, text]` triples
    Actions, ///< buttons beside a control: maps of `id`, `label`, `button_role`, `tooltip` and the preview marks
    Vectors, ///< the vectors of a slope graph: maps of `key`, `hue_param`, `saturation_param` and their look
    Columns, ///< a table's columns: texts, or maps of `id` and `label`
    Rows, ///< a table's rows: lists of values, or maps of a column's id to its value
    Lines, ///< console text: a text, or a list of texts or of maps of `text` and `stream`
};

/// The name the reference gives a field type.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* panelFieldTypeName(PanelFieldType type) noexcept;

/// One field of a kind.
struct PanelFieldSpec
{
    /// Its name in a node.
    const char* name = "";

    /// What it holds.
    PanelFieldType type = PanelFieldType::Text;

    /// What an absent field means, as the reference writes it; empty when nothing is drawn for it.
    const char* absent = "";

    /// How long it may be: characters of a text, entries of a list; 0 for the type's own bound.
    int limit = 0;

    /// What it does, in a line.
    const char* meaning = "";
};

/// What a kind is to the panel.
enum class PanelKindRole : std::uint8_t
{
    Parameter, ///< holds a value the extension reads: the panel's values are its parameters
    Group, ///< holds other nodes: a section, a card, the tabs and a tab, a column
    Action, ///< a button the person presses; the extension hears of the press
    Display, ///< shows something and holds no value: a banner, a readout, a status, a histogram
    Hosted, ///< drawn by the process that shows the panel, not by the panel: the target and the manual mask
};

/// The name the reference gives a role.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* panelKindRoleName(PanelKindRole role) noexcept;

/// Which vocabulary a kind comes from.
enum class PanelVocabulary : std::uint8_t
{
    Parameters, ///< a process window's list of parameters (`get_params()`)
    Widgets, ///< a tree of widgets: an extension's settings, a panel sent through the protocol
};

/// One kind: its name, where it comes from, what it is, and its fields.
struct PanelKindSpec
{
    const char* name = "";
    std::vector<const char*> aliases;
    PanelVocabulary vocabulary = PanelVocabulary::Parameters;
    PanelKindRole role = PanelKindRole::Display;
    bool holdsChildren = false;
    const char* meaning = "";
    std::vector<PanelFieldSpec> fields;

    /// The field of that name, or null.
    [[nodiscard]] AFTERNIGHTCONTROLS_API const PanelFieldSpec* field(QStringView fieldName) const noexcept;
};

/// Every kind a description may hold, in the order the reference lists them.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::span<const PanelKindSpec> panelKinds();

/// The kind of that name or alias, or null.
[[nodiscard]] AFTERNIGHTCONTROLS_API const PanelKindSpec* panelKind(QStringView name) noexcept;

/// The bounds a description is held to.
namespace panel_limits
{
/// How many nodes a description may hold, groups included.
inline constexpr int kMaxNodes = 512;
/// How deep groups may nest.
inline constexpr int kMaxDepth = 32;
/// The characters of an id, a label, a title or a placeholder.
inline constexpr int kMaxShortText = 256;
/// The characters of a prose text: a banner's message, a tooltip, a description.
inline constexpr int kMaxLongText = 8192;
/// The characters of a console's text.
inline constexpr int kMaxConsoleText = 262144;
/// The entries of a choice.
inline constexpr int kMaxOptions = 1024;
/// The entries of any other list: points, ranges, rows, columns, numbers.
inline constexpr int kMaxEntries = 4096;
} // namespace panel_limits

/// One node: a kind, the fields it states and, for a group, what it holds.
struct PanelNode
{
    /// The kind's name (never an alias).
    QString kind;

    /// The fields the node states, by name, in the order its kind lists them; a field it does not state is absent.
    QCborMap fields;

    /// What a group holds, in order.
    std::vector<PanelNode> children;

    /// The node's id, or empty.
    [[nodiscard]] AFTERNIGHTCONTROLS_API QString id() const;

    /// A field the node states, or an undefined value.
    [[nodiscard]] AFTERNIGHTCONTROLS_API QCborValue field(QAnyStringView name) const;

    /// Whether the node states a field.
    [[nodiscard]] AFTERNIGHTCONTROLS_API bool has(QAnyStringView name) const;

    friend bool operator==(const PanelNode& left, const PanelNode& right)
    {
        return left.kind == right.kind && left.fields == right.fields && left.children == right.children;
    }
};

/// Whether a node is the extension's credits: an `info` whose id is `attribution`, `credits` or `credit`, or whose
/// `role` or `purpose` is `attribution` or `credits`. The process that shows the panel shows them with its
/// description; the panel draws no banner for them.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isPanelCredits(const PanelNode& node);

/// How the panel is drawn: the standard look, or the custom one an extension asks for (`panel_variant`), whose
/// sections fold and whose banners warn.
enum class PanelVariant : std::uint8_t
{
    Native,
    Custom,
};

/// An extension's panel.
struct ExtensionPanelDescription
{
    PanelVariant variant = PanelVariant::Native;

    /// The nodes at the top of the panel, in order.
    std::vector<PanelNode> nodes;

    friend bool operator==(const ExtensionPanelDescription& left, const ExtensionPanelDescription& right)
    {
        return left.variant == right.variant && left.nodes == right.nodes;
    }
};

/// What reading a process window's list left out, and why: an entry with no id or no type, of a type the window never
/// drew, a second mark that closes no tabs. The window has always passed over such entries; the reading names them so
/// that they can be logged.
struct PanelReadingNotes
{
    QStringList leftOut;
};

/// Reads a process window's list of parameters into a description by the rules the window draws it by: a `meta`
/// entry's `panel_variant` is the variant (the rest of it is the window's); `tabs` opens a set of tabs at the top of
/// the panel and `tab` a page of it, opening a set when there is none; `end_tabs` closes it; `section` opens a section
/// in the page or at the top; a parameter, an action, a readout or a hosted control goes into the section open, and
/// opens one titled "Parameters" ("Creative Controls" in the custom look) when none is; a banner goes into the section
/// open or else the page; a slope graph goes into the page. Types are read trimmed and in lower case; an alias is read
/// as its kind.
///
/// @param defs the list.
/// @param notes where what was left out is written; may be null.
/// @return the description, not validated.
[[nodiscard]] AFTERNIGHTCONTROLS_API ExtensionPanelDescription
panelDescriptionFromParamDefs(const QJsonArray& defs, PanelReadingNotes* notes = nullptr);

/// Reads a tree of widgets - a map with its kind under `widget` and what it holds under `children` - into a
/// description whose one node is that tree.
///
/// @param root the tree's root.
/// @param out where the description is written.
/// @param error why the tree was refused: a node that is not a map or names no kind, children that are not a list.
/// @return whether it was read; it is not validated.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool
panelDescriptionFromWidgetTree(const QCborMap& root, ExtensionPanelDescription* out, std::string* error = nullptr);

/// The description in its own form: a map of `variant` (`native` or `custom`) and `nodes`, each node a map of its
/// `kind`, its fields and, for a group, its `children`. What `panelDescriptionFromCbor` reads back as an equal
/// description.
[[nodiscard]] AFTERNIGHTCONTROLS_API QCborMap panelDescriptionToCbor(const ExtensionPanelDescription& description);

/// Reads a description from its own form and validates it. A field its kind does not have is refused by name, as an
/// unknown kind is.
///
/// @param value the form.
/// @param out where the description is written.
/// @param error why it was refused.
/// @return whether it was read and is valid.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool
panelDescriptionFromCbor(const QCborValue& value, ExtensionPanelDescription* out, std::string* error = nullptr);

/// Holds a description to its kinds and bounds, and refuses it whole at the first error: a kind it does not know, by
/// name; more nodes than `panel_limits::kMaxNodes` or groups nested deeper than `panel_limits::kMaxDepth`; children
/// under a kind that holds none; a field of the wrong type; a text past its field's bound or holding a control
/// character; a list past its bound; a range whose least is above its most; a negative step; a choice with no entries;
/// two values under one id.
///
/// @param description the description.
/// @param error why it was refused, naming the node by its kind and id.
/// @return whether it holds.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool validatePanelDescription(const ExtensionPanelDescription& description,
                                                                   std::string* error = nullptr);

/// How many nodes a description holds, groups and their children counted.
[[nodiscard]] AFTERNIGHTCONTROLS_API int panelNodeCount(const ExtensionPanelDescription& description) noexcept;

} // namespace an::ui::extensions
