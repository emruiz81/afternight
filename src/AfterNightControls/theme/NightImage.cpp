// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/NightImage.h"

#include "theme/AnDesignTokens.h"

#include <QCache>
#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

/// The luma of 8-bit components, `0.299 R + 0.587 G + 0.114 B` to the nearest whole value, a half rounded up.
constexpr int lumaOf(const int red, const int green, const int blue)
{
    return (299 * red + 587 * green + 114 * blue + 500) / 1000;
}

/// Whether the calling thread is the application's GUI thread.
bool onTheGuiThread()
{
    const QCoreApplication* const application = QCoreApplication::instance();
    return application != nullptr && QThread::currentThread() == application->thread();
}

/// How much the forms `nightImage` keeps may hold at once, in KiB: the pictures a window shows at a time with room.
constexpr qsizetype kKeptKibibytes = 128 * 1024;

/// A caller on the GUI thread waiting for a form: told when it is kept, if it still lives.
struct Waiter
{
    QPointer<QObject> context;
    /// Whether the caller named a context: a waiter whose context is gone is not told.
    bool bound = false;
    std::function<void()> ready;
};

/// The Night forms `nightImage` keeps, the ones being made and the passes made: a theme service, made at the first ask
/// and left to the process's end.
struct NightImages
{
    std::mutex mutex;
    QCache<qint64, QImage> kept{kKeptKibibytes};
    /// The pictures being made on the thread pool, by key, with who waits for each.
    std::unordered_map<qint64, std::vector<Waiter>> making;
    /// Advanced when the kept forms are let go, so a form made before is not kept after.
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> passesOnTheGuiThread{0};
    std::atomic<std::uint64_t> passesElsewhere{0};
};

NightImages& nightImages()
{
    static NightImages images;
    return images;
}

/// The cost a kept form counts against the budget: its bytes in KiB, at least one.
qsizetype costOf(const QImage& form)
{
    return (std::max)(qsizetype{1}, static_cast<qsizetype>(form.sizeInBytes() / 1024));
}

/// Keeps a form made for a key, unless the forms were let go since it was asked for.
void keep(NightImages& images, const qint64 key, const QImage& form, const std::uint64_t generation)
{
    if (images.generation == generation)
    {
        images.kept.insert(key, new QImage(form), costOf(form));
    }
}

} // namespace

namespace an::ui
{

QColor nightColor(const QColor& colour)
{
    if (!colour.isValid())
    {
        return colour;
    }
    return QColor(lumaOf(colour.red(), colour.green(), colour.blue()), 0, 0, colour.alpha());
}

QColor colorAsShown(const QColor& colour)
{
    return isNightMode() ? nightColor(colour) : colour;
}

void toNightInPlace(std::uint8_t* const bytes, const int width, const int height, const std::ptrdiff_t stride,
                    const NightPixelOrder order)
{
    if (bytes == nullptr || width <= 0 || height <= 0)
    {
        return;
    }
    if (onTheGuiThread())
    {
        nightImages().passesOnTheGuiThread.fetch_add(1u, std::memory_order_relaxed);
    }
    else
    {
        nightImages().passesElsewhere.fetch_add(1u, std::memory_order_relaxed);
    }
    const int pixelBytes = order == NightPixelOrder::Rgb ? 3 : 4;
    // Where the red and the blue byte of a pixel lie; green is always the second.
    const int red = order == NightPixelOrder::Bgra ? 2 : 0;
    const int blue = order == NightPixelOrder::Bgra ? 0 : 2;
    for (int y = 0; y < height; ++y)
    {
        std::uint8_t* pixel = bytes + y * stride;
        for (int x = 0; x < width; ++x, pixel += pixelBytes)
        {
            const int luma = lumaOf(pixel[red], pixel[1], pixel[blue]);
            pixel[red] = static_cast<std::uint8_t>(luma);
            pixel[1] = 0u;
            pixel[blue] = 0u;
        }
    }
}

QImage nightForm(const QImage& picture)
{
    if (picture.isNull())
    {
        return {};
    }
    QImage form =
        picture.convertToFormat(picture.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    form.detach();
    // A 32-bit pixel is one word, `0xAARRGGBB`: its bytes lie blue first on a little-endian host, alpha first on a
    // big-endian one, where the red byte is the second and the blue byte the fourth.
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    toNightInPlace(form.bits(), form.width(), form.height(), form.bytesPerLine(), NightPixelOrder::Bgra);
#else
    toNightInPlace(form.bits() + 1, form.width(), form.height(), form.bytesPerLine(), NightPixelOrder::Rgba);
#endif
    return form;
}

QImage nightImage(const QImage& picture, const QObject* const context, std::function<void()> ready)
{
    if (picture.isNull())
    {
        return {};
    }
    NightImages& images = nightImages();
    const qint64 key = picture.cacheKey();
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(images.mutex);
        if (const QImage* const kept = images.kept.object(key); kept != nullptr)
        {
            return *kept;
        }
        generation = images.generation;
        if (onTheGuiThread())
        {
            // Made on the thread pool; the caller is told when it is kept. A picture already being made only gains a
            // waiter.
            const auto [entry, first] = images.making.try_emplace(key);
            if (ready)
            {
                entry->second.push_back(
                    Waiter{QPointer<QObject>(const_cast<QObject*>(context)), context != nullptr, std::move(ready)});
            }
            if (!first)
            {
                return {};
            }
        }
    }

    if (!onTheGuiThread())
    {
        const QImage form = nightForm(picture);
        const std::lock_guard<std::mutex> lock(images.mutex);
        keep(images, key, form, generation);
        return form;
    }

    QThreadPool::globalInstance()->start([picture, key, generation]() {
        NightImages& made = nightImages();
        const QImage form = nightForm(picture);
        std::vector<Waiter> waiters;
        {
            const std::lock_guard<std::mutex> lock(made.mutex);
            keep(made, key, form, generation);
            if (const auto entry = made.making.find(key); entry != made.making.end())
            {
                waiters = std::move(entry->second);
                made.making.erase(entry);
            }
        }
        QCoreApplication* const application = QCoreApplication::instance();
        if (application == nullptr || waiters.empty())
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [waiters = std::move(waiters)]() {
                for (const Waiter& waiter : waiters)
                {
                    if ((!waiter.bound || !waiter.context.isNull()) && waiter.ready)
                    {
                        waiter.ready();
                    }
                }
            },
            Qt::QueuedConnection);
    });
    return {};
}

void NightPicture::setPicture(const QPixmap& picture)
{
    m_picture = picture;
    m_image = picture.toImage();
    m_night = QPixmap();
}

void NightPicture::setPicture(const QImage& picture)
{
    m_picture = QPixmap::fromImage(picture);
    m_image = picture;
    m_night = QPixmap();
}

void NightPicture::clear()
{
    m_picture = QPixmap();
    m_image = QImage();
    m_night = QPixmap();
}

QPixmap NightPicture::shown(const QObject* const owner, const std::function<void()>& repaint) const
{
    if (!isNightMode() || m_picture.isNull())
    {
        // The form is kept only while Night is.
        m_night = QPixmap();
        return m_picture;
    }
    if (m_night.isNull())
    {
        const QImage form = nightImage(m_image, owner, repaint);
        if (form.isNull())
        {
            return {};
        }
        m_night = QPixmap::fromImage(form);
        m_night.setDevicePixelRatio(m_picture.devicePixelRatio());
    }
    return m_night;
}

void clearNightImages()
{
    NightImages& images = nightImages();
    const std::lock_guard<std::mutex> lock(images.mutex);
    images.kept.clear();
    ++images.generation;
}

namespace testing
{

NightPasses nightPasses()
{
    const NightImages& images = nightImages();
    return NightPasses{.onTheGuiThread = images.passesOnTheGuiThread.load(std::memory_order_relaxed),
                       .elsewhere = images.passesElsewhere.load(std::memory_order_relaxed)};
}

std::size_t keptNightImages()
{
    NightImages& images = nightImages();
    const std::lock_guard<std::mutex> lock(images.mutex);
    return static_cast<std::size_t>(images.kept.count());
}

} // namespace testing

} // namespace an::ui
