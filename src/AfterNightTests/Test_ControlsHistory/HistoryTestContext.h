// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The application context the history's view cases run under: the viewer's, with the view factory a clone makes its
/// view through, and a temporary directory of the case's own that the views' histories make their directories in.
#pragma once

#include "Test_ControlsViewer/ViewerTestContext.h"
#include "Test_ImageViewControl/history_test_support.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"

#include <QString>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::history
{

/// The viewer's context, with a view factory and a temporary directory.
class HistoryTestContext final : public an::tests::viewer::ViewerTestContext,
                                 public an::ui::IViewFactory,
                                 public an::ui::ITempFileService
{
public:
    explicit HistoryTestContext(an::consumer::EngineSession session) : ViewerTestContext(std::move(session)) {}

    ~HistoryTestContext() override
    {
        // The views it made go before the session the base lets go.
        m_made.clear();
    }

    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }

    [[nodiscard]] an::ui::ITempFileService* getTempFileService() override
    {
        return this;
    }

    // ---- IViewFactory: the views a clone makes, kept and deleted with the context.

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string& historyDesc,
                                                                       ViewControlMode mode, ImageViewControl*, bool,
                                                                       bool) override
    {
        auto view = an::tests::viewer::makeView(document.names().displayName.toStdString(), mode);
        view->setDocument(std::move(document));
        view->initializeNewImageHistory(historyDesc);
        madeHistories.push_back(historyDesc);
        m_made.push_back(std::move(view));
        return m_made.back().get();
    }

    [[nodiscard]] ImageViewControl*
    createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&& document, QWidget*,
                                          bool autoDebayer, const std::string& historyDesc, ViewControlMode mode,
                                          ImageViewControl*, bool, bool) override
    {
        if (!document.sequence().has_value())
        {
            return nullptr;
        }
        auto view = an::tests::viewer::makeView(document.names().displayName.toStdString(), mode);
        view->setSequenceDocument(std::move(document), autoDebayer);
        view->initializeNewImageHistory(historyDesc);
        madeHistories.push_back(historyDesc);
        m_made.push_back(std::move(view));
        return m_made.back().get();
    }

    // ---- ITempFileService: a directory of the case's own.

    [[nodiscard]] bool isTempFile(const std::string& fileName) override
    {
        return QString::fromStdString(fileName).startsWith(m_temporary.path());
    }

    [[nodiscard]] std::string createTempFilePathForNewSequence(an::SequenceFormat format) const override
    {
        const int index = m_nextTemporary.fetch_add(1);
        const char* extension = format == an::SequenceFormat::Ser ? "ser" : "anseq";
        return QStringLiteral("%1/sequence-%2.%3")
            .arg(m_temporary.path())
            .arg(index)
            .arg(QString::fromLatin1(extension))
            .toStdString();
    }

    [[nodiscard]] QString getTempDirPath() const override
    {
        return m_temporary.path();
    }

    /// The views the factory made, in order.
    [[nodiscard]] std::vector<ImageViewControl*> madeViews() const
    {
        std::vector<ImageViewControl*> views;
        for (const auto& view : m_made)
        {
            views.push_back(view.get());
        }
        return views;
    }

    /// The history descriptions the factory's views were made with.
    std::vector<std::string> madeHistories;

private:
    QTemporaryDir m_temporary;
    mutable std::atomic<int> m_nextTemporary{0};
    std::vector<an::tests::viewer::ViewHandle> m_made;
};

/// A history context over a session of a given width with the allowances the case states; fails the case when the
/// session cannot be opened.
[[nodiscard]] inline std::unique_ptr<HistoryTestContext>
openHistoryContext(const an::consumer::MemoryAllowances& memory = {}, const std::uint64_t engineBudget = 0u)
{
    std::optional<TestSessionProvider> provider = openHistorySession(memory, engineBudget);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<HistoryTestContext>(std::move(provider->owned()));
}

} // namespace an::tests::history
