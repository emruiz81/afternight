// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Frame headers read through the session's codecs for the workflow core.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/FrameHeaderReader.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>
#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>

#include <string>

namespace ImageIntegration
{

FrameHeaderReader sessionFrameHeaderReader(const an::consumer::EngineSession& session)
{
    return [&session](const QString& path) -> std::optional<FrameHeaderFacts> {
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
        if (!scope.hasValue())
        {
            return std::nullopt;
        }
        const std::string nativePath = path.toUtf8().toStdString();
        const an::Result<an::CodecImageInfo> probed = an::probeImage(
            scope->engine(), scope->execution(), an::CodecReadRequest{.path = nativePath, .validatePayload = false});
        if (!probed.hasValue())
        {
            return std::nullopt;
        }
        FrameHeaderFacts facts;
        facts.width = probed->descriptor.extent().width();
        facts.height = probed->descriptor.extent().height();
        if (facts.width == 0u || facts.height == 0u)
        {
            return std::nullopt;
        }
        const auto& cards = probed->metadata.codec().cards();
        const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, facts.width, facts.height);
        if (solution.hasValue() &&
            (solution->distortion().hasForwardModel() || solution->distortion().hasInverseModel()))
        {
            facts.distortion = solution->distortion();
        }
        // The stamp is read from the FITS vocabulary, as the artifact tracker reads it; a malformed card reads as none.
        const auto identity = an::artifactIdentityOf(probed->metadata.codec(), an::CodecNamespace::Fits);
        if (identity.hasValue() && identity->has_value())
        {
            facts.artifactIdentity = QString::fromStdString((*identity)->artifactId());
        }
        return facts;
    };
}

} // namespace ImageIntegration
