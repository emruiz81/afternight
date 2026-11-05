// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/CancellationToken.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/persistence/RegistrationMetricsRepository.h"

#include <QString>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace an::ui
{
enum class CometReferenceResolutionStatus
{
    Resolved,
    NeedsDetection,
    NeedsUserChoice,
    Cancelled,
    Failed
};

struct CometReferenceResolutionRequest
{
    QString groupKey;
    int settingsVersion = 0;
    std::vector<FrameId> groupFrames;
    bool autoReference = true;
    std::optional<FrameId> explicitReferenceFrame;
    std::function<QString(const FrameId&)> getFilter;
};

struct CometReferenceResolutionResult
{
    CometReferenceResolutionStatus status = CometReferenceResolutionStatus::Failed;
    std::optional<FrameId> resolvedReferenceFrame;
    std::optional<AutoReferencePickResult> autoReferencePick;
    QString message;

    [[nodiscard]] bool isResolved() const noexcept
    {
        return status == CometReferenceResolutionStatus::Resolved && resolvedReferenceFrame.has_value();
    }
};

class CometReferenceResolutionService
{
public:
    explicit CometReferenceResolutionService(QString metricsPath) : m_metricsPath(std::move(metricsPath)) {}

    [[nodiscard]] CometReferenceResolutionResult resolve(const CometReferenceResolutionRequest& request,
                                                         const CancellationToken* cancellation = nullptr) const
    {
        if (cancellation != nullptr && cancellation->isCancellationRequested())
        {
            return {CometReferenceResolutionStatus::Cancelled, std::nullopt, std::nullopt,
                    QStringLiteral("Reference resolution was cancelled.")};
        }

        if (request.groupFrames.empty())
        {
            return {CometReferenceResolutionStatus::NeedsUserChoice, std::nullopt, std::nullopt,
                    QStringLiteral("No frames are available for reference resolution.")};
        }

        if (!request.autoReference)
        {
            if (!request.explicitReferenceFrame.has_value())
            {
                return {CometReferenceResolutionStatus::NeedsUserChoice, std::nullopt, std::nullopt,
                        QStringLiteral("Choose a reference frame before comet tracking.")};
            }

            const FrameId reference = request.explicitReferenceFrame.value();
            if (!containsFrame(request.groupFrames, reference))
            {
                return {CometReferenceResolutionStatus::NeedsUserChoice, std::nullopt, std::nullopt,
                        QStringLiteral("The selected reference frame is not part of this registration group.")};
            }

            return {CometReferenceResolutionStatus::Resolved, reference, std::nullopt, QString()};
        }

        QString error;
        const auto getFilter =
            request.getFilter ? request.getFilter : [](const FrameId&) { return QStringLiteral("(unknown)"); };
        const RegistrationMetricsRepository repository(m_metricsPath);
        const auto pick = repository.pickBestReference(request.groupKey, request.settingsVersion, request.groupFrames,
                                                       getFilter, error);

        if (cancellation != nullptr && cancellation->isCancellationRequested())
        {
            return {CometReferenceResolutionStatus::Cancelled, std::nullopt, std::nullopt,
                    QStringLiteral("Reference resolution was cancelled.")};
        }

        if (pick.has_value())
        {
            return {CometReferenceResolutionStatus::Resolved, pick->id, pick, QString()};
        }

        if (isMissingMetricsError(error))
        {
            return {CometReferenceResolutionStatus::NeedsDetection, std::nullopt, std::nullopt, error};
        }

        return {CometReferenceResolutionStatus::Failed, std::nullopt, std::nullopt, error};
    }

private:
    [[nodiscard]] static bool containsFrame(const std::vector<FrameId>& frames, const FrameId& needle)
    {
        const QString canonical = needle.canonicalPath();
        return std::any_of(frames.begin(), frames.end(),
                           [&](const FrameId& frame) { return frame.canonicalPath() == canonical; });
    }

    [[nodiscard]] static bool isMissingMetricsError(const QString& error)
    {
        return error.contains(QStringLiteral("metrics"), Qt::CaseInsensitive) ||
               error.contains(QStringLiteral("not found"), Qt::CaseInsensitive) ||
               error.contains(QStringLiteral("auto-detect"), Qt::CaseInsensitive);
    }

    QString m_metricsPath;
};
} // namespace an::ui
