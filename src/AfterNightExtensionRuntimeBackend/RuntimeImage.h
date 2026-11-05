// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The embedded module's `_RuntimeImage`: an image asset of the backend's session, what the SDK says about it, and the
/// protocol under which NumPy writes into its samples.
///
/// **`to_numpy()` lends NumPy the asset's own samples, writable.** Extensions edit an array and hand the image back, so
/// the array is neither a copy nor read-only. The array's base is a capsule that shares ownership of the asset, so the
/// array and every slice of it keep the samples alive after the `_RuntimeImage` is gone; an asset's samples never move,
/// and replacing an image's shape or representation (`from_numpy`) gives it a new asset while the old arrays keep the
/// old one.
///
/// **While an array exists the asset is externally mutable.** NumPy writes move no engine generation, so a revision
/// read while an export exists does not name the samples. Every synchronous engine call on an exported asset therefore
/// moves the pixel generation before it reads - under the interpreter's lock, which the call keeps, and under an asset
/// lease, which it takes - and the generation moves once more when the last array is released, before ordinary leased
/// access resumes. Nothing starts background work on an exported asset; asynchronous native work is handed a copy made
/// under the same lock.
///
/// Concurrent mutation from NumPy code that releases the interpreter's lock while an engine call runs is outside the
/// contract: the SDK's documentation asks such a caller to synchronize itself.
#pragma once

#include "RuntimePython.h"

#include "RuntimeEngine.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace an::sdk
{

/// One asset of a runtime image and what its NumPy exports owe it.
///
/// Shared by the `_RuntimeImage` that holds it and by the base capsule of every array exported from it. Every member
/// but `externallyMutable` is called with the interpreter's lock held.
class ExportedAsset
{
public:
    /// Holds an asset.
    ///
    /// @param asset the asset, in an SDK image's form.
    explicit ExportedAsset(std::shared_ptr<an::consumer::ImageAsset> asset) noexcept;

    /// The asset.
    [[nodiscard]] an::consumer::ImageAsset& asset() const noexcept;

    /// Counts an array lent to NumPy.
    void exportStarted() noexcept;

    /// Counts an array's release; the last one moves the pixel generation, or marks it owed when a reader holds the
    /// asset now.
    void exportReleased() noexcept;

    /// How many arrays lent from the asset are alive.
    [[nodiscard]] std::size_t writableExports() const noexcept;

    /// Whether NumPy may be writing into the asset's samples: an array is alive, or the generation owed after the
    /// last one has not moved yet. Safe on any thread.
    [[nodiscard]] bool externallyMutable() const noexcept;

    /// A read lease for a synchronous engine call, after moving the pixel generation if NumPy may have written.
    ///
    /// @param context what the binding is doing, for the raised failure.
    /// @return the lease.
    [[nodiscard]] an::consumer::AssetReadLease readForEngine(std::string_view context);

    /// The write lease for a synchronous engine call that writes the samples in place.
    ///
    /// @param context what the binding is doing, for the raised failure.
    /// @return the lease.
    [[nodiscard]] an::consumer::AssetWriteLease writeForEngine(std::string_view context);

private:
    /// Moves the pixel generation if NumPy may have written, waiting for any reader.
    void settleExternalWrites(std::string_view context);

    std::shared_ptr<an::consumer::ImageAsset> m_asset;
    std::atomic<std::size_t> m_exports{0u};
    std::atomic<bool> m_generationOwed{false};
};

/// The embedded module's `_RuntimeImage`.
class RuntimeImage
{
public:
    /// An image of an asset and its metadata.
    ///
    /// @param asset the asset.
    /// @param metadata what the SDK says about it.
    RuntimeImage(std::shared_ptr<ExportedAsset> asset, SdkMetadata metadata);

    /// The samples as a writable NumPy array over the asset's own storage: (height, width) for grey, (height, width,
    /// channels) over planar storage for colour.
    [[nodiscard]] py::array toNumpy() const;

    /// Replaces the samples with a copy of an array, as a new asset; the metadata stays.
    ///
    /// @param array the array.
    void fromNumpy(const py::array& array);

    /// The header cards, the EXIF cards and FILENAME, as text.
    [[nodiscard]] py::dict metadata() const;

    /// The extent, representation and channel layout, the backend and the file name.
    [[nodiscard]] py::dict properties() const;

    /// Sets a header card, or the file name for "FILENAME".
    ///
    /// @param key the keyword; never empty or reserved.
    /// @param value the value; None sets an empty one.
    void setMetadata(const std::string& key, const py::object& value);

    /// A copy: a new asset with its own samples, and the same metadata.
    [[nodiscard]] std::shared_ptr<RuntimeImage> clone() const;

    /// The export protocol's state, for the backend's own suite: the asset's identity and generations and the arrays
    /// alive.
    [[nodiscard]] py::dict exportState() const;

    /// The asset.
    [[nodiscard]] const std::shared_ptr<ExportedAsset>& asset() const noexcept;

    /// What the SDK says about the image.
    [[nodiscard]] const SdkMetadata& sdkMetadata() const noexcept;

    /// What the SDK says about the image, for a binding that changes it.
    [[nodiscard]] SdkMetadata& sdkMetadata() noexcept;

private:
    std::shared_ptr<ExportedAsset> m_asset;
    SdkMetadata m_metadata;
};

/// A runtime image of an engine image: adopted as an asset of the session after being brought into an SDK image's
/// form.
///
/// @param session the session.
/// @param scope the operation that made the image.
/// @param image the image; taken.
/// @param metadata what the SDK says about it.
/// @param context what the binding is doing, for the raised failure.
/// @return the runtime image.
[[nodiscard]] std::shared_ptr<RuntimeImage> runtimeImageOf(const an::consumer::EngineSession& session,
                                                           const an::consumer::OperationScope& scope,
                                                           an::consumer::AnyImage&& image, SdkMetadata metadata,
                                                           std::string_view context);

/// An engine image of a NumPy array: a 2D grey array, or a 3D array in (height, width, channels) or (channels, height,
/// width) order with 1, 3 or 4 channels, of one of the eight representations, copied into compact planar storage.
///
/// @param scope the operation.
/// @param array the array.
/// @return the image.
[[nodiscard]] an::consumer::AnyImage imageFromArray(const an::consumer::OperationScope& scope, const py::array& array);

/// The text a Python value is stored as in a header card: None empty, a bool a FITS logical, a number as Python spells
/// it, anything else a quoted string.
[[nodiscard]] std::string lexicalCardValue(const py::handle& value);

/// Sets every entry of a dictionary as `RuntimeImage::setMetadata` would, passing over empty and reserved keys.
///
/// @param values the entries.
/// @param metadata the metadata to change.
void applyMetadataDict(const py::dict& values, SdkMetadata& metadata);

/// The runtime image a parameter names, or a raised failure for None.
///
/// @param image the parameter.
/// @param parameterName its name, for the failure.
/// @return the image.
[[nodiscard]] const std::shared_ptr<RuntimeImage>& requireRuntimeImage(const std::shared_ptr<RuntimeImage>& image,
                                                                       std::string_view parameterName);

} // namespace an::sdk
