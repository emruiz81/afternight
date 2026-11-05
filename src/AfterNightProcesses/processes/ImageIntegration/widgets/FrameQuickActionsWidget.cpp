// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameQuickActionsWidget.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "theme/AnDesignTokens.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QUndoStack>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::FrameQuickActionsWidget)

namespace ImageIntegration
{

class FrameQuickActionsWidget::Impl
{
public:
    FrameQuickActionsWidget* q;
    IntegrationDataModel* model;
    QUndoStack* undoStack = nullptr;
    FrameType frameType;

    QPushButton* btnIncludeAll = nullptr;
    QPushButton* btnExcludeAll = nullptr;
    QPushButton* btnClearTags = nullptr;

    explicit Impl(FrameQuickActionsWidget* parent, FrameType type, IntegrationDataModel* m, QUndoStack* undo)
        : q(parent), model(m), undoStack(undo), frameType(type)
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* title = new QLabel(QObject::tr("Quick Actions"), q);
        title->setObjectName(QStringLiteral("lblFrameQuickActionsTitle"));
        title->setFont(an::ui::tokens().typography.heading3);
        layout->addWidget(title);

        // Grid layout for action buttons
        auto* grid = new QGridLayout();
        grid->setSpacing(4);

        btnIncludeAll = new QPushButton(QObject::tr("Include All"), q);
        btnIncludeAll->setObjectName(QStringLiteral("btnIncludeAll"));
        btnIncludeAll->setToolTip(QObject::tr("Mark all frames as included in processing."));
        grid->addWidget(btnIncludeAll, 0, 0);

        btnExcludeAll = new QPushButton(QObject::tr("Exclude All"), q);
        btnExcludeAll->setObjectName(QStringLiteral("btnExcludeAll"));
        btnExcludeAll->setToolTip(QObject::tr("Mark all frames as excluded from processing."));
        grid->addWidget(btnExcludeAll, 0, 1);

        btnClearTags = new QPushButton(QObject::tr("Clear Tags"), q);
        btnClearTags->setObjectName(QStringLiteral("btnClearTags"));
        btnClearTags->setToolTip(QObject::tr("Remove user-defined custom tags from all frames of this type."));
        grid->addWidget(btnClearTags, 0, 2);

        layout->addLayout(grid);
        layout->addStretch(1);

        // Connect buttons
        QObject::connect(btnIncludeAll, &QPushButton::clicked, q, [this]() {
            if (!model)
            {
                return;
            }

            const auto& collection = model->frames(frameType);
            QStringList paths;
            for (const auto& frame : collection.frames())
            {
                if (frame.excluded)
                {
                    paths << frame.id.canonicalPath();
                }
            }

            if (!paths.isEmpty())
            {
                std::vector<FrameAttributeChange> changes;
                changes.reserve(paths.size());
                for (const auto& path : paths)
                {
                    changes.push_back(FrameAttributeChange{path, true, false});
                }
                if (undoStack)
                {
                    undoStack->push(new SetFrameAttributesCommand(model, FrameAttribute::Excluded, std::move(changes)));
                }
                else
                {
                    model->beginBatchUpdate();
                    for (const auto& path : paths)
                    {
                        model->updateFrameAttribute(path, FrameAttribute::Excluded, false);
                    }
                    model->endBatchUpdate();
                }
            }
        });

        QObject::connect(btnExcludeAll, &QPushButton::clicked, q, [this]() {
            if (!model)
            {
                return;
            }

            const auto& collection = model->frames(frameType);
            QStringList paths;
            for (const auto& frame : collection.frames())
            {
                if (!frame.excluded)
                {
                    paths << frame.id.canonicalPath();
                }
            }

            if (!paths.isEmpty())
            {
                std::vector<FrameAttributeChange> changes;
                changes.reserve(paths.size());
                for (const auto& path : paths)
                {
                    changes.push_back(FrameAttributeChange{path, false, true});
                }
                if (undoStack)
                {
                    undoStack->push(new SetFrameAttributesCommand(model, FrameAttribute::Excluded, std::move(changes)));
                }
                else
                {
                    model->beginBatchUpdate();
                    for (const auto& path : paths)
                    {
                        model->updateFrameAttribute(path, FrameAttribute::Excluded, true);
                    }
                    model->endBatchUpdate();
                }
            }
        });

        QObject::connect(btnClearTags, &QPushButton::clicked, q, [this]() {
            if (!model)
            {
                return;
            }

            // Asked without a nested event loop: the tags are cleared once the person confirms.
            auto* box = new QMessageBox(QMessageBox::Warning, QObject::tr("Clear Tags"),
                                        QObject::tr("This will reset all user defined tags from the frames."),
                                        QMessageBox::Yes | QMessageBox::No, q);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setDefaultButton(QMessageBox::No);
            QObject::connect(box, &QDialog::finished, q, [this, box](const int result) {
                QMessageBox::StandardButton answer = box->standardButton(box->clickedButton());
                if (answer == QMessageBox::NoButton)
                {
                    answer = static_cast<QMessageBox::StandardButton>(result);
                }
                if (answer != QMessageBox::Yes || !model)
                {
                    return;
                }

                const auto& collection = model->frames(frameType);
                QStringList paths;
                for (const auto& frame : collection.frames())
                {
                    if (!frame.customTag.isEmpty())
                    {
                        paths << frame.id.canonicalPath();
                    }
                }

                if (!paths.isEmpty())
                {
                    std::vector<FrameAttributeChange> changes;
                    changes.reserve(paths.size());
                    for (const auto& frame : collection.frames())
                    {
                        if (!frame.customTag.isEmpty())
                        {
                            changes.push_back(
                                FrameAttributeChange{frame.id.canonicalPath(), frame.customTag, QString()});
                        }
                    }
                    if (changes.empty())
                    {
                        return;
                    }

                    if (undoStack)
                    {
                        undoStack->push(
                            new SetFrameAttributesCommand(model, FrameAttribute::CustomTag, std::move(changes)));
                    }
                    else
                    {
                        model->beginBatchUpdate();
                        for (const auto& path : paths)
                        {
                            model->updateFrameAttribute(path, FrameAttribute::CustomTag, QString());
                        }
                        model->endBatchUpdate();
                    }
                }
            });
            box->open();
        });

        q->setObjectName(QStringLiteral("frameQuickActionsWidget"));
    }

    void updateButtonStates()
    {
        if (!model)
        {
            btnIncludeAll->setEnabled(false);
            btnExcludeAll->setEnabled(false);
            btnClearTags->setEnabled(false);
            return;
        }

        const auto& collection = model->frames(frameType);
        const bool hasFrames = !collection.empty();

        bool anyExcluded = false;
        bool anyIncluded = false;
        bool anyTagged = false;

        for (const auto& frame : collection.frames())
        {
            if (frame.excluded)
            {
                anyExcluded = true;
            }
            else
            {
                anyIncluded = true;
            }
            if (!frame.customTag.isEmpty())
            {
                anyTagged = true;
            }
        }

        btnIncludeAll->setEnabled(anyExcluded);
        btnExcludeAll->setEnabled(anyIncluded);
        btnClearTags->setEnabled(anyTagged);
    }
};

FrameQuickActionsWidget::FrameQuickActionsWidget(FrameType frameType, IntegrationDataModel* model, QWidget* parent,
                                                 QUndoStack* undoStack)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, frameType, model, undoStack))
{
}

FrameQuickActionsWidget::~FrameQuickActionsWidget() = default;

void FrameQuickActionsWidget::setFrameType(FrameType type)
{
    m_impl->frameType = type;
    refresh();
}

FrameType FrameQuickActionsWidget::frameType() const
{
    return m_impl->frameType;
}

void FrameQuickActionsWidget::refresh()
{
    m_impl->updateButtonStates();
}

} // namespace ImageIntegration
