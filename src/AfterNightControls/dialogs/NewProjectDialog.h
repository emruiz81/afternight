// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The New Project dialog: what a new project is called, where its folder is made and what it starts with.

#include "AfterNightControlsExport.h"

#include "controls/ImageHistoryStepCodec.h"

#include <QDialog>
#include <QString>

#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class PathPickerWidget;
class QCheckBox;
class QLineEdit;
class QPushButton;

namespace an::ui
{

class AnInfoBanner;
class AnSegmentedControl;

/// What a folder is to the project that would be made in it.
enum class NewProjectFolderState : std::uint8_t
{
    /// Not there yet, or there and empty.
    Free,

    /// A project already: the dialog offers to open it.
    Project,

    /// It holds files and is no project.
    Occupied
};

/// What the dialog starts from.
struct NewProjectOptions
{
    /// The folder the project's folder is offered in.
    QString location;

    /// The compression chosen as the dialog opens: the setting's.
    history::StepCompression compression = history::StepCompression::Lz4;

    /// The object the field starts with, when one is suggested, and the caption that says where it comes from.
    QString suggestedObject;
    QString objectCaption;

    /// How many of the open images have a file: with any, the dialog offers to add them to the project.
    int openImages = 0;

    /// The size of the open images' undo history; stated, the dialog offers to move it into the project.
    std::optional<qint64> openHistoryBytes;

    /// What a folder is; empty, every folder is free.
    std::function<NewProjectFolderState(const QString& folder)> folderState;
};

/// What the person chose.
struct NewProjectChoice
{
    QString name;
    QString location;

    /// The project's folder: the name under the location.
    QString folder;

    /// The object; empty for none.
    QString object;

    history::StepCompression compression = history::StepCompression::Lz4;
    bool addOpenImages = false;
    bool moveHistory = false;

    /// The folder is a project already and the person chose to open it instead.
    bool openExisting = false;
};

/// "New Project", 560 by 600: the Name; the Location with a folder button, and under it the folder the project will
/// be in, in the monospace; the Object, optional, with a caption that says where a suggestion comes from; the History
/// compression as segments - LZ4 (fast), None, Zstandard (smallest), the setting's chosen - over "History steps are
/// XISF images that open in other programs"; with images open, "Add the N open images to the project" and, when their
/// history's size is stated, "Move their undo history into the project (size)"; the note that images stay where they
/// are; Cancel and Create project.
///
/// The name is the folder's, so it must be one a folder can take, and the folder must be free: a name that is none, a
/// location that is no folder's path, and a folder that holds files and is no project are said in a line over the
/// buttons and Create project is withdrawn. A folder that is a project already is said too, and the button offers
/// `Open project` instead. The dialog makes nothing: accepted, `choice()` says what was chosen.
class AFTERNIGHTCONTROLS_API NewProjectDialog : public QDialog
{
    W_OBJECT(NewProjectDialog)

public:
    static constexpr int kWidth = 560;
    static constexpr int kHeight = 600;

    explicit NewProjectDialog(NewProjectOptions options, QWidget* parent = nullptr);
    ~NewProjectDialog() override;

    /// What the dialog states now.
    [[nodiscard]] NewProjectChoice choice() const;

    /// The folder the project would be in; empty until a name and a location are stated.
    [[nodiscard]] QString projectFolder() const;

    /// What keeps the project from being made, as the dialog says it; empty when it can be made or opened.
    [[nodiscard]] QString problem() const;

    /// Whether the folder is a project already, which the button then offers to open.
    [[nodiscard]] bool offersToOpen() const;

    [[nodiscard]] QLineEdit* nameField() const noexcept;
    [[nodiscard]] PathPickerWidget* locationPicker() const noexcept;
    /// The line under the location that shows the project's folder.
    [[nodiscard]] QWidget* folderLine() const noexcept;
    [[nodiscard]] QLineEdit* objectField() const noexcept;
    [[nodiscard]] AnSegmentedControl* compressionPicker() const noexcept;
    [[nodiscard]] QCheckBox* addOpenImagesCheck() const noexcept;
    [[nodiscard]] QCheckBox* moveHistoryCheck() const noexcept;
    [[nodiscard]] AnInfoBanner* messageBanner() const noexcept;
    [[nodiscard]] QPushButton* createButton() const noexcept;
    [[nodiscard]] QPushButton* cancelButton() const noexcept;

private:
    /// Reads what the fields state: the folder's line, the message and what the button offers.
    void refresh();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
