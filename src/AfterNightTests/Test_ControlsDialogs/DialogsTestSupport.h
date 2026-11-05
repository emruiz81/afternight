// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the dialogs suite's cases share: the viewer's application context over a session that reads every container
/// the product does, with a view factory and a RAW policy service that records what the settings publish; images and
/// documents carrying stated cards; and the files a case writes into a directory of its own.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/FilePolicies.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/PersistenceDbExecutor.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::dialogs
{

using an::tests::viewer::Channels;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeAsset;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;
using an::tests::viewer::settle;
using an::tests::viewer::ViewHandle;

/// A RAW policy service as the application's persistence coordinator is one: it builds a policy from the settings it
/// is handed and publishes it, and it records every publication.
class RecordingRawPolicies final : public an::ui::IRawOpenPolicyService
{
public:
    RecordingRawPolicies()
    {
        an::Result<std::shared_ptr<const an::ui::RawOpenPolicy>> policy =
            an::ui::RawOpenPolicy::create(an::ui::RawOpenSettings{});
        EXPECT_TRUE(policy.hasValue()) << policy.status().message();
        if (policy.hasValue())
        {
            m_published = *policy;
        }
    }

    [[nodiscard]] std::shared_ptr<const an::ui::RawOpenPolicy> rawOpenPolicy() const override
    {
        return m_published;
    }

    an::Status setRawOpenSettings(const an::ui::RawOpenSettings& settings) override
    {
        published.push_back(settings);
        an::Result<std::shared_ptr<const an::ui::RawOpenPolicy>> policy = an::ui::RawOpenPolicy::create(settings);
        if (!policy.hasValue())
        {
            return policy.status();
        }
        m_published = *policy;
        return an::Status::success();
    }

    /// Every set of settings published, in order.
    std::vector<an::ui::RawOpenSettings> published;

private:
    std::shared_ptr<const an::ui::RawOpenPolicy> m_published;
};

/// The viewer's context over a session that reads every container the product does, with a view factory whose views
/// it keeps and a RAW policy service it lends.
class DialogsTestContext final : public an::tests::viewer::ViewerTestContext, public an::ui::IViewFactory
{
public:
    explicit DialogsTestContext(an::consumer::EngineSession session) : ViewerTestContext(std::move(session))
    {
        rawOpenPolicyService = &rawPolicies;
    }

    ~DialogsTestContext() override
    {
        // The views it made go before the session the base lets go.
        m_made.clear();
    }

    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string&, ViewControlMode mode,
                                                                       ImageViewControl*, bool, bool) override
    {
        auto view = makeView(document.names().displayName.toStdString(), mode);
        view->setDocument(std::move(document));
        m_made.push_back(std::move(view));
        return m_made.back().get();
    }

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&&,
                                                                          QWidget*, bool, const std::string&,
                                                                          ViewControlMode, ImageViewControl*, bool,
                                                                          bool) override
    {
        return nullptr;
    }

    /// Records whether a view's global action is enabled, as the application's action state does.
    void setCustomImageActionEnabled(ImageViewControl*, GlobalViewControlActions action, bool enable) override
    {
        actionsEnabled[action] = enable;
    }

    /// The RAW policies the settings publish and the views read under.
    RecordingRawPolicies rawPolicies;

    /// Whether each global action a view stated was last enabled.
    std::map<GlobalViewControlActions, bool> actionsEnabled;

private:
    std::vector<ViewHandle> m_made;
};

/// A dialogs context over a session of a given width whose context reads every container the product does; fails the
/// case when the session cannot be opened.
[[nodiscard]] inline std::unique_ptr<DialogsTestContext> openDialogsContext(const std::uint32_t width = 2u)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    settings.codecRegistry = []() { return an::ui::createProductCodecRegistry(); };
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return nullptr;
    }
    return std::make_unique<DialogsTestContext>(std::move(*session));
}

/// A card of a namespace; fails the case when it cannot be made.
[[nodiscard]] inline an::CodecCard card(const an::CodecNamespace origin, std::string keyword, std::string value,
                                        std::string comment = {})
{
    an::Result<an::CodecCard> made =
        an::CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    if (!made.hasValue())
    {
        return *an::CodecCard::create(an::CodecNamespace::Unspecified, "INVALID", {}, {});
    }
    return std::move(*made);
}

/// Codec metadata holding cards; fails the case when it cannot be made.
[[nodiscard]] inline an::CodecMetadata codecOf(std::vector<an::CodecCard> cards)
{
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    return codec.hasValue() ? std::move(*codec) : an::CodecMetadata{};
}

/// Metadata carrying cards, and stating nothing else.
[[nodiscard]] inline an::ImageMetadata metadataWithCards(std::vector<an::CodecCard> cards)
{
    return an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                     an::Provenance{}, codecOf(std::move(cards)), std::nullopt);
}

/// A grey Float32 document of a session carrying cards, under a name and a file; fails the case when it cannot be
/// built.
[[nodiscard]] inline std::optional<an::ui::ImageDocument>
makeCardDocument(const an::consumer::EngineSession& session, std::vector<an::CodecCard> cards,
                 an::ui::DocumentNames names = {}, const std::uint32_t width = 16u, const std::uint32_t height = 12u)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::Float32, width, height, Channels::Grey);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, metadataWithCards(std::move(cards)));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset), std::move(names));
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// A directory of a case's own, removed with it; fails the case when it cannot be made.
class CaseDirectory
{
public:
    CaseDirectory()
    {
        EXPECT_TRUE(m_directory.isValid());
    }

    /// A path inside the directory.
    [[nodiscard]] QString path(const QString& name) const
    {
        return m_directory.filePath(name);
    }

private:
    QTemporaryDir m_directory;
};

/// The settings database of a case's own, as the settings pages are handed it: a SQLite file in a directory removed
/// with the case.
class SettingsStore
{
public:
    SettingsStore()
    {
        EXPECT_TRUE(m_directory.isValid());
        open();
    }

    /// A store over a writable copy of a settings database, so nothing the case does reaches the database copied.
    explicit SettingsStore(const QString& copyOf)
    {
        EXPECT_TRUE(m_directory.isValid());
        const QString path = m_directory.filePath(QStringLiteral("settings.db"));
        EXPECT_TRUE(QFile::copy(copyOf, path)) << copyOf.toStdString();
        EXPECT_TRUE(
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser));
        open();
    }

    ~SettingsStore()
    {
        m_settings.reset();
        m_executor.reset();
    }

    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    /// The store.
    [[nodiscard]] GlobalSettingsPersistence* persistence() const
    {
        return m_settings.get();
    }

    /// A stored value, or nothing.
    [[nodiscard]] std::optional<QString> stored(const char* key) const
    {
        return m_settings->selectOptional(QString::fromLatin1(key));
    }

    /// Stores a value.
    void store(const char* key, const QString& value) const
    {
        EXPECT_TRUE(m_settings->upsert(QString::fromLatin1(key), value));
    }

    /// Copies the store's database, as it holds what was stored, to a path that holds no file.
    [[nodiscard]] bool copyTo(const QString& path) const
    {
        return QFile::copy(m_directory.filePath(QStringLiteral("settings.db")), path);
    }

    /// The keys stored under a prefix.
    [[nodiscard]] QStringList keysUnder(const QString& prefix) const
    {
        return m_settings->selectKeysWithPrefix(prefix);
    }

    /// A reader over the store, as the product's readers take one.
    [[nodiscard]] an::ui::SettingReader reader() const
    {
        GlobalSettingsPersistence* store = m_settings.get();
        return [store](const QString& key, const QString& fallback) { return store->select(key, fallback); };
    }

private:
    void open()
    {
        static std::atomic<int> sequence{0};
        m_executor =
            std::make_unique<PersistenceDbExecutor>(m_directory.filePath(QStringLiteral("settings.db")),
                                                    QStringLiteral("dialogs_settings_%1").arg(sequence.fetch_add(1)));
        m_settings = std::make_unique<GlobalSettingsPersistence>(*m_executor);
        EXPECT_TRUE(m_settings->initializeTable());
    }

    QTemporaryDir m_directory;
    std::unique_ptr<PersistenceDbExecutor> m_executor;
    std::unique_ptr<GlobalSettingsPersistence> m_settings;
};

} // namespace an::tests::dialogs
