// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A process shown as a page: its preview as the canvas, its controls in a column of a fixed width beside it.
///
/// A process window is made of parts its builder names its own way; `initializeStandardProcess` is handed them and
/// records them as the process's standard parts, which the page is arranged from. Nothing the framework points at is
/// deleted: the panes are reparented and the layouts moved, the button box is kept where it was and the footer's
/// buttons press its own, so every connection the framework made still holds.

#include "AfterNightControlsExport.h"
#include "controls/ProcessPresentation.h"

#include <QDialogButtonBox>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class ImageViewControl;
class QAbstractButton;
class QCheckBox;
class QComboBox;
class QLayout;
class QMenu;
class QScrollArea;
class QWidget;

namespace an::ui
{

class AnCollapsibleSection;
class AnInfoBanner;
struct JobHeading;

/// The parts of a standard process window, as `initializeStandardProcess` is handed them.
struct ProcessStandardParts
{
    /// The window's main layout: the row of the controls and the preview, or the one that holds the splitter between
    /// them.
    QLayout* mainLayout = nullptr;

    /// The layout the preview view is added to.
    QLayout* previewLayout = nullptr;

    /// The checkbox that shows and hides the preview; null for a process whose preview is always shown.
    QCheckBox* previewCheckbox = nullptr;

    /// The window's buttons: Apply, Close, Reset, or what its builder put there.
    QDialogButtonBox* buttonBox = nullptr;

    /// The image the process runs on, and the channel; null where the builder has none.
    QComboBox* targetSelector = nullptr;
    QComboBox* channelSelector = nullptr;
};

/// The tone of a page's message.
enum class PageMessageTone : std::uint8_t
{
    Info,
    Warning,
    Error
};

/// The width of a page's column for a window of a width: 360 or 520 px, and 320 or 440 px in a window under 1200 px
/// wide.
[[nodiscard]] AFTERNIGHTCONTROLS_API int pageColumnWidth(ProcessColumnWidth width, int windowWidth);

/// A process arranged as a page inside its own widget:
///
/// ```
/// process widget
/// |- canvas pane (stretch 1)   the preview view - a mirror page's view of its target - and the canvas's foot under it
/// |                            (a shell's history strip)
/// '- column (fixed width)
///     |- readout slot          a shell's compact readout; empty in a shell that has none
///     |- header                the category as an eyebrow, the process's name (its description as the tooltip), Close
///     |- toggles               Preview, Preview area, and the Compare pill at the right
///     |- banner                the process's message, hidden when there is none; during a run, the run's own row
///     |- body                  scrolls: Target image and Channel first, the controls' top-level groups as sections
///     '- footer                Reset . stretch . Close . Apply; Cancel in place of Apply during a run
/// ```
///
/// **A process with no preview** - no preview layout among its parts - is a mirror page: what its window laid out is
/// moved whole into the body, in order, the window's margins its padding, and the canvas shows the view of the target
/// its process lays there (`setCanvasView`). It has no toggles row.
///
/// **The canvas with nothing to show** says why: the images the process still needs, by their names in its controls,
/// while some are not chosen (`setMissingInputs`) - a process launched on no image chooses every image it reads; what
/// the canvas holds is hidden meanwhile - and that the preview is off otherwise. A canvas shows something when it
/// holds a view, or a widget of the process's own that marks itself as the canvas's content with the dynamic property
/// `anCanvasContent` (a grid of previews).
///
/// **A full page** (`Description::fullPage`) is the process whole, with no canvas and no column: what its window laid
/// out is moved, in order, into a scroll area that fills the page and scrolls only when the page is smaller than the
/// process's least size. Its own menu bar is hidden there and its menus offered (`menus`) for a shell to put under a
/// button. The parts of a column - the header, the body, the footer, the canvas - are null on a full page. At its head,
/// over the area, it says the process's message as a column's banner does and, during a run, shows the run's own row
/// in the message's place with Cancel at its end, which asks the process to stop the run: the process's own controls
/// stay as the process leaves them, as they do behind a progress window. A process that shows its run in itself - an
/// integration, whose run's view has its own Cancel - has no such row (`Description::runAtHead`).
///
/// **A job** (`setJobHeading`). A process that holds a body of work with a name of its own - an integration - says it
/// here for a shell's task header to show in place of the process's name alone (`components/AnTaskHeader.h`,
/// `JobHeading`): its name and its facts, a measure of it, and the acts of its own the header offers. The process
/// states it once it is a page and again whenever it changes; the page keeps it and tells one listener.
///
/// **The body fits its column** by three means: it scrolls up and down, never sideways, the footer staying in view;
/// the controls' top-level group boxes are sections that open and close, titled as the groups, in order - none when the
/// controls have one group; and the controls' own side margins, which are the body's padding, give way when the
/// controls would be wider than the column. Sections are independent, except that opening one that would push the body
/// past the column's height closes the one used longest ago. Which sections are open is kept with the process's
/// settings.
///
/// **The footer** shows what the button box holds, in the order Reset (quiet), a stretch, Close, the primary (Apply,
/// Ok): each of its buttons presses the box's own, whose enabled state, text and tooltip it shows.
///
/// Made, laid out and used on the GUI thread; it lives as long as the process it arranges.
class AFTERNIGHTCONTROLS_API ProcessPageLayout final
{
public:
    /// What the page states of its process.
    struct Description
    {
        /// The process's name, the column's title.
        QString title;

        /// What the process does, the title's tooltip.
        QString description;

        /// Its column's width.
        ProcessColumnWidth columnWidth = ProcessColumnWidth::Standard;

        /// The key its open sections are kept under in the process settings store; none are kept when empty.
        QString settingsKey;

        /// Whether the process has a preview, and so the toggles row.
        bool hasPreview = true;

        /// Whether the controls take every height the body leaves them, so a part of them that stretches - an editor -
        /// grows with the column; otherwise they keep their own height at the body's top.
        bool controlsFillTheBody = false;

        /// Whether the page is the process whole, with no canvas and no column.
        bool fullPage = false;

        /// Whether a full page shows the process's run at its head - what the run does, how far it has come, and
        /// Cancel; not for a process that shows its run in itself. A page with a column always shows its run.
        bool runAtHead = true;
    };

    /// Arranges `process` - whose layout is the window's main layout or holds it - as a page.
    ///
    /// @param process the process's widget; the page is laid out inside it and is its child.
    /// @param parts the parts its builder made.
    /// @param description what the page states of it.
    ProcessPageLayout(QWidget& process, const ProcessStandardParts& parts, Description description);
    ~ProcessPageLayout();

    ProcessPageLayout(const ProcessPageLayout&) = delete;
    ProcessPageLayout& operator=(const ProcessPageLayout&) = delete;
    ProcessPageLayout(ProcessPageLayout&&) = delete;
    ProcessPageLayout& operator=(ProcessPageLayout&&) = delete;

    // ---- The parts

    /// The page: the canvas pane beside the column, the process widget's only shown child.
    [[nodiscard]] QWidget* page() const noexcept;

    /// The canvas, where the preview lies.
    [[nodiscard]] QWidget* canvas() const noexcept;

    /// The place under the canvas, empty until a shell puts something there.
    [[nodiscard]] QWidget* canvasFoot() const noexcept;

    [[nodiscard]] QWidget* column() const noexcept;
    [[nodiscard]] QWidget* readoutSlot() const noexcept;
    [[nodiscard]] QWidget* header() const noexcept;
    [[nodiscard]] QWidget* toggles() const noexcept;
    [[nodiscard]] AnInfoBanner* banner() const noexcept;

    /// The run's own row: shown in the banner's place during a run; null on a full page whose process shows its run
    /// in itself.
    [[nodiscard]] QWidget* runBanner() const noexcept;

    /// How many bars the run's row shows: none outside a run, one, or two over a sequence - Overall and Processing.
    [[nodiscard]] int runBars() const;
    [[nodiscard]] QScrollArea* body() const noexcept;

    /// The widget the body scrolls.
    [[nodiscard]] QWidget* bodyContent() const noexcept;
    [[nodiscard]] QWidget* footer() const noexcept;

    /// The controls' top-level groups as sections, in order; none for controls of one group.
    [[nodiscard]] const std::vector<AnCollapsibleSection*>& sections() const noexcept;

    /// The parts the page was arranged from.
    [[nodiscard]] const ProcessStandardParts& parts() const noexcept;

    /// A full page's scroll area, which holds the process whole; null on a page with a column.
    [[nodiscard]] QScrollArea* fullArea() const noexcept;

    /// A full page's menus: the menus of the process's own menu bar, in its order, which the page hides; none for a
    /// process with no menu bar and on a page with a column.
    [[nodiscard]] QList<QMenu*> menus() const;

    /// States the job the process holds, for a shell's task header to say; said again as it is, it tells nothing.
    void setJobHeading(const JobHeading& job);

    /// The job the process stated; null for a process that states none, which its header names by its process.
    [[nodiscard]] const JobHeading* jobHeading() const noexcept;

    /// Tells a listener, on the GUI thread, each time the job the process states changes.
    void setJobHeadingListener(std::function<void()> listener);

    /// The footer's button for a standard button of the box; null for one the box does not hold.
    [[nodiscard]] QAbstractButton* footerButton(QDialogButtonBox::StandardButton which) const;

    /// The footer's Cancel, shown in place of the primary button during a run; on a full page, the Cancel at the end
    /// of the run's row, and null where the process shows its run in itself.
    [[nodiscard]] QAbstractButton* cancelButton() const noexcept;

    /// The header's Close.
    [[nodiscard]] QAbstractButton* headerCloseButton() const noexcept;

    /// The toggles row's Preview area and Compare; hidden while the preview view offers none.
    [[nodiscard]] QAbstractButton* previewAreaToggle() const noexcept;
    [[nodiscard]] QAbstractButton* compareToggle() const noexcept;

    /// The column's width now.
    [[nodiscard]] int columnWidth() const;

    // ---- What the page shows

    /// The column's title: the process's name, which a shell states as the process's menu entry and task header state
    /// it; until one does, the name `Description::title` gave.
    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    /// What the process does, the title's tooltip: `Description::description`, and the process's description each time
    /// it states another - an extension's process states its credits there once its host has told them.
    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;

    /// The eyebrow over the title: the process's category, which a shell names.
    void setCategory(const QString& category);
    [[nodiscard]] QString category() const;

    /// A quiet tag after the title - "extension" for a process whose code runs in a process of its own - and what its
    /// tooltip says; an empty text shows none, as a page does until a process states one.
    void setTag(const QString& text, const QString& toolTip);
    [[nodiscard]] QString tag() const;
    [[nodiscard]] QString tagToolTip() const;

    /// What the toggles row's Compare names the image the canvas shows and the alternate it shows instead, and its
    /// tooltip: "Processed" and "Original" for a process's result and the image before it; a process whose alternate is
    /// another image names the two.
    void setCompareNames(const QString& shown, const QString& alternate, const QString& toolTip);

    /// The preview view the toggles row's Preview area and Compare act on; null while the preview is off.
    void setPreviewView(ImageViewControl* preview);

    /// Lays a view in the canvas, filling it, and makes it the view the page shows (`previewView`): a mirror page's
    /// view of its target. The canvas's layout holds it; null takes it out.
    void setCanvasView(ImageViewControl* view);

    /// The view the canvas shows now - the preview, or a mirror page's view of its target; null while the preview is
    /// off.
    [[nodiscard]] ImageViewControl* previewView() const;

    /// Tells a listener, on the GUI thread, each time the view the canvas shows is made or goes: the view, or null.
    void setPreviewViewListener(std::function<void(ImageViewControl*)> listener);

    /// The images the process still needs, by their names in its controls - "Star image" - which the canvas names while
    /// it shows no view; none once every one is chosen.
    void setMissingInputs(const QStringList& inputs);

    /// What the canvas says while it shows no view - the images still to choose, or that the preview is off - its lines
    /// joined by a line break; empty while it shows one, and on a full page.
    [[nodiscard]] QString canvasMessage() const;

    /// The process's message, as `TopLevelProcessBaseControl::showProcessMessage` states it; empty hides the banner.
    /// The acts the message offered before go with it.
    void showMessage(const QString& text, PageMessageTone tone, std::optional<double> progressPercent);
    [[nodiscard]] QString message() const;

    /// The acts the message shown now offers at its end - Restart, Close - in order, until the next message; none
    /// takes them away.
    void setMessageActions(std::vector<ProcessMessageAction> actions);
    [[nodiscard]] QStringList messageActions() const;

    /// Where the process's run stands: during a run the run's row replaces the banner, the toggles, Reset and Close are
    /// disabled, the body's controls are disabled and Cancel takes the primary button's place.
    void setRunState(const ProcessRunState& state);

    /// What the header's Close, the footer's Cancel and a section's change ask of the process.
    struct Hooks
    {
        std::function<void()> close;
        std::function<void()> cancelRun;
    };
    void setHooks(Hooks hooks);

    /// Lays the column out for the width of the window the page is in now.
    void windowResized();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
