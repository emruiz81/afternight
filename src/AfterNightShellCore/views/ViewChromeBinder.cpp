// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "views/ViewChromeBinder.h"

#include "components/AnFloatingToolbar.h"
#include "components/AnHistoryStrip.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"

#include <QAbstractButton>
#include <QMenu>
#include <QObject>
#include <QPoint>
#include <QPointer>

#include <cstdint>
#include <optional>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// Hears one view's listener calls and hands each to the binder.
class ViewFollower final : public ImageViewControlListener
{
public:
    explicit ViewFollower(std::function<void(ImageViewControl*)> changed, std::function<void()> gone)
        : ImageViewControlListener(nullptr), m_changed(std::move(changed)), m_gone(std::move(gone))
    {
    }

    ~ViewFollower() override = default;

    ViewFollower(const ViewFollower&) = delete;
    ViewFollower& operator=(const ViewFollower&) = delete;

    [[nodiscard]] ImageViewControl* view() const
    {
        return m_image_view_control;
    }

    void onImageLoaded(ImageViewControl* view) override
    {
        m_changed(view);
    }

    void onImageUpdated(ImageViewControl* view) override
    {
        m_changed(view);
    }

    void onImageRenamed(ImageViewControl* view) override
    {
        m_changed(view);
    }

    void onImageViewScaled(ImageViewControl* view) override
    {
        m_changed(view);
    }

    void onChromeActionsChanged(ImageViewControl* view) override
    {
        m_changed(view);
    }

    void onImageViewControlDestroyed(ImageViewControl* view) override
    {
        ImageViewControlListener::onImageViewControlDestroyed(view);
        m_gone();
    }

private:
    std::function<void(ImageViewControl*)> m_changed;
    std::function<void()> m_gone;
};

} // namespace

struct ViewChromeBinder::Impl
{
    QPointer<an::ui::AnFloatingToolbar> toolbar;
    QPointer<an::ui::AnHistoryStrip> strip;
    MenuFiller menuFiller;
    QPointer<QMenu> openMenu;
    QMetaObject::Connection moreConnection;
    QMetaObject::Connection stepConnection;
    int viewCount = 1;

    std::unique_ptr<ViewFollower> toolbarFollower;
    std::unique_ptr<ViewFollower> historyFollower;
    /// The steps listener of the history the strip shows, and the history it was added to.
    std::optional<std::uint64_t> stepsToken;
    an::ui::IViewHistory* listenedHistory = nullptr;

    /// Shows the toolbar's view as it is now: its actions and choices, its name, its zoom.
    void showToolbarView()
    {
        if (!toolbar)
        {
            return;
        }
        ImageViewControl* view = toolbarFollower->view();
        if (view == nullptr)
        {
            toolbar->setActions({});
            toolbar->setViewName({});
            toolbar->setZoom(std::nullopt);
            return;
        }
        toolbar->setActions(view->chromeActions());
        toolbar->setViewName(QString::fromStdString(view->getUniqueName()));
        toolbar->setViewCount(viewCount);
        toolbar->setZoom(view->hasImage() ? std::optional<double>(view->getCurrentZoom()) : std::nullopt);
    }

    /// Shows the strip's view's history as it is now: its steps and the history's buttons.
    void showHistoryView()
    {
        if (!strip)
        {
            return;
        }
        ImageViewControl* view = historyFollower->view();
        an::ui::IViewHistory* history = view != nullptr ? view->viewHistory() : nullptr;
        if (view == nullptr)
        {
            strip->setActions(nullptr, nullptr, nullptr);
            strip->setSteps({});
            return;
        }
        const an::ui::ViewChromeActions& actions = view->chromeActions();
        strip->setActions(actions.undo, actions.redo, actions.history);
        strip->setSteps(history != nullptr ? history->steps() : an::ui::ViewHistorySteps{});
    }

    /// Stops listening to the steps of the history it listened to.
    void forgetHistory()
    {
        if (stepsToken.has_value() && listenedHistory != nullptr)
        {
            listenedHistory->removeStepsListener(*stepsToken);
        }
        stepsToken.reset();
        listenedHistory = nullptr;
    }

    void listenToHistory()
    {
        forgetHistory();
        ImageViewControl* view = historyFollower->view();
        an::ui::IViewHistory* history = view != nullptr ? view->viewHistory() : nullptr;
        if (history == nullptr)
        {
            return;
        }
        listenedHistory = history;
        stepsToken = history->addStepsListener([this]() { showHistoryView(); });
    }

    /// Opens the view's menu over the More button: the toolbar lies over the bottom of an image, so it opens upwards.
    void openViewMenu()
    {
        ImageViewControl* view = toolbarFollower->view();
        if (!toolbar || view == nullptr || !menuFiller)
        {
            return;
        }
        auto* menu = new QMenu(toolbar);
        menu->setAttribute(Qt::WA_DeleteOnClose, true);
        menuFiller(*menu, *view);
        if (menu->isEmpty())
        {
            delete menu;
            return;
        }
        QAbstractButton* more = toolbar->moreButton();
        const QPoint origin = more->mapToGlobal(QPoint(0, 0));
        openMenu = menu;
        menu->popup(QPoint(origin.x(), origin.y() - menu->sizeHint().height()));
    }
};

ViewChromeBinder::ViewChromeBinder(an::ui::AnFloatingToolbar* toolbar, an::ui::AnHistoryStrip* strip)
    : m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    d.toolbar = toolbar;
    d.strip = strip;
    d.toolbarFollower = std::make_unique<ViewFollower>([this](ImageViewControl*) { m_impl->showToolbarView(); },
                                                       [this]() {
                                                           // Told while the view is being destroyed: it is followed no
                                                           // more, and shown no more.
                                                           m_impl->toolbarFollower->setViewControl(nullptr);
                                                           m_impl->showToolbarView();
                                                       });
    d.historyFollower = std::make_unique<ViewFollower>([this](ImageViewControl*) { m_impl->showHistoryView(); },
                                                       [this]() {
                                                           // The history goes after its view's listeners are told: its
                                                           // listener is taken away while it is there.
                                                           m_impl->forgetHistory();
                                                           m_impl->historyFollower->setViewControl(nullptr);
                                                           m_impl->showHistoryView();
                                                       });
    if (toolbar != nullptr)
    {
        d.moreConnection = QObject::connect(toolbar, &an::ui::AnFloatingToolbar::moreRequested, toolbar,
                                            [this]() { m_impl->openViewMenu(); });
    }
    if (strip != nullptr)
    {
        d.stepConnection = QObject::connect(strip, &an::ui::AnHistoryStrip::stepClicked, strip, [this](int index) {
            ImageViewControl* view = m_impl->historyFollower->view();
            an::ui::IViewHistory* history = view != nullptr ? view->viewHistory() : nullptr;
            if (history != nullptr && index >= 0)
            {
                (void)history->jumpToStep(static_cast<std::size_t>(index));
            }
        });
    }
}

ViewChromeBinder::~ViewChromeBinder()
{
    QObject::disconnect(m_impl->moreConnection);
    QObject::disconnect(m_impl->stepConnection);
    m_impl->forgetHistory();
    m_impl->toolbarFollower->setViewControl(nullptr);
    m_impl->historyFollower->setViewControl(nullptr);
    if (m_impl->openMenu)
    {
        m_impl->openMenu->close();
    }
}

void ViewChromeBinder::bind(ImageViewControl* view)
{
    bindToolbar(view);
    bindHistory(view);
}

void ViewChromeBinder::bindToolbar(ImageViewControl* view)
{
    if (m_impl->toolbarFollower->view() != view)
    {
        if (m_impl->openMenu)
        {
            // The menu acts on the view it was opened for: it goes with it.
            m_impl->openMenu->close();
        }
        m_impl->toolbarFollower->setViewControl(view);
    }
    m_impl->showToolbarView();
}

void ViewChromeBinder::bindHistory(ImageViewControl* view)
{
    if (m_impl->historyFollower->view() != view)
    {
        m_impl->historyFollower->setViewControl(view);
        m_impl->listenToHistory();
    }
    m_impl->showHistoryView();
}

ImageViewControl* ViewChromeBinder::toolbarView() const
{
    return m_impl->toolbarFollower->view();
}

ImageViewControl* ViewChromeBinder::historyView() const
{
    return m_impl->historyFollower->view();
}

void ViewChromeBinder::setViewCount(const int count)
{
    m_impl->viewCount = count;
    if (m_impl->toolbar)
    {
        m_impl->toolbar->setViewCount(count);
    }
}

void ViewChromeBinder::setMenuFiller(MenuFiller filler)
{
    m_impl->menuFiller = std::move(filler);
}

QMenu* ViewChromeBinder::openMenu() const
{
    return m_impl->openMenu.data();
}

} // namespace an::ui::shell
