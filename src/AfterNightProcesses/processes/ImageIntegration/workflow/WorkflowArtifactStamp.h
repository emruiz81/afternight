// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How a workflow stage publishes an image it generates: through the engine's output transaction, whole or not at all,
/// carrying in its own header the identity of the manifest entry it satisfies and the run that wrote it.
///
/// An image written with no identity could have its reuse decided only by hashing its whole payload. Every image a
/// stage writes through `publishWorkflowImage` states its identity, and the artifact tracker decides reuse from a stat
/// or a header read.

#include "AfterNightProcessesExport.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/Image/Image.h>

#include <QString>

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace ImageIntegration
{

/// The identity a generated image carries: the manifest fingerprint and the run identity, each reduced to the
/// characters an identity card may hold and to its longest length.
///
/// @param manifestHash the fingerprint of the manifest entry the image satisfies.
/// @param runId the run that writes it; empty for none.
/// @return the identity; `ErrorCode::InvalidArgument` when nothing of the fingerprint remains.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ArtifactIdentity>
workflowArtifactIdentity(const QString& manifestHash, const QString& runId);

/// Writes a generated Float32 image to the container its path names through the output transaction, stamped with its
/// identity: the destination holds the whole image or what it held before, and nothing is left beside it. The
/// directory is made when it does not exist.
///
/// @param scope the stage's admitted operation.
/// @param image the image.
/// @param path where it is published.
/// @param identity the identity it carries.
/// @return success; `ErrorCode::ExternalFailure` when the directory cannot be made; or what the transaction refused, a
/// stop
///         among them.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status publishWorkflowImage(const an::consumer::OperationScope& scope,
                                                                      const an::Image<float>& image,
                                                                      const QString& path,
                                                                      const an::ArtifactIdentity& identity);

} // namespace ImageIntegration
