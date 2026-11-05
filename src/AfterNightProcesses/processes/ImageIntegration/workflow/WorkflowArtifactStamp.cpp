// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How a workflow stage publishes an image it generates, stamped with the manifest entry it satisfies.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/WorkflowArtifactStamp.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDir>
#include <QFileInfo>

#include <string>

namespace ImageIntegration
{

namespace
{

/// The characters of a text an identity card may hold, at most its longest identity.
[[nodiscard]] std::string identityCharacters(const QString& text)
{
    std::string kept;
    const QByteArray bytes = text.toUtf8();
    for (const char character : bytes)
    {
        if (kept.size() >= an::kMaxArtifactIdentityChars)
        {
            break;
        }
        if (an::isArtifactIdentityChar(character))
        {
            kept.push_back(character);
        }
    }
    return kept;
}

} // namespace

an::Result<an::ArtifactIdentity> workflowArtifactIdentity(const QString& manifestHash, const QString& runId)
{
    std::string artifact = identityCharacters(manifestHash);
    if (artifact.empty())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument,
                                   "the manifest fingerprint holds no character an artifact identity may carry");
    }
    return an::ArtifactIdentity::create(std::move(artifact), identityCharacters(runId));
}

an::Status publishWorkflowImage(const an::consumer::OperationScope& scope, const an::Image<float>& image,
                                const QString& path, const an::ArtifactIdentity& identity)
{
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory))
    {
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   "the directory '" + directory.toUtf8().toStdString() + "' cannot be made");
    }
    const std::string destination = path.toUtf8().toStdString();
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    an::Result<an::RawFrame> frame = an::borrowImageFrame(image);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    an::OutputRequest request;
    request.destinationPath = destination;
    request.format = an::codecFormatFromPath(destination);
    request.intent = &*intent;
    request.identity = &identity;
    request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
    const an::Result<an::OutputReport> written =
        an::writeImage(scope.engine(), scope.execution(), request, *frame, image.metadata());
    if (!written.hasValue())
    {
        return written.status();
    }
    return an::Status::success();
}

} // namespace ImageIntegration
