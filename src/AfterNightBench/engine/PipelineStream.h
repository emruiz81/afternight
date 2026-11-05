// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/IO/FrameStream.h>
#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace an::bench::engine
{
namespace pipeline_detail
{
// A sequence of private image transactions. The parent execution outlives the stream and every
// transaction; the callback's temporary execution does not. Publication happens after the stream
// has joined, so a read, processing or encode failure preserves existing destinations and removes
// every pending file. Publication is atomic per file; a later commit failure may leave earlier
// complete files published.
class ImageTransactions final : public FrameSinkBackend
{
public:
    explicit ImageTransactions(std::span<OutputTransaction> transactions) : m_transactions(transactions) {}

    Result<SequenceWriteResult> append(const EngineContext&, const ExecutionContext& execution,
                                       const SequenceFrame& frame) override
    {
        if (const auto stopped = execution.checkCancellation(); stopped.failed())
            return stopped;
        const auto index = frame.position.index;
        if (index >= m_transactions.size())
            return Status::failure(ErrorCode::OutOfRange);
        auto& transaction = m_transactions[static_cast<std::size_t>(index)];
        if (const auto encoded = transaction.encode(frame.frame, frame.metadata); encoded.failed())
            return encoded;
        std::error_code error;
        const auto bytes = std::filesystem::file_size(std::filesystem::path(transaction.pendingPath()), error);
        if (error)
            return Status::failure(ErrorCode::ExternalFailure, "cannot inspect the encoded pipeline frame");
        ++m_progress.framesWritten;
        m_progress.bytesWritten += bytes;
        return m_progress;
    }

    Result<SequenceWriteResult> finish(const ExecutionContext&) override
    {
        return m_progress;
    }
    Status close() override
    {
        return Status::success();
    }

private:
    std::span<OutputTransaction> m_transactions;
    SequenceWriteResult m_progress;
};

template <class FunctionT> class Processor final : public FrameStreamProcessor
{
public:
    explicit Processor(FunctionT& function) : m_function(function) {}
    Result<SequenceFrame> process(const EngineContext& engine, const ExecutionContext& execution,
                                  SequenceFrame frame) override
    {
        return m_function(engine, execution, std::move(frame));
    }

private:
    FunctionT& m_function;
};
} // namespace pipeline_detail

// Bounded ordered file processing through the same public stream a consumer uses. Empty output
// paths make this a visitor. One queued frame per side bounds memory independently
// of the input count; a one-worker execution remains inline under the engine's contract.
template <class FunctionT>
Status streamPipelineFiles(const EngineContext& engine, const ExecutionContext& execution,
                           std::span<const std::string> inputs, std::span<const std::string> outputs,
                           FunctionT&& function, FrameStreamCounters& counters)
{
    if (!outputs.empty() && outputs.size() != inputs.size())
        return Status::failure(ErrorCode::IncompatibleArguments);
    std::vector<CodecReadRequest> requests;
    requests.reserve(inputs.size());
    for (const auto& path : inputs)
        requests.push_back({.path = path, .format = CodecFormat::Fits});
    auto source = createImageListSource(engine, execution, requests, {});
    if (!source)
        return source.status();
    std::vector<OutputTransaction> transactions;
    transactions.reserve(outputs.size());
    for (const auto& path : outputs)
    {
        auto transaction = OutputTransaction::begin(
            engine, execution,
            {.destinationPath = path, .format = CodecFormat::Fits, .durability = OutputDurability::CloseOnly});
        if (!transaction)
            return transaction.status();
        transactions.push_back(std::move(*transaction));
    }
    FrameSink sink;
    if (!outputs.empty())
    {
        auto made = FrameSink::create(engine, {.frameCount = outputs.size()},
                                      std::make_unique<pipeline_detail::ImageTransactions>(transactions));
        if (!made)
            return made.status();
        sink = std::move(*made);
    }
    pipeline_detail::Processor processor(function);
    const auto status = streamFrames(engine, execution, *source, outputs.empty() ? nullptr : &sink, &processor,
                                     {.frameLimit = inputs.size()}, counters);
    if (status.failed())
        return status;
    for (auto& transaction : transactions)
    {
        if (auto committed = transaction.commit(); !committed)
            return committed.status();
    }
    return Status::success();
}
} // namespace an::bench::engine
