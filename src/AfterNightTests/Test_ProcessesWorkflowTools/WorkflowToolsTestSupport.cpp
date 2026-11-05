// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The workflow tools' cases' frames on disk, read back as the engine reads them, and the dialogs a case leaves shown
/// closed when it ends.

#include "Test_ProcessesWorkflowTools/WorkflowToolsTestSupport.h"

#include "controls/ImageViewFiles.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>

#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>

namespace an::tests::processes::workflow
{

namespace
{

/// The largest value a representation holds and its smallest, for fractions of full scale.
template <typename SampleT> [[nodiscard]] constexpr double fullScaleOf() noexcept
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return 1.0;
    }
    else
    {
        return static_cast<double>((std::numeric_limits<SampleT>::max)()) -
               static_cast<double>((std::numeric_limits<SampleT>::min)());
    }
}

template <typename SampleT> [[nodiscard]] SampleT fromFraction(const double fraction) noexcept
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<SampleT>(fraction);
    }
    else
    {
        const double code = std::round(std::clamp(fraction, 0.0, 1.0) * fullScaleOf<SampleT>() +
                                       static_cast<double>((std::numeric_limits<SampleT>::min)()));
        return static_cast<SampleT>(code);
    }
}

template <typename SampleT> [[nodiscard]] double toFraction(const SampleT value) noexcept
{
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<double>(value);
    }
    else
    {
        return (static_cast<double>(value) - static_cast<double>((std::numeric_limits<SampleT>::min)())) /
               fullScaleOf<SampleT>();
    }
}

/// A card's number as text, in the C locale: std::to_string writes the process locale's decimal separator, and the
/// engine's card reader refuses "300,000000".
[[nodiscard]] std::string cardNumber(const double value)
{
    char buffer[32];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return error == std::errc() ? std::string(buffer, end) : std::string();
}

} // namespace

std::optional<std::string> ReadBack::card(const std::string& keyword) const
{
    for (const auto& [name, value] : cards)
    {
        if (name == keyword)
        {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<double> ReadBack::number(const std::string& keyword) const
{
    const std::optional<std::string> text = card(keyword);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    std::size_t first = text->find_first_not_of(' ');
    if (first == std::string::npos)
    {
        return std::nullopt;
    }
    double value = 0.0;
    const char* const begin = text->data() + first + (text->at(first) == '+' ? 1u : 0u);
    const char* const end = text->data() + text->size();
    const auto [stopped, error] = std::from_chars(begin, end, value);
    if (error != std::errc() || std::find_if(stopped, end, [](const char c) { return c != ' '; }) != end)
    {
        return std::nullopt;
    }
    return value;
}

bool writeFrame(const an::consumer::EngineSession& session, const QString& path, const std::uint32_t width,
                const std::uint32_t height, const std::uint32_t channels, const an::SampleType type,
                const std::function<double(std::uint32_t, std::uint32_t, std::uint32_t)>& sample,
                const an::ImageMetadata& metadata, const std::optional<an::CfaDescriptor>& cfa)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        ADD_FAILURE() << hold.status().message();
        return false;
    }
    std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        hold->engine(), type, width, height,
        channels == 3u ? an::tests::viewer::Channels::Rgb : an::tests::viewer::Channels::Grey);
    if (descriptor.has_value() && cfa.has_value())
    {
        an::ImageDescriptorRequest request = descriptor->toRequest();
        request.cfa = *cfa;
        descriptor = an::ImageDescriptor::create(request);
    }
    if (!descriptor.has_value())
    {
        ADD_FAILURE() << "no description";
        return false;
    }
    std::optional<an::consumer::AnyImage> made;
    const bool dispatched = an::dispatchSampleType(type, [&]<typename SampleT>() {
        an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
            hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
        if (!image.hasValue())
        {
            ADD_FAILURE() << image.status().message();
            return;
        }
        {
            std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
            for (std::uint32_t c = 0u; c < channels; ++c)
            {
                for (std::uint32_t y = 0u; y < height; ++y)
                {
                    const auto row = access->view().row(c, y);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        (*row)[x] = fromFraction<SampleT>(sample(x, y, c));
                    }
                }
            }
        }
        made.emplace(std::move(*image));
    });
    if (!dispatched || !made.has_value())
    {
        return false;
    }
    an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(session, std::move(*made));
    if (!asset.hasValue())
    {
        ADD_FAILURE() << asset.status().message();
        return false;
    }
    const an::Result<an::OutputIntent> intent = an::OutputIntent::create(an::OutputIntentRequest{.sampleType = type});
    if (!intent.hasValue())
    {
        ADD_FAILURE() << intent.status().message();
        return false;
    }
    const an::Status written = an::ui::writeViewImage(session, *asset, path, *intent, {});
    if (written.failed())
    {
        ADD_FAILURE() << written.message();
        return false;
    }
    return true;
}

bool writeFlatFrame(const an::consumer::EngineSession& session, const QString& path, const std::uint32_t width,
                    const std::uint32_t height, const std::uint32_t channels, const double fraction,
                    const an::SampleType type, const std::optional<double> exposureSeconds, const QString& filter)
{
    std::vector<an::CodecCard> cards;
    if (exposureSeconds.has_value())
    {
        an::Result<an::CodecCard> card =
            an::CodecCard::create(an::CodecNamespace::Fits, "EXPTIME", cardNumber(*exposureSeconds), "");
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }
    if (!filter.isEmpty())
    {
        an::Result<an::CodecCard> card =
            an::CodecCard::create(an::CodecNamespace::Fits, "FILTER", "'" + filter.toStdString() + "'", "");
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, std::move(cards));
    if (!codec.hasValue())
    {
        ADD_FAILURE() << codec.status().message();
        return false;
    }
    const an::ImageMetadata metadata =
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, std::move(*codec), std::nullopt);
    return writeFrame(
        session, path, width, height, channels, type,
        [fraction](std::uint32_t, std::uint32_t, std::uint32_t) { return fraction; }, metadata);
}

bool writeShiftedField(const an::consumer::EngineSession& session, const QString& path, const StarField& field,
                       const double dx, const double dy, const bool solved)
{
    StarField moved = field;
    for (PlantedStar& star : moved.stars)
    {
        star.x += dx;
        star.y += dy;
    }
    // The field is rendered again with the stars where they now are: a round Gaussian over the sky.
    std::fill(moved.samples.begin(), moved.samples.end(), static_cast<float>(moved.background));
    for (std::uint32_t c = 0u; c < moved.channels; ++c)
    {
        for (const PlantedStar& star : moved.stars)
        {
            const int radius = static_cast<int>(std::ceil(star.sigma * 5.0));
            const int cx = static_cast<int>(std::floor(star.x));
            const int cy = static_cast<int>(std::floor(star.y));
            for (int y = cy - radius; y <= cy + radius; ++y)
            {
                for (int x = cx - radius; x <= cx + radius; ++x)
                {
                    if (x < 0 || y < 0 || x >= static_cast<int>(moved.width) || y >= static_cast<int>(moved.height))
                    {
                        continue;
                    }
                    const double ddx = (static_cast<double>(x) + 0.5) - star.x;
                    const double ddy = (static_cast<double>(y) + 0.5) - star.y;
                    moved.samples[(static_cast<std::size_t>(c) * moved.height + static_cast<std::size_t>(y)) *
                                      moved.width +
                                  static_cast<std::size_t>(x)] +=
                        static_cast<float>(star.amplitude *
                                           std::exp(-(ddx * ddx + ddy * ddy) / (2.0 * star.sigma * star.sigma)));
                }
            }
        }
    }
    const an::ImageMetadata metadata = an::tests::processes::stars::fieldMetadata(field, solved);
    return writeFrame(
        session, path, moved.width, moved.height, moved.channels, an::SampleType::Float32,
        [&moved](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return moved.value(x, y, c); }, metadata);
}

std::optional<ReadBack> readBack(const an::consumer::EngineSession& session, const QString& path)
{
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = path}, {}, nullptr);
    if (!read.hasValue())
    {
        ADD_FAILURE() << "'" << path.toStdString() << "': " << read.status().message();
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    ReadBack back;
    std::visit(
        [&back](const auto& typed) {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            back.type = an::kSampleTypeOf<SampleT>;
            back.width = view->width();
            back.height = view->height();
            back.channels = view->channelCount();
            back.fractions.reserve(static_cast<std::size_t>(back.width) * back.height * back.channels);
            for (std::uint32_t c = 0u; c < back.channels; ++c)
            {
                for (std::uint32_t y = 0u; y < back.height; ++y)
                {
                    const auto row = view->row(c, y);
                    for (const SampleT value : *row)
                    {
                        back.fractions.push_back(toFraction<SampleT>(value));
                    }
                }
            }
            for (const an::CodecCard& card : typed.metadata().codec().cards())
            {
                back.cards.emplace_back(card.keyword(), card.value());
            }
        },
        lease->image());
    return back;
}

class LogCapture::Sink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    [[nodiscard]] std::vector<std::string> messages() const
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        return m_messages;
    }

    void onRecord(std::string fragment, std::function<void()> action)
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_actions.emplace_back(std::move(fragment), std::move(action));
    }

    void holdAt(std::string fragment)
    {
        std::lock_guard<std::mutex> lock(m_holdMutex);
        m_holdFragment = std::move(fragment);
        m_held = false;
        m_released = false;
    }

    [[nodiscard]] bool waitHeld(const std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_holdMutex);
        return m_holdChanged.wait_for(lock, timeout, [this]() { return m_held; });
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_holdMutex);
            m_released = true;
        }
        m_holdChanged.notify_all();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& record) override
    {
        const std::string text(record.payload.data(), record.payload.size());
        std::vector<std::function<void()>> due;
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_messages.push_back(text);
            for (const auto& [fragment, action] : m_actions)
            {
                if (text.find(fragment) != std::string::npos)
                {
                    due.push_back(action);
                }
            }
        }
        for (const std::function<void()>& action : due)
        {
            action();
        }
        std::unique_lock<std::mutex> lock(m_holdMutex);
        if (!m_holdFragment.empty() && !m_held && text.find(m_holdFragment) != std::string::npos)
        {
            m_held = true;
            m_holdChanged.notify_all();
            (void)m_holdChanged.wait_for(lock, std::chrono::seconds(10), [this]() { return m_released; });
        }
    }

    void flush_() override {}

private:
    mutable std::mutex m_recordsMutex;
    std::vector<std::string> m_messages;
    std::vector<std::pair<std::string, std::function<void()>>> m_actions;

    std::mutex m_holdMutex;
    std::condition_variable m_holdChanged;
    std::string m_holdFragment;
    bool m_held = false;
    bool m_released = false;
};

LogCapture::LogCapture() : m_sink(std::make_shared<Sink>())
{
    if (an::platform::detail::gLogger)
    {
        m_previousLevel = static_cast<int>(an::platform::detail::gLogger->level());
        an::platform::detail::gLogger->set_level(spdlog::level::trace);
        an::platform::setSessionConsoleSink(m_sink);
    }
}

LogCapture::~LogCapture()
{
    m_sink->release();
    if (an::platform::detail::gLogger)
    {
        an::platform::clearSessionConsoleSink();
        an::platform::detail::gLogger->set_level(static_cast<spdlog::level::level_enum>(m_previousLevel));
    }
}

std::vector<std::string> LogCapture::messages() const
{
    return m_sink->messages();
}

std::size_t LogCapture::count(const std::string& fragment) const
{
    std::size_t found = 0u;
    for (const std::string& text : m_sink->messages())
    {
        found += text.find(fragment) != std::string::npos ? 1u : 0u;
    }
    return found;
}

void LogCapture::onRecord(std::string fragment, std::function<void()> action)
{
    m_sink->onRecord(std::move(fragment), std::move(action));
}

void LogCapture::holdAt(std::string fragment)
{
    m_sink->holdAt(std::move(fragment));
}

bool LogCapture::waitHeld(const std::chrono::milliseconds timeout) const
{
    return m_sink->waitHeld(timeout);
}

void LogCapture::release()
{
    m_sink->release();
}

void WorkflowCase::SetUp()
{
    ProfilerCase::SetUp();
}

void WorkflowCase::TearDown()
{
    // A dialog the integration's companions opened over a tool's window closes with the case.
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (auto* dialog = qobject_cast<QDialog*>(widget); dialog != nullptr && dialog->isVisible())
        {
            dialog->reject();
        }
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ProfilerCase::TearDown();
}

} // namespace an::tests::processes::workflow
