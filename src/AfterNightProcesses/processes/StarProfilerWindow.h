// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The window both star profilers are: a detection run on the thread pool and shown when it lands, a table of the
/// stars a person curates - selected, ordered, deleted, filtered - drawn over the view, profiles loaded and saved on
/// the thread pool, and a mask painted from the stars in a window that waits for nothing.
///
/// **Nothing waits on the GUI thread.** A detection snapshots the dialog on the GUI thread, takes its read lease and
/// then its admission on a worker - an edit in place holds the writer while it waits to be admitted - and lands through
/// the event loop only while it is the run last asked for; a profile is read and written the same way; the stars
/// filter and the mask window are shown, never run modally, so the event loop - and the extension host's heartbeat it
/// carries - keeps turning while a person works in them. The window holds no lock a worker holds.
///
/// **The two layouts differ only in what they ask and show.** A layout states its dialog's request, its table's
/// columns and figures, its ordering, its filter window and its document's generation; every rule is here.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageProcessControl.h"
#include "controls/ImageStarSelectionController.h"
#include "controls/StarsFilterControlHandler.h"
#include "processes/StarProfilerSupport.h"
#include "widgets/TableWidget.h"

#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

class ImageViewControl;
class ProfiledStarMaskCreatorControl;
class QCheckBox;
class QCloseEvent;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace an::ui::processes
{

/// What one detection made: the stars a layout keeps, the document header that records how they were found, and what
/// the solution and the catalog added.
struct StarProfileResult
{
    /// The stars, in the order the table first shows them.
    an::ui::StarList stars;

    /// The document without its stars.
    an::StarProfilePayload header;

    /// Each star's world coordinate and catalog match.
    StarEnrichmentTable enrichment;

    /// Why the catalog added nothing, when it could not be read.
    std::optional<an::Status> catalogFailure;
};

/// A layout's detection, run on a worker under the run's admitted execution over the image under its lease. The
/// layout builds it on the GUI thread from its dialog, so a worker reads none of the window.
using StarProfileWork =
    std::function<an::Result<StarProfileResult>(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource&)>;

/// The controls both layouts have, which the window drives.
struct StarProfilerWidgets
{
    TableWidget* table = nullptr;
    QPushButton* detect = nullptr;
    QPushButton* reset = nullptr;
    QPushButton* load = nullptr;
    QPushButton* save = nullptr;
    QPushButton* clear = nullptr;
    QPushButton* filter = nullptr;
    QPushButton* mask = nullptr;
    QPushButton* deleteStar = nullptr;
    QLabel* detectedStars = nullptr;
    QCheckBox* centerSelected = nullptr;
    QDoubleSpinBox* zoom = nullptr;
    QDialogButtonBox* buttons = nullptr;
};

} // namespace an::ui::processes

/// A star profiler's window. A layout derives from it.
class AFTERNIGHTPROCESSES_API StarProfilerWindow : public ImageProcessControl,
                                                   public ImageStarSelectorControl,
                                                   public TableWidgetListener,
                                                   public StarsFilterControlHandler
{
public:
    ~StarProfilerWindow() override;

    // A click over the view landed on a drawn star: its row is selected.
    void onStarSelected(size_t idx) override;

    void onRowSelected(TableWidget* sender, int row) override;
    void onColumnClicked(TableWidget* sender, int column) override;

    [[nodiscard]] an::ui::StarListPtr getCurrentStarList() override;

    // The stars a filter kept become the profile's: the list is marked edited by hand.
    void setCurrentStarList(const an::ui::StarListPtr& starList, bool fullUpdate = false) override;

    [[nodiscard]] ImageViewControl* getViewControl() override;

    void updateStarSelection(const an::ui::StarListPtr& starSelection) override;

    // Starts a detection over the view's image on the thread pool. Returns false, with nothing started, while one runs
    // or when there is no image or no session.
    bool detectStars();

    // Whether a detection is on its way.
    [[nodiscard]] bool isDetecting() const noexcept;

    // Reads a profile document on the thread pool; when it lands and the window accepts it, it replaces the stars and
    // supersedes a detection on its way. A read the window refuses changes nothing: a detection asked for before it
    // runs on and lands as if no read had been asked for. Returns false, with nothing started, when there is no
    // session.
    bool loadProfileFrom(const QString& path);

    // Writes the stars as a profile document on the thread pool. Returns false, with nothing started, when there are
    // no stars or no session.
    bool saveProfileTo(const QString& path);

    // Forgets the stars, and any detection or read on its way.
    void clearProfile();

    // Deletes the selected star.
    void deleteSelectedStar();

    // Selects a row as a click on it does.
    void selectStar(int row);

    // Orders the table by a column as a click on its header does: largest first, then smallest first on the next.
    void orderByColumn(int column);

    // Opens the layout's stars filter over the stars, shown and not modal.
    StarsFilterControlBase* openStarsFilter();

    // Opens the profiled mask window over the stars, shown and not modal: it paints on a worker and opens the mask in a
    // new view.
    ProfiledStarMaskCreatorControl* openMaskCreator();

    // What the solution and the catalog said about each star.
    [[nodiscard]] const an::ui::processes::StarEnrichmentTable& enrichment() const noexcept;

    // Whether a person changed the stars after they were detected or read.
    [[nodiscard]] bool manuallyEdited() const noexcept;

    // The document header the stars were found or read with; nothing before either.
    [[nodiscard]] const std::optional<an::StarProfilePayload>& profileHeader() const noexcept;

    // The document the stars make now; nothing without stars.
    [[nodiscard]] std::optional<an::StarProfileFile> currentDocument() const;

protected:
    StarProfilerWindow(ImageViewControl* control, const std::string& name);

    // Takes the layout's controls and connects them; the layout calls it once its dialog is built.
    void attachWidgets(const an::ui::processes::StarProfilerWidgets& widgets);

    // The generation of the documents the layout reads and writes.
    [[nodiscard]] virtual an::StarProfileGeneration profileGeneration() const = 0;

    // The file dialogs' filter and the folder they remember.
    [[nodiscard]] virtual QString profileFileFilter() const = 0;
    [[nodiscard]] virtual QString profileFolderContext() const = 0;

    // The name the log calls the layout by.
    [[nodiscard]] virtual std::string logName() const = 0;

    // The layout's detection, built from its dialog now.
    [[nodiscard]] virtual an::ui::processes::StarProfileWork detectionWork() const = 0;

    // The table's column titles.
    [[nodiscard]] virtual QStringList columnTitles() const = 0;

    // Fills one row of the table with a star's figures.
    virtual void fillRow(TableWidget& table, int row, const an::DetectedStar& star) const = 0;

    // The list ordered by one of the layout's columns.
    [[nodiscard]] virtual an::ui::StarList orderedBy(an::ui::StarList stars, int column, bool descending) const = 0;

    // The layout's stars filter window, made over this process.
    [[nodiscard]] virtual StarsFilterControlBase* makeStarsFilter() = 0;

    // Told when a detection starts, with the stop it runs under; a case stops it there.
    virtual void detectionStarting(std::stop_source& stop);

    void closeEvent(QCloseEvent* event) override;

private:
    void initializeTable();
    void updateControls();
    void showStars();
    void forgetRead();
    void forgetRuns();
    void centerStar(double x, double y);
    void landDetection(std::uint64_t generation, an::Result<an::ui::processes::StarProfileResult> result);
    void publishDetection(an::Result<an::ui::processes::StarProfileResult> result);
    void landLoad(std::uint64_t generation, const QString& path, an::Result<an::StarProfileFile> document);

    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_LoadProfileButtonPushed();
    void on_SaveProfileButtonPushed();
    void on_ClearProfileButtonPushed();
    void on_centerSelectedStarCheckClicked(Qt::CheckState state);
    void on_zoomDetectedStarsValueChanged(double value);

    an::ui::processes::StarProfilerWidgets m_widgets;
    std::unique_ptr<ImageStarSelectionController> m_selection;
    an::ui::StarListPtr m_stars = std::make_shared<const an::ui::StarList>();
    an::ui::processes::StarEnrichmentTable m_enrichment;
    std::optional<an::StarProfilePayload> m_header;
    bool m_manuallyEdited = false;
    bool m_detecting = false;
    bool m_closeConfirmed = false;
    std::uint64_t m_generation = 0u;
    std::shared_ptr<std::stop_source> m_runStop;
    // The read last asked for, whether it has yet to land, and what a detection that landed before it found.
    std::uint64_t m_readGeneration = 0u;
    bool m_readPending = false;
    std::shared_ptr<std::stop_source> m_readStop;
    std::optional<an::Result<an::ui::processes::StarProfileResult>> m_heldDetection;
    int m_sortColumn = -1;
    bool m_sortDescending = false;
};
