// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/FloatingImagesPage.h"

#include "controls/ImageViewControl.h"
#include "theme/ComponentParts.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace an::ui::observatory
{

FloatingImagesPage::FloatingImagesPage(Dependencies dependencies, QWidget* parent)
    : QWidget(parent), m_dependencies(std::move(dependencies))
{
    setObjectName(QStringLiteral("observatoryFloatingImagesPage"));
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);

    auto* column = new QVBoxLayout();
    column->setSpacing(an::ui::parts::kTopBarGap);
    outer->addLayout(column);
    outer->addStretch(1);

    auto* title = new QLabel(tr("The images of this workspace are floating"), this);
    title->setObjectName(QStringLiteral("floatingImagesTitle"));
    title->setAlignment(Qt::AlignHCenter);
    column->addWidget(title);

    m_rows = new QVBoxLayout();
    m_rows->setSpacing(an::ui::parts::kTopBarGap);
    column->addLayout(m_rows);

    m_dockAll = new QPushButton(tr("Dock all here"), this);
    m_dockAll->setObjectName(QStringLiteral("dockAllHereButton"));
    connect(m_dockAll, &QPushButton::clicked, this, [this]() {
        if (m_dependencies.dockAll)
        {
            m_dependencies.dockAll();
        }
    });
    column->addWidget(m_dockAll, 0, Qt::AlignHCenter);

    auto* hint = new QLabel(tr("or drag a window back here"), this);
    hint->setAlignment(Qt::AlignHCenter);
    column->addWidget(hint);

    m_openFile = new QPushButton(tr("Open file..."), this);
    m_openFile->setObjectName(QStringLiteral("floatingImagesOpenFileButton"));
    connect(m_openFile, &QPushButton::clicked, this, [this]() {
        if (m_dependencies.openFile)
        {
            m_dependencies.openFile();
        }
    });
    column->addWidget(m_openFile, 0, Qt::AlignHCenter);
}

void FloatingImagesPage::setViews(const std::vector<ImageViewControl*>& views)
{
    while (QLayoutItem* item = m_rows->takeAt(0))
    {
        if (QLayout* const row = item->layout())
        {
            while (QLayoutItem* part = row->takeAt(0))
            {
                delete part->widget();
                delete part;
            }
        }
        delete item;
    }
    m_views = views;
    m_dockHere.clear();
    m_names.clear();
    for (ImageViewControl* view : m_views)
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(an::ui::parts::kTopBarGap);

        auto* picture = new QLabel(this);
        picture->setFixedSize(kPictureSide, kPictureSide);
        picture->setAlignment(Qt::AlignCenter);
        // A small picture of what the image's window shows.
        const QPixmap shown = view->grab();
        if (!shown.isNull())
        {
            picture->setPixmap(shown.scaled(kPictureSide, kPictureSide, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        row->addWidget(picture);

        auto* name = new QLabel(QString::fromUtf8(view->getUniqueName()), this);
        name->setToolTip(name->text());
        row->addWidget(name, 1);
        m_names.push_back(name);

        auto* dock = new QPushButton(tr("Dock here"), this);
        connect(dock, &QPushButton::clicked, this, [this, view]() {
            if (m_dependencies.dockHere)
            {
                m_dependencies.dockHere(*view);
            }
        });
        row->addWidget(dock);
        m_dockHere.push_back(dock);
        m_rows->addLayout(row);
    }
}

const std::vector<ImageViewControl*>& FloatingImagesPage::views() const noexcept
{
    return m_views;
}

QPushButton* FloatingImagesPage::dockHereButton(const ImageViewControl* view) const
{
    for (std::size_t index = 0; index < m_views.size(); ++index)
    {
        if (m_views[index] == view)
        {
            return m_dockHere[index];
        }
    }
    return nullptr;
}

QPushButton* FloatingImagesPage::dockAllButton() const noexcept
{
    return m_dockAll;
}

QPushButton* FloatingImagesPage::openFileButton() const noexcept
{
    return m_openFile;
}

QString FloatingImagesPage::rowName(const ImageViewControl* view) const
{
    for (std::size_t index = 0; index < m_views.size(); ++index)
    {
        if (m_views[index] == view)
        {
            return m_names[index]->text();
        }
    }
    return {};
}

} // namespace an::ui::observatory
