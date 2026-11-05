// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonValue>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "processes/ImageIntegration/model/FrameId.h"

namespace
{
bool isWindowsDriveRoot(const QString& path)
{
    return path.size() == 3 && path[1] == QLatin1Char(':') && path[2] == QLatin1Char('/');
}

bool isUncRoot(const QString& path)
{
    if (!path.startsWith(QStringLiteral("//")))
    {
        return false;
    }

    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.size() == 2;
}

QString normalizeExpectedPath(const QString& raw)
{
    QString normalized = QDir::fromNativeSeparators(raw);
    normalized = QDir::cleanPath(normalized);

    QString absolutePath = QDir::fromNativeSeparators(QFileInfo(normalized).absoluteFilePath());
    while (absolutePath.endsWith(QLatin1Char('/')) && absolutePath != QStringLiteral("/") &&
           !isWindowsDriveRoot(absolutePath) && !isUncRoot(absolutePath))
    {
        absolutePath.chop(1);
    }

#ifdef _WIN32
    if (absolutePath.size() >= 2 && absolutePath[1] == QLatin1Char(':'))
    {
        absolutePath[0] = absolutePath[0].toUpper();
    }
#endif

    return absolutePath;
}
} // namespace

TEST(FrameId, CanonicalizesWindowsPaths)
{
#ifdef _WIN32
    FrameId id(QStringLiteral("C:\\Temp\\Folder\\..\\File.fit"));
    EXPECT_EQ(id.canonicalPath(), QStringLiteral("C:/Temp/File.fit"));
#else
    GTEST_SKIP() << "Windows-only path canonicalization.";
#endif
}

TEST(FrameId, CanonicalizesForwardSlashes)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    const QString raw = base + QStringLiteral("/subdir/../file.fit");
    const FrameId id(raw);

    EXPECT_EQ(id.canonicalPath(), normalizeExpectedPath(raw));
}

TEST(FrameId, HandlesDriveLetterCasing)
{
#ifdef _WIN32
    FrameId lower(QStringLiteral("c:\\temp\\file.fit"));
    FrameId upper(QStringLiteral("C:\\TEMP\\FILE.fit"));
    EXPECT_EQ(lower, upper);
#else
    GTEST_SKIP() << "Windows-only drive letter casing.";
#endif
}

TEST(FrameId, HandlesUNCPaths)
{
#ifdef _WIN32
    FrameId unc(QStringLiteral("\\\\Server\\Share\\Dir\\File.fit"));
    FrameId longUnc(QStringLiteral("\\\\?\\UNC\\Server\\Share\\Dir\\File.fit"));
    EXPECT_EQ(unc, longUnc);
#else
    GTEST_SKIP() << "Windows-only UNC paths.";
#endif
}

TEST(FrameId, HandlesSpacesInPath)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId id(base + QStringLiteral("/Test Folder/Frame.fit"));
    EXPECT_TRUE(id.canonicalPath().contains(QLatin1Char(' ')));
}

TEST(FrameId, HandlesUnicodeCharacters)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    const QString unicodePath = base + QStringLiteral("/caf\u00e9.fit");
    FrameId id(unicodePath);
    EXPECT_TRUE(id.canonicalPath().contains(QStringLiteral("caf\u00e9.fit")));
}

TEST(FrameId, EqualityOperatorWorks)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId left(base + QStringLiteral("/subdir/../file.fit"));
    FrameId right(base + QStringLiteral("/file.fit"));
    FrameId other(base + QStringLiteral("/other.fit"));

    EXPECT_EQ(left, right);
    EXPECT_NE(left, other);
}

TEST(FrameId, HashingIsConsistent)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId id1(base + QStringLiteral("/subdir/../file.fit"));
    FrameId id2(base + QStringLiteral("/file.fit"));

    EXPECT_EQ(qHash(id1), qHash(id2));

    std::unordered_set<FrameId> ids;
    ids.insert(id1);
    ids.insert(id2);
    EXPECT_EQ(ids.size(), 1u);
}

TEST(FrameId, LessThanForSorting)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId a(base + QStringLiteral("/a.fit"));
    FrameId b(base + QStringLiteral("/b.fit"));

    std::vector<FrameId> ids{b, a};
    std::sort(ids.begin(), ids.end());

    EXPECT_EQ(ids.front(), a);
}

TEST(FrameId, SerializesToJson)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId id(base + QStringLiteral("/file.fit"));
    QJsonValue value = id.toJson();

    EXPECT_TRUE(value.isString());
    EXPECT_EQ(value.toString(), id.canonicalPath());
}

TEST(FrameId, DeserializesFromJson)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId id(base + QStringLiteral("/file.fit"));
    FrameId parsed = FrameId::fromJson(id.toJson());

    EXPECT_EQ(parsed, id);
}

TEST(FrameId, ToStringProducesDisplayPath)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId id(base + QStringLiteral("/file.fit"));

    EXPECT_EQ(id.toString(), QDir::toNativeSeparators(id.canonicalPath()));
}

TEST(FrameId, InvalidPathThrows)
{
    EXPECT_THROW(FrameId(QStringLiteral("   ")), std::invalid_argument);
}

TEST(FrameId, EmptyPathHandled)
{
    FrameId id;
    EXPECT_FALSE(id.isValid());
    EXPECT_TRUE(id.toString().isEmpty());
}

TEST(FrameId, CopyAndMoveSemantics)
{
    const QString base = QDir::fromNativeSeparators(QDir::tempPath());
    FrameId original(base + QStringLiteral("/file.fit"));

    FrameId copy = original;
    FrameId moved = std::move(copy);

    EXPECT_EQ(moved, original);
    EXPECT_EQ(moved.canonicalPath(), original.canonicalPath());
}
