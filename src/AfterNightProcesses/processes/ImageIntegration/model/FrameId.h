// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/**
 * @file FrameId.h
 * @brief Canonical identity key for image frames.
 *
 * FrameId provides a platform-independent, canonicalized identifier for
 * image files. It normalizes path separators, case sensitivity (on Windows),
 * and provides consistent hashing for use in containers.
 *
 * @par Design Rationale
 * File paths can vary in representation (forward/backslash, case, relative vs
 * absolute). FrameId ensures consistent identity across the application by
 * canonicalizing paths at construction time without filesystem access.
 *
 * @see FrameCollection, IntegrationDataModel
 */

#include <QHash>
#include <QJsonValue>
#include <QString>

#include <functional>

/**
 * @brief Canonicalized identity key for an image frame.
 *
 * FrameId wraps a file path and provides a stable identity that can be used
 * as a key in hash maps and for equality comparison. The canonicalization
 * ensures that different representations of the same file path compare equal.
 *
 * @par Thread Safety
 * FrameId is immutable after construction and safe for concurrent access.
 *
 * @par Example
 * @code
 * FrameId id1("C:\\Images\\light_001.fits");
 * FrameId id2("c:/images/light_001.fits");
 * assert(id1 == id2);  // Same canonical identity
 * @endcode
 */
class AFTERNIGHTPROCESSES_API FrameId
{
public:
    /** @brief Construct an invalid FrameId with an empty path. */
    FrameId() = default;

    /**
     * @brief Construct a FrameId from a file path string.
     * @param path The file path to canonicalize (may be relative or absolute).
     * @note The path is canonicalized without filesystem access.
     */
    explicit FrameId(const QString& path);

    /** @brief Return the canonical identity path. */
    [[nodiscard]] const QString& canonicalPath() const noexcept;

    /** @brief Return the normalized comparison/hash key used by FrameId identity. */
    [[nodiscard]] const QString& identityKey() const noexcept
    {
        return m_identityKey;
    }

    /** @brief Return a display-friendly path using native separators. */
    [[nodiscard]] QString toString() const;

    /** @brief Return true if this FrameId contains a non-empty canonical path. */
    [[nodiscard]] bool isValid() const noexcept;

    /**
     * @brief Serialize the FrameId to a JSON value.
     * @return JSON string containing the canonical path.
     */
    [[nodiscard]] QJsonValue toJson() const;

    /**
     * @brief Deserialize a FrameId from a JSON value.
     * @param value JSON string containing a file path.
     * @return Reconstructed FrameId.
     */
    [[nodiscard]] static FrameId fromJson(const QJsonValue& value);

    friend bool operator==(const FrameId& lhs, const FrameId& rhs) noexcept
    {
        return lhs.m_identityKey == rhs.m_identityKey;
    }

    friend bool operator!=(const FrameId& lhs, const FrameId& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend bool operator<(const FrameId& lhs, const FrameId& rhs) noexcept
    {
        return lhs.m_identityKey < rhs.m_identityKey;
    }

    friend size_t qHash(const FrameId& id, size_t seed) noexcept;

private:
    [[nodiscard]] static QString canonicalizePath(const QString& path);
    [[nodiscard]] static QString identityKeyFor(const QString& canonicalPath);

    QString m_canonicalPath;
    QString m_identityKey;
};

[[nodiscard]] inline size_t qHash(const FrameId& id, size_t seed = 0) noexcept
{
    return qHash(id.m_identityKey, seed);
}

namespace std
{
template <> struct hash<FrameId>
{
    size_t operator()(const FrameId& id) const noexcept
    {
        return qHash(id);
    }
};
} // namespace std
