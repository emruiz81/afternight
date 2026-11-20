// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "actions/ProcessListDialogs.h"

#include "core/ApplicationCore.h"
#include "services/FavoriteProcessesModel.h"
#include "services/RecentProcessesModel.h"

#include "app/MessageBoxes.h"
#include "app/ProcessMenuState.h"
#include "app/ProcessRegistry.h"
#include "app/WindowPlacement.h"
#include "components/AnWindowHeading.h"
#include "theme/AnDesignTokens.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace an::ui::shell
{

namespace
{

constexpr char kContext[] = "an::ui::shell::ProcessListDialogs";

[[nodiscard]] QString text(const char* source)
{
    return QCoreApplication::translate(kContext, source);
}

/// Asks a question with Yes and No, Yes the default; whether Yes was the answer.
[[nodiscard]] bool askYes(const QString& question, const QString& detail)
{
    const an::ui::ScopedTopLevelFlagRestorer restorer;
    QMessageBox box;
    box.setText(question);
    box.setInformativeText(detail);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);
    return box.exec() == QMessageBox::Yes;
}

} // namespace

bool clearRecentProcesses(ApplicationCore& core)
{
    RecentProcessesModel& recent = core.recentProcesses();
    recent.load();
    if (recent.entries().empty())
    {
        recent.save();
        return true;
    }
    if (!askYes(text("Are you sure you want to delete the recent used processes?"),
                text("The history of used processes will be lost")))
    {
        return false;
    }
    recent.clear();
    return true;
}

bool manageFavoriteProcesses(ApplicationCore& core, QWidget* parent)
{
    FavoriteProcessesModel& favourites = core.favoriteProcesses();
    favourites.load();

    std::vector<std::pair<QString, std::string>> processes;
    const auto& descriptors = an::ui::process_registry::ProcessRegistry::instance().all();
    processes.reserve(descriptors.size());
    for (const auto& descriptor : descriptors)
    {
        if (!descriptor.id.empty())
        {
            processes.emplace_back(an::ui::processDisplayNameFromDescriptor(&descriptor, descriptor.id), descriptor.id);
        }
    }
    std::sort(processes.begin(), processes.end(), [](const auto& lhs, const auto& rhs) {
        return QString::compare(lhs.first, rhs.first, Qt::CaseInsensitive) < 0;
    });

    const an::ui::ScopedTopLevelFlagRestorer restorer;
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("manageFavoriteProcessesDialog"));
    dialog.setWindowTitle(text("Manage Favorite Processes"));
    // A list a score of rows high, as wide as the names it lists read at.
    const an::ui::MetricTokens& metrics = an::ui::tokens().metrics;
    dialog.resize(metrics.rowTable * 18, metrics.rowTable * 21);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(metrics.pad, metrics.pad, metrics.pad, metrics.pad);
    layout->setSpacing(metrics.gap);

    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText(text("Search processes..."));
    layout->addWidget(search);

    auto* list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(list);
    for (const auto& [displayName, processId] : processes)
    {
        auto* item = new QListWidgetItem(displayName, list);
        item->setData(Qt::UserRole, QString::fromUtf8(processId));
        item->setToolTip(QString::fromUtf8(processId));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(favourites.ids().contains(processId) ? Qt::Checked : Qt::Unchecked);
    }

    QObject::connect(search, &QLineEdit::textChanged, &dialog, [list](const QString& typed) {
        const QString wanted = typed.trimmed();
        for (int row = 0; row < list->count(); ++row)
        {
            QListWidgetItem* const item = list->item(row);
            if (item != nullptr)
            {
                item->setHidden(!wanted.isEmpty() && !item->text().contains(wanted, Qt::CaseInsensitive) &&
                                !item->toolTip().contains(wanted, Qt::CaseInsensitive));
            }
        }
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    an::ui::headWindow(dialog, text("Manage Favorite Processes"),
                       text("The processes ticked are listed first: in the Favorite Processes menu and in the command "
                            "palette."),
                       text("Processes"));
    an::ui::holdWithinAvailableAreaWhenFirstShown(dialog);

    if (dialog.exec() != QDialog::Accepted)
    {
        return false;
    }

    std::set<std::string> chosen;
    for (int row = 0; row < list->count(); ++row)
    {
        QListWidgetItem* const item = list->item(row);
        if (item != nullptr && item->checkState() == Qt::Checked)
        {
            const QString processId = item->data(Qt::UserRole).toString();
            if (!processId.isEmpty())
            {
                chosen.insert(processId.toUtf8().toStdString());
            }
        }
    }
    favourites.setIds(std::move(chosen));
    return true;
}

bool clearFavoriteProcesses(ApplicationCore& core)
{
    FavoriteProcessesModel& favourites = core.favoriteProcesses();
    favourites.load();
    if (favourites.ids().empty())
    {
        return true;
    }
    if (!askYes(text("Are you sure you want to delete the favorite processes list?"),
                text("The list of favorite processes will be lost")))
    {
        return false;
    }
    favourites.clear();
    return true;
}

} // namespace an::ui::shell
