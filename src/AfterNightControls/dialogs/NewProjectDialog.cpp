// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "dialogs/NewProjectDialog.h"

#include "components/AnInfoBanner.h"
#include "components/AnLabeledField.h"
#include "components/AnSegmentedControl.h"
#include "controls/settings/PathPickerWidget.h"
#include "controls/settings/ProjectSettings.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <array>
#include <utility>

W_OBJECT_IMPL(an::ui::NewProjectDialog)

namespace an::ui
{

namespace
{

/// The compressions in the order the segments show them.
constexpr std::array<history::StepCompression, 3> kCompressions = {
    history::StepCompression::Lz4, history::StepCompression::None, history::StepCompression::Zstd};

/// The line under the location: what it says in the caption's text, then the folder in the monospace, cut in its
/// middle when the line is narrower than it; the whole folder is its tooltip.
class FolderLine final : public QWidget
{
public:
    explicit FolderLine(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("newProjectFolder"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setFolder(const QString& folder)
    {
        m_folder = folder;
        setToolTip(folder);
        setAccessibleName(lead() + QLatin1Char(' ') + folder);
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {QFontMetrics(captionFont()).horizontalAdvance(lead()) + tokens().spacing.sm, lineHeight()};
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return {0, lineHeight()};
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (m_folder.isEmpty())
        {
            return;
        }
        const DesignTokens& t = tokens();
        QPainter painter(this);
        const QFont caption = captionFont();
        const QFont mono = monoFont();
        const QString text = lead();
        const int leadWidth = QFontMetrics(caption).horizontalAdvance(text + QLatin1Char(' '));
        painter.setFont(caption);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(QRect(0, 0, leadWidth, height()), Qt::AlignLeft | Qt::AlignVCenter, text);
        painter.setFont(mono);
        painter.setPen(t.colors.textPrimary);
        const int room = (std::max)(0, width() - leadWidth);
        painter.drawText(QRect(leadWidth, 0, room, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(mono).elidedText(m_folder, Qt::ElideMiddle, room));
    }

private:
    [[nodiscard]] static QString lead()
    {
        return NewProjectDialog::tr("The project folder will be");
    }

    [[nodiscard]] static QFont captionFont()
    {
        const DesignTokens& t = tokens();
        return parts::sizedFont(t.typography.caption, t.metrics.fontCaption);
    }

    [[nodiscard]] static QFont monoFont()
    {
        const DesignTokens& t = tokens();
        return parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
    }

    [[nodiscard]] static int lineHeight()
    {
        return (std::max)(QFontMetrics(captionFont()).height(), QFontMetrics(monoFont()).height()) +
               tokens().spacing.xs;
    }

    QString m_folder;
};

} // namespace

struct NewProjectDialog::Impl
{
    NewProjectOptions options;
    QLineEdit* name = nullptr;
    PathPickerWidget* location = nullptr;
    FolderLine* folder = nullptr;
    QLineEdit* object = nullptr;
    AnSegmentedControl* compression = nullptr;
    QCheckBox* addOpenImages = nullptr;
    QCheckBox* moveHistory = nullptr;
    AnInfoBanner* message = nullptr;
    QPushButton* create = nullptr;
    QPushButton* cancel = nullptr;
    QString problem;
    bool offersToOpen = false;
};

NewProjectDialog::NewProjectDialog(NewProjectOptions options, QWidget* parent)
    : QDialog(parent), m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    impl.options = std::move(options);
    setObjectName(QStringLiteral("newProjectDialog"));
    setWindowTitle(tr("New Project"));
    setModal(true);

    const DesignTokens& t = tokens();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.spacing.xl, t.spacing.lg, t.spacing.xl, t.spacing.lg);
    column->setSpacing(t.spacing.md);

    // The name, which is the folder's.
    impl.name = new QLineEdit(this);
    impl.name->setObjectName(QStringLiteral("newProjectName"));
    column->addWidget(new AnLabeledField(tr("Name"), impl.name, AnLabeledField::LabelPlacement::Above, this));

    // The location, and under it the folder the project will be in.
    impl.location = new PathPickerWidget(PathPickerWidget::Mode::Directory, this);
    impl.location->setObjectName(QStringLiteral("newProjectLocation"));
    impl.location->setDialogTitle(tr("Select the Folder the Project Is Made In"));
    impl.location->setPath(QDir::toNativeSeparators(impl.options.location));
    auto* place = new QVBoxLayout();
    place->setContentsMargins(0, 0, 0, 0);
    place->setSpacing(t.spacing.xs);
    place->addWidget(new AnLabeledField(tr("Location"), impl.location, AnLabeledField::LabelPlacement::Above, this));
    impl.folder = new FolderLine(this);
    place->addWidget(impl.folder);
    column->addLayout(place);

    // The object, optional; a suggestion says where it comes from.
    impl.object = new QLineEdit(impl.options.suggestedObject, this);
    impl.object->setObjectName(QStringLiteral("newProjectObject"));
    auto* objectField =
        new AnLabeledField(tr("Object (optional)"), impl.object, AnLabeledField::LabelPlacement::Above, this);
    if (!impl.options.suggestedObject.isEmpty() && !impl.options.objectCaption.isEmpty())
    {
        objectField->setHint(impl.options.objectCaption);
    }
    column->addWidget(objectField);

    // How the history's steps are compressed: the setting's is chosen.
    impl.compression =
        new AnSegmentedControl(QStringList{tr("LZ4 (fast)"), tr("None"), tr("Zstandard (smallest)")}, this);
    impl.compression->setObjectName(QStringLiteral("newProjectCompression"));
    impl.compression->setAccessibleName(tr("History compression"));
    for (std::size_t index = 0; index < kCompressions.size(); ++index)
    {
        if (kCompressions[index] == impl.options.compression)
        {
            impl.compression->setCurrentIndex(static_cast<int>(index));
        }
    }
    auto* compressionRow = new QHBoxLayout();
    compressionRow->setContentsMargins(0, 0, 0, 0);
    compressionRow->addWidget(impl.compression);
    compressionRow->addStretch(1);
    auto* compressionHolder = new QWidget(this);
    compressionHolder->setLayout(compressionRow);
    auto* compressionField =
        new AnLabeledField(tr("History compression"), compressionHolder, AnLabeledField::LabelPlacement::Above, this);
    compressionField->setHint(tr("History steps are XISF images that open in other programs"));
    column->addWidget(compressionField);

    // With images open: add them, and move their history.
    const int open = impl.options.openImages;
    impl.addOpenImages =
        new QCheckBox(open == 1 ? tr("Add the open image to the project")
                                : tr("Add the %1 open images to the project").arg(QLocale().toString(open)),
                      this);
    impl.addOpenImages->setObjectName(QStringLiteral("newProjectAddOpenImages"));
    impl.addOpenImages->setChecked(open > 0);
    impl.addOpenImages->setVisible(open > 0);
    column->addWidget(impl.addOpenImages);
    const bool moves = open > 0 && impl.options.openHistoryBytes.has_value();
    const QString size =
        QLocale().formattedDataSize(impl.options.openHistoryBytes.value_or(0), 1, QLocale::DataSizeTraditionalFormat);
    impl.moveHistory = new QCheckBox(open == 1 ? tr("Move its undo history into the project (%1)").arg(size)
                                               : tr("Move their undo history into the project (%1)").arg(size),
                                     this);
    impl.moveHistory->setObjectName(QStringLiteral("newProjectMoveHistory"));
    impl.moveHistory->setChecked(moves);
    impl.moveHistory->setVisible(moves);
    column->addWidget(impl.moveHistory);
    // A history moves with its image: there is none to move for an image left out.
    QObject::connect(impl.addOpenImages, &QCheckBox::toggled, this, [this](const bool added) {
        m_impl->moveHistory->setEnabled(added);
        if (!added)
        {
            m_impl->moveHistory->setChecked(false);
        }
    });

    column->addStretch(1);

    auto* note = new AnInfoBanner(tr("Images stay where they are. The project keeps their history, the window "
                                     "layouts and the integration data."),
                                  AnInfoBanner::Tone::Info, this);
    note->setObjectName(QStringLiteral("newProjectNote"));
    column->addWidget(note);

    // What keeps the project from being made, over the buttons.
    impl.message = new AnInfoBanner(QString(), AnInfoBanner::Tone::Warning, this);
    impl.message->setObjectName(QStringLiteral("newProjectMessage"));
    impl.message->hide();
    column->addWidget(impl.message);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(t.spacing.sm);
    buttons->addStretch(1);
    impl.cancel = new QPushButton(tr("Cancel"), this);
    impl.cancel->setObjectName(QStringLiteral("newProjectCancel"));
    impl.cancel->setAutoDefault(false);
    buttons->addWidget(impl.cancel);
    impl.create = new QPushButton(tr("Create project"), this);
    impl.create->setObjectName(QStringLiteral("newProjectCreate"));
    impl.create->setProperty("anButtonVariant", QStringLiteral("primary"));
    impl.create->setDefault(true);
    buttons->addWidget(impl.create);
    column->addLayout(buttons);

    QObject::connect(impl.cancel, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(impl.create, &QPushButton::clicked, this, &QDialog::accept);
    QObject::connect(impl.name, &QLineEdit::textChanged, this, [this]() { refresh(); });
    QObject::connect(impl.location, &PathPickerWidget::pathChanged, this, [this]() { refresh(); });

    resize(kWidth, kHeight);
    impl.name->setFocus();
    refresh();
}

NewProjectDialog::~NewProjectDialog() = default;

NewProjectChoice NewProjectDialog::choice() const
{
    const Impl& impl = *m_impl;
    NewProjectChoice choice;
    choice.name = impl.name->text().trimmed();
    const QString location = impl.location->path().trimmed();
    choice.location = location.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(location));
    choice.folder = projectFolder();
    choice.object = impl.object->text().trimmed();
    const int chosen = impl.compression->currentIndex();
    choice.compression = chosen >= 0 && chosen < static_cast<int>(kCompressions.size())
                             ? kCompressions[static_cast<std::size_t>(chosen)]
                             : impl.options.compression;
    choice.addOpenImages = impl.options.openImages > 0 && impl.addOpenImages->isChecked();
    choice.moveHistory =
        choice.addOpenImages && impl.options.openHistoryBytes.has_value() && impl.moveHistory->isChecked();
    choice.openExisting = impl.offersToOpen;
    return choice;
}

QString NewProjectDialog::projectFolder() const
{
    const QString name = m_impl->name->text().trimmed();
    const QString location = m_impl->location->path().trimmed();
    if (name.isEmpty() || location.isEmpty() || !isValidProjectName(name))
    {
        return {};
    }
    return QDir::cleanPath(QDir(QDir::fromNativeSeparators(location)).filePath(name));
}

QString NewProjectDialog::problem() const
{
    return m_impl->problem;
}

bool NewProjectDialog::offersToOpen() const
{
    return m_impl->offersToOpen;
}

void NewProjectDialog::refresh()
{
    Impl& impl = *m_impl;
    const QString name = impl.name->text().trimmed();
    const QString location = impl.location->path().trimmed();
    const QString folder = projectFolder();
    impl.folder->setFolder(QDir::toNativeSeparators(folder));

    impl.problem.clear();
    impl.offersToOpen = false;
    QString notice;
    bool possible = false;
    if (name.isEmpty() || location.isEmpty())
    {
        // Nothing to say yet: the project has no name, or nowhere to be.
    }
    else if (!isValidProjectName(name))
    {
        impl.problem = tr("A project's name is its folder's: it cannot hold < > : \" / \\ | ? * nor end with a "
                          "dot.");
    }
    else if (!QDir::isAbsolutePath(QDir::fromNativeSeparators(location)))
    {
        impl.problem = tr("The location must be a folder's full path.");
    }
    else
    {
        const NewProjectFolderState state =
            impl.options.folderState ? impl.options.folderState(folder) : NewProjectFolderState::Free;
        switch (state)
        {
        case NewProjectFolderState::Occupied:
            impl.problem = tr("This folder holds files and is not a project. Choose another name or another "
                              "location.");
            break;
        case NewProjectFolderState::Project:
            impl.offersToOpen = true;
            possible = true;
            notice = tr("This folder is a project already. You can open it.");
            break;
        case NewProjectFolderState::Free:
            possible = true;
            break;
        }
    }

    const QString said = impl.problem.isEmpty() ? notice : impl.problem;
    impl.message->setTone(impl.problem.isEmpty() ? AnInfoBanner::Tone::Info : AnInfoBanner::Tone::Warning);
    impl.message->setMessage(said);
    impl.message->setVisible(!said.isEmpty());
    impl.create->setText(impl.offersToOpen ? tr("Open project") : tr("Create project"));
    impl.create->setEnabled(possible);
}

QLineEdit* NewProjectDialog::nameField() const noexcept
{
    return m_impl->name;
}

PathPickerWidget* NewProjectDialog::locationPicker() const noexcept
{
    return m_impl->location;
}

QWidget* NewProjectDialog::folderLine() const noexcept
{
    return m_impl->folder;
}

QLineEdit* NewProjectDialog::objectField() const noexcept
{
    return m_impl->object;
}

AnSegmentedControl* NewProjectDialog::compressionPicker() const noexcept
{
    return m_impl->compression;
}

QCheckBox* NewProjectDialog::addOpenImagesCheck() const noexcept
{
    return m_impl->addOpenImages;
}

QCheckBox* NewProjectDialog::moveHistoryCheck() const noexcept
{
    return m_impl->moveHistory;
}

AnInfoBanner* NewProjectDialog::messageBanner() const noexcept
{
    return m_impl->message;
}

QPushButton* NewProjectDialog::createButton() const noexcept
{
    return m_impl->create;
}

QPushButton* NewProjectDialog::cancelButton() const noexcept
{
    return m_impl->cancel;
}

} // namespace an::ui
