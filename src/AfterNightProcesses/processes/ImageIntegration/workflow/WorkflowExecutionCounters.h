// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

/**
 * @brief Immutable snapshot of optional workflow execution diagnostics.
 */
class WorkflowExecutionCounters
{
public:
    constexpr WorkflowExecutionCounters() noexcept = default;

    constexpr WorkflowExecutionCounters(
        std::uint64_t bytesHashed, std::uint64_t bytesCopied, std::uint64_t sqliteStatementExecutions,
        std::uint64_t sqliteTransactionsStarted, std::uint64_t sqliteTransactionsCommitted,
        std::uint64_t sqliteTransactionsRolledBack, std::uint64_t inputDecodeOperations = 0,
        std::uint64_t scientificOutputEncodeCompletions = 0, std::uint64_t inputPassesStarted = 0,
        std::uint64_t tilePassesStarted = 0, std::uint64_t dumpBytesRead = 0, std::uint64_t dumpBytesWritten = 0,
        std::uint64_t pixmapBytesRead = 0, std::uint64_t pixmapBytesWritten = 0,
        std::uint64_t descriptorProviderHits = 0, std::uint64_t denseProviderHits = 0,
        std::uint64_t sipLazyProviderHits = 0, std::uint64_t polynomialLazyProviderHits = 0,
        std::uint64_t sipCoordinatesEvaluated = 0, std::uint64_t sharedSipPowerTableBuilds = 0,
        std::uint64_t explicitSipInverseEvaluations = 0, std::uint64_t sipNewtonFallbackIterations = 0,
        std::uint64_t polynomialCoordinatesEvaluated = 0, std::uint64_t sharedPolynomialPowerTableBuilds = 0,
        std::uint64_t lazyBlocksGenerated = 0, std::uint64_t lazyCoordinatePairsGenerated = 0,
        std::uint64_t peakLazyCoordinateBytes = 0, std::uint64_t denseFallbackCount = 0,
        std::uint64_t lazyMapCancellations = 0, std::uint64_t lazyMapFailures = 0,
        std::uint64_t stackLoaderRequests = 0, std::uint64_t roiSubsetReads = 0,
        std::uint64_t fullFrameFallbackReads = 0, std::uint64_t logicalDecodedPixelBytes = 0,
        std::uint64_t calibrationInvocations = 0, std::uint64_t calibratedSpillFilesCreated = 0,
        std::uint64_t calibratedSpillBytesWritten = 0, std::uint64_t calibratedSpillBytesRead = 0,
        std::uint64_t calibratedSpillRoiReads = 0, std::uint64_t calibratedSpillCleanupSuccesses = 0,
        std::uint64_t calibratedSpillCleanupFailures = 0) noexcept
        : m_bytesHashed(bytesHashed), m_bytesCopied(bytesCopied),
          m_sqliteStatementExecutions(sqliteStatementExecutions),
          m_sqliteTransactionsStarted(sqliteTransactionsStarted),
          m_sqliteTransactionsCommitted(sqliteTransactionsCommitted),
          m_sqliteTransactionsRolledBack(sqliteTransactionsRolledBack), m_inputDecodeOperations(inputDecodeOperations),
          m_scientificOutputEncodeCompletions(scientificOutputEncodeCompletions),
          m_inputPassesStarted(inputPassesStarted), m_tilePassesStarted(tilePassesStarted),
          m_dumpBytesRead(dumpBytesRead), m_dumpBytesWritten(dumpBytesWritten), m_pixmapBytesRead(pixmapBytesRead),
          m_pixmapBytesWritten(pixmapBytesWritten), m_descriptorProviderHits(descriptorProviderHits),
          m_denseProviderHits(denseProviderHits), m_sipLazyProviderHits(sipLazyProviderHits),
          m_polynomialLazyProviderHits(polynomialLazyProviderHits), m_sipCoordinatesEvaluated(sipCoordinatesEvaluated),
          m_sharedSipPowerTableBuilds(sharedSipPowerTableBuilds),
          m_explicitSipInverseEvaluations(explicitSipInverseEvaluations),
          m_sipNewtonFallbackIterations(sipNewtonFallbackIterations),
          m_polynomialCoordinatesEvaluated(polynomialCoordinatesEvaluated),
          m_sharedPolynomialPowerTableBuilds(sharedPolynomialPowerTableBuilds),
          m_lazyBlocksGenerated(lazyBlocksGenerated), m_lazyCoordinatePairsGenerated(lazyCoordinatePairsGenerated),
          m_peakLazyCoordinateBytes(peakLazyCoordinateBytes), m_denseFallbackCount(denseFallbackCount),
          m_lazyMapCancellations(lazyMapCancellations), m_lazyMapFailures(lazyMapFailures),
          m_stackLoaderRequests(stackLoaderRequests), m_roiSubsetReads(roiSubsetReads),
          m_fullFrameFallbackReads(fullFrameFallbackReads), m_logicalDecodedPixelBytes(logicalDecodedPixelBytes),
          m_calibrationInvocations(calibrationInvocations), m_calibratedSpillFilesCreated(calibratedSpillFilesCreated),
          m_calibratedSpillBytesWritten(calibratedSpillBytesWritten),
          m_calibratedSpillBytesRead(calibratedSpillBytesRead), m_calibratedSpillRoiReads(calibratedSpillRoiReads),
          m_calibratedSpillCleanupSuccesses(calibratedSpillCleanupSuccesses),
          m_calibratedSpillCleanupFailures(calibratedSpillCleanupFailures)
    {
    }

    [[nodiscard]] constexpr std::uint64_t bytesHashed() const noexcept
    {
        return m_bytesHashed;
    }

    [[nodiscard]] constexpr std::uint64_t bytesCopied() const noexcept
    {
        return m_bytesCopied;
    }

    [[nodiscard]] constexpr std::uint64_t sqliteStatementExecutions() const noexcept
    {
        return m_sqliteStatementExecutions;
    }

    [[nodiscard]] constexpr std::uint64_t sqliteTransactionsStarted() const noexcept
    {
        return m_sqliteTransactionsStarted;
    }

    [[nodiscard]] constexpr std::uint64_t sqliteTransactionsCommitted() const noexcept
    {
        return m_sqliteTransactionsCommitted;
    }

    [[nodiscard]] constexpr std::uint64_t sqliteTransactionsRolledBack() const noexcept
    {
        return m_sqliteTransactionsRolledBack;
    }

    [[nodiscard]] constexpr std::uint64_t inputDecodeOperations() const noexcept
    {
        return m_inputDecodeOperations;
    }

    [[nodiscard]] constexpr std::uint64_t scientificOutputEncodeCompletions() const noexcept
    {
        return m_scientificOutputEncodeCompletions;
    }

    [[nodiscard]] constexpr std::uint64_t inputPassesStarted() const noexcept
    {
        return m_inputPassesStarted;
    }

    [[nodiscard]] constexpr std::uint64_t tilePassesStarted() const noexcept
    {
        return m_tilePassesStarted;
    }

    [[nodiscard]] constexpr std::uint64_t dumpBytesRead() const noexcept
    {
        return m_dumpBytesRead;
    }

    [[nodiscard]] constexpr std::uint64_t dumpBytesWritten() const noexcept
    {
        return m_dumpBytesWritten;
    }

    [[nodiscard]] constexpr std::uint64_t pixmapBytesRead() const noexcept
    {
        return m_pixmapBytesRead;
    }

    [[nodiscard]] constexpr std::uint64_t pixmapBytesWritten() const noexcept
    {
        return m_pixmapBytesWritten;
    }

    [[nodiscard]] constexpr std::uint64_t descriptorProviderHits() const noexcept
    {
        return m_descriptorProviderHits;
    }
    [[nodiscard]] constexpr std::uint64_t denseProviderHits() const noexcept
    {
        return m_denseProviderHits;
    }
    [[nodiscard]] constexpr std::uint64_t sipLazyProviderHits() const noexcept
    {
        return m_sipLazyProviderHits;
    }
    [[nodiscard]] constexpr std::uint64_t polynomialLazyProviderHits() const noexcept
    {
        return m_polynomialLazyProviderHits;
    }
    [[nodiscard]] constexpr std::uint64_t sipCoordinatesEvaluated() const noexcept
    {
        return m_sipCoordinatesEvaluated;
    }
    [[nodiscard]] constexpr std::uint64_t sharedSipPowerTableBuilds() const noexcept
    {
        return m_sharedSipPowerTableBuilds;
    }
    [[nodiscard]] constexpr std::uint64_t explicitSipInverseEvaluations() const noexcept
    {
        return m_explicitSipInverseEvaluations;
    }
    [[nodiscard]] constexpr std::uint64_t sipNewtonFallbackIterations() const noexcept
    {
        return m_sipNewtonFallbackIterations;
    }
    [[nodiscard]] constexpr std::uint64_t polynomialCoordinatesEvaluated() const noexcept
    {
        return m_polynomialCoordinatesEvaluated;
    }
    [[nodiscard]] constexpr std::uint64_t sharedPolynomialPowerTableBuilds() const noexcept
    {
        return m_sharedPolynomialPowerTableBuilds;
    }
    [[nodiscard]] constexpr std::uint64_t lazyBlocksGenerated() const noexcept
    {
        return m_lazyBlocksGenerated;
    }
    [[nodiscard]] constexpr std::uint64_t lazyCoordinatePairsGenerated() const noexcept
    {
        return m_lazyCoordinatePairsGenerated;
    }
    [[nodiscard]] constexpr std::uint64_t peakLazyCoordinateBytes() const noexcept
    {
        return m_peakLazyCoordinateBytes;
    }
    [[nodiscard]] constexpr std::uint64_t denseFallbackCount() const noexcept
    {
        return m_denseFallbackCount;
    }
    [[nodiscard]] constexpr std::uint64_t lazyMapCancellations() const noexcept
    {
        return m_lazyMapCancellations;
    }
    [[nodiscard]] constexpr std::uint64_t lazyMapFailures() const noexcept
    {
        return m_lazyMapFailures;
    }

    [[nodiscard]] constexpr std::uint64_t stackLoaderRequests() const noexcept
    {
        return m_stackLoaderRequests;
    }

    [[nodiscard]] constexpr std::uint64_t roiSubsetReads() const noexcept
    {
        return m_roiSubsetReads;
    }

    [[nodiscard]] constexpr std::uint64_t fullFrameFallbackReads() const noexcept
    {
        return m_fullFrameFallbackReads;
    }

    [[nodiscard]] constexpr std::uint64_t logicalDecodedPixelBytes() const noexcept
    {
        return m_logicalDecodedPixelBytes;
    }

    [[nodiscard]] constexpr std::uint64_t calibrationInvocations() const noexcept
    {
        return m_calibrationInvocations;
    }

    [[nodiscard]] constexpr std::uint64_t calibratedSpillFilesCreated() const noexcept
    {
        return m_calibratedSpillFilesCreated;
    }

    [[nodiscard]] constexpr std::uint64_t calibratedSpillBytesWritten() const noexcept
    {
        return m_calibratedSpillBytesWritten;
    }

    [[nodiscard]] constexpr std::uint64_t calibratedSpillBytesRead() const noexcept
    {
        return m_calibratedSpillBytesRead;
    }

    [[nodiscard]] constexpr std::uint64_t calibratedSpillRoiReads() const noexcept
    {
        return m_calibratedSpillRoiReads;
    }

    [[nodiscard]] constexpr std::uint64_t calibratedSpillCleanupSuccesses() const noexcept
    {
        return m_calibratedSpillCleanupSuccesses;
    }

    [[nodiscard]] constexpr std::uint64_t calibratedSpillCleanupFailures() const noexcept
    {
        return m_calibratedSpillCleanupFailures;
    }

private:
    std::uint64_t m_bytesHashed = 0;
    std::uint64_t m_bytesCopied = 0;
    std::uint64_t m_sqliteStatementExecutions = 0;
    std::uint64_t m_sqliteTransactionsStarted = 0;
    std::uint64_t m_sqliteTransactionsCommitted = 0;
    std::uint64_t m_sqliteTransactionsRolledBack = 0;
    std::uint64_t m_inputDecodeOperations = 0;
    std::uint64_t m_scientificOutputEncodeCompletions = 0;
    std::uint64_t m_inputPassesStarted = 0;
    std::uint64_t m_tilePassesStarted = 0;
    std::uint64_t m_dumpBytesRead = 0;
    std::uint64_t m_dumpBytesWritten = 0;
    std::uint64_t m_pixmapBytesRead = 0;
    std::uint64_t m_pixmapBytesWritten = 0;
    std::uint64_t m_descriptorProviderHits = 0;
    std::uint64_t m_denseProviderHits = 0;
    std::uint64_t m_sipLazyProviderHits = 0;
    std::uint64_t m_polynomialLazyProviderHits = 0;
    std::uint64_t m_sipCoordinatesEvaluated = 0;
    std::uint64_t m_sharedSipPowerTableBuilds = 0;
    std::uint64_t m_explicitSipInverseEvaluations = 0;
    std::uint64_t m_sipNewtonFallbackIterations = 0;
    std::uint64_t m_polynomialCoordinatesEvaluated = 0;
    std::uint64_t m_sharedPolynomialPowerTableBuilds = 0;
    std::uint64_t m_lazyBlocksGenerated = 0;
    std::uint64_t m_lazyCoordinatePairsGenerated = 0;
    std::uint64_t m_peakLazyCoordinateBytes = 0;
    std::uint64_t m_denseFallbackCount = 0;
    std::uint64_t m_lazyMapCancellations = 0;
    std::uint64_t m_lazyMapFailures = 0;
    std::uint64_t m_stackLoaderRequests = 0;
    std::uint64_t m_roiSubsetReads = 0;
    std::uint64_t m_fullFrameFallbackReads = 0;
    std::uint64_t m_logicalDecodedPixelBytes = 0;
    std::uint64_t m_calibrationInvocations = 0;
    std::uint64_t m_calibratedSpillFilesCreated = 0;
    std::uint64_t m_calibratedSpillBytesWritten = 0;
    std::uint64_t m_calibratedSpillBytesRead = 0;
    std::uint64_t m_calibratedSpillRoiReads = 0;
    std::uint64_t m_calibratedSpillCleanupSuccesses = 0;
    std::uint64_t m_calibratedSpillCleanupFailures = 0;
};

/**
 * @brief Execution-scoped mutable accumulator for low-frequency diagnostics.
 *
 * Atomic fields make increments safe when workflow helpers run concurrently.
 * All additions saturate at UINT64_MAX rather than wrapping.
 */
class WorkflowExecutionCounterAccumulator
{
public:
    void addBytesHashed(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_bytesHashed, amount);
    }

    void addBytesCopied(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_bytesCopied, amount);
    }

    void recordSqliteStatementExecution() noexcept
    {
        saturatingAdd(m_sqliteStatementExecutions, 1);
    }

    void recordSqliteTransactionStarted() noexcept
    {
        saturatingAdd(m_sqliteTransactionsStarted, 1);
    }

    void recordSqliteTransactionCommitted() noexcept
    {
        saturatingAdd(m_sqliteTransactionsCommitted, 1);
    }

    void recordSqliteTransactionRolledBack() noexcept
    {
        saturatingAdd(m_sqliteTransactionsRolledBack, 1);
    }

    void addInputDecodeOperations(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_inputDecodeOperations, amount);
    }

    void addScientificOutputEncodeCompletions(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_scientificOutputEncodeCompletions, amount);
    }

    void addInputPassesStarted(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_inputPassesStarted, amount);
    }

    void addTilePassesStarted(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_tilePassesStarted, amount);
    }

    void addStackLoaderRequests(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_stackLoaderRequests, amount);
    }

    void addRoiSubsetReads(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_roiSubsetReads, amount);
    }

    void addFullFrameFallbackReads(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_fullFrameFallbackReads, amount);
    }

    void addLogicalDecodedPixelBytes(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_logicalDecodedPixelBytes, amount);
    }

    void addCalibrationInvocations(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibrationInvocations, amount);
    }

    void addCalibratedSpillFilesCreated(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibratedSpillFilesCreated, amount);
    }

    void addCalibratedSpillBytesWritten(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibratedSpillBytesWritten, amount);
    }

    void addCalibratedSpillBytesRead(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibratedSpillBytesRead, amount);
    }

    void addCalibratedSpillRoiReads(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibratedSpillRoiReads, amount);
    }

    void addCalibratedSpillCleanupSuccesses(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibratedSpillCleanupSuccesses, amount);
    }

    void addCalibratedSpillCleanupFailures(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_calibratedSpillCleanupFailures, amount);
    }

    void addDumpBytesRead(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_dumpBytesRead, amount);
    }

    void addDumpBytesWritten(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_dumpBytesWritten, amount);
    }

    void addPixmapBytesRead(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_pixmapBytesRead, amount);
    }

    void addPixmapBytesWritten(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_pixmapBytesWritten, amount);
    }

    void addDescriptorProviderHits(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_descriptorProviderHits, amount);
    }
    void addDenseProviderHits(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_denseProviderHits, amount);
    }
    void addSipLazyProviderHits(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_sipLazyProviderHits, amount);
    }
    void addPolynomialLazyProviderHits(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_polynomialLazyProviderHits, amount);
    }
    void addSipCoordinatesEvaluated(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_sipCoordinatesEvaluated, amount);
    }
    void addSharedSipPowerTableBuilds(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_sharedSipPowerTableBuilds, amount);
    }
    void addExplicitSipInverseEvaluations(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_explicitSipInverseEvaluations, amount);
    }
    void addSipNewtonFallbackIterations(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_sipNewtonFallbackIterations, amount);
    }
    void addPolynomialCoordinatesEvaluated(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_polynomialCoordinatesEvaluated, amount);
    }
    void addSharedPolynomialPowerTableBuilds(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_sharedPolynomialPowerTableBuilds, amount);
    }
    void addLazyBlocksGenerated(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_lazyBlocksGenerated, amount);
    }
    void addLazyCoordinatePairsGenerated(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_lazyCoordinatePairsGenerated, amount);
    }
    void updatePeakLazyCoordinateBytes(std::uint64_t amount) noexcept
    {
        std::uint64_t current = m_peakLazyCoordinateBytes.load(std::memory_order_relaxed);
        while (current < amount &&
               !m_peakLazyCoordinateBytes.compare_exchange_weak(current, amount, std::memory_order_relaxed))
        {
        }
    }
    void addDenseFallbackCount(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_denseFallbackCount, amount);
    }
    void addLazyMapCancellations(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_lazyMapCancellations, amount);
    }
    void addLazyMapFailures(std::uint64_t amount) noexcept
    {
        saturatingAdd(m_lazyMapFailures, amount);
    }

    [[nodiscard]] WorkflowExecutionCounters snapshot() const noexcept
    {
        return {m_bytesHashed.load(std::memory_order_relaxed),
                m_bytesCopied.load(std::memory_order_relaxed),
                m_sqliteStatementExecutions.load(std::memory_order_relaxed),
                m_sqliteTransactionsStarted.load(std::memory_order_relaxed),
                m_sqliteTransactionsCommitted.load(std::memory_order_relaxed),
                m_sqliteTransactionsRolledBack.load(std::memory_order_relaxed),
                m_inputDecodeOperations.load(std::memory_order_relaxed),
                m_scientificOutputEncodeCompletions.load(std::memory_order_relaxed),
                m_inputPassesStarted.load(std::memory_order_relaxed),
                m_tilePassesStarted.load(std::memory_order_relaxed),
                m_dumpBytesRead.load(std::memory_order_relaxed),
                m_dumpBytesWritten.load(std::memory_order_relaxed),
                m_pixmapBytesRead.load(std::memory_order_relaxed),
                m_pixmapBytesWritten.load(std::memory_order_relaxed),
                m_descriptorProviderHits.load(std::memory_order_relaxed),
                m_denseProviderHits.load(std::memory_order_relaxed),
                m_sipLazyProviderHits.load(std::memory_order_relaxed),
                m_polynomialLazyProviderHits.load(std::memory_order_relaxed),
                m_sipCoordinatesEvaluated.load(std::memory_order_relaxed),
                m_sharedSipPowerTableBuilds.load(std::memory_order_relaxed),
                m_explicitSipInverseEvaluations.load(std::memory_order_relaxed),
                m_sipNewtonFallbackIterations.load(std::memory_order_relaxed),
                m_polynomialCoordinatesEvaluated.load(std::memory_order_relaxed),
                m_sharedPolynomialPowerTableBuilds.load(std::memory_order_relaxed),
                m_lazyBlocksGenerated.load(std::memory_order_relaxed),
                m_lazyCoordinatePairsGenerated.load(std::memory_order_relaxed),
                m_peakLazyCoordinateBytes.load(std::memory_order_relaxed),
                m_denseFallbackCount.load(std::memory_order_relaxed),
                m_lazyMapCancellations.load(std::memory_order_relaxed),
                m_lazyMapFailures.load(std::memory_order_relaxed),
                m_stackLoaderRequests.load(std::memory_order_relaxed),
                m_roiSubsetReads.load(std::memory_order_relaxed),
                m_fullFrameFallbackReads.load(std::memory_order_relaxed),
                m_logicalDecodedPixelBytes.load(std::memory_order_relaxed),
                m_calibrationInvocations.load(std::memory_order_relaxed),
                m_calibratedSpillFilesCreated.load(std::memory_order_relaxed),
                m_calibratedSpillBytesWritten.load(std::memory_order_relaxed),
                m_calibratedSpillBytesRead.load(std::memory_order_relaxed),
                m_calibratedSpillRoiReads.load(std::memory_order_relaxed),
                m_calibratedSpillCleanupSuccesses.load(std::memory_order_relaxed),
                m_calibratedSpillCleanupFailures.load(std::memory_order_relaxed)};
    }

private:
    static void saturatingAdd(std::atomic<std::uint64_t>& counter, const std::uint64_t amount) noexcept
    {
        std::uint64_t current = counter.load(std::memory_order_relaxed);
        constexpr std::uint64_t kMaximum = (std::numeric_limits<std::uint64_t>::max)();
        while (current != kMaximum)
        {
            const std::uint64_t desired = amount > kMaximum - current ? kMaximum : current + amount;
            if (counter.compare_exchange_weak(current, desired, std::memory_order_relaxed))
            {
                return;
            }
        }
    }

    std::atomic<std::uint64_t> m_bytesHashed{0};
    std::atomic<std::uint64_t> m_bytesCopied{0};
    std::atomic<std::uint64_t> m_sqliteStatementExecutions{0};
    std::atomic<std::uint64_t> m_sqliteTransactionsStarted{0};
    std::atomic<std::uint64_t> m_sqliteTransactionsCommitted{0};
    std::atomic<std::uint64_t> m_sqliteTransactionsRolledBack{0};
    std::atomic<std::uint64_t> m_inputDecodeOperations{0};
    std::atomic<std::uint64_t> m_scientificOutputEncodeCompletions{0};
    std::atomic<std::uint64_t> m_inputPassesStarted{0};
    std::atomic<std::uint64_t> m_tilePassesStarted{0};
    std::atomic<std::uint64_t> m_dumpBytesRead{0};
    std::atomic<std::uint64_t> m_dumpBytesWritten{0};
    std::atomic<std::uint64_t> m_pixmapBytesRead{0};
    std::atomic<std::uint64_t> m_pixmapBytesWritten{0};
    std::atomic<std::uint64_t> m_descriptorProviderHits{0};
    std::atomic<std::uint64_t> m_denseProviderHits{0};
    std::atomic<std::uint64_t> m_sipLazyProviderHits{0};
    std::atomic<std::uint64_t> m_polynomialLazyProviderHits{0};
    std::atomic<std::uint64_t> m_sipCoordinatesEvaluated{0};
    std::atomic<std::uint64_t> m_sharedSipPowerTableBuilds{0};
    std::atomic<std::uint64_t> m_explicitSipInverseEvaluations{0};
    std::atomic<std::uint64_t> m_sipNewtonFallbackIterations{0};
    std::atomic<std::uint64_t> m_polynomialCoordinatesEvaluated{0};
    std::atomic<std::uint64_t> m_sharedPolynomialPowerTableBuilds{0};
    std::atomic<std::uint64_t> m_lazyBlocksGenerated{0};
    std::atomic<std::uint64_t> m_lazyCoordinatePairsGenerated{0};
    std::atomic<std::uint64_t> m_peakLazyCoordinateBytes{0};
    std::atomic<std::uint64_t> m_denseFallbackCount{0};
    std::atomic<std::uint64_t> m_lazyMapCancellations{0};
    std::atomic<std::uint64_t> m_lazyMapFailures{0};
    std::atomic<std::uint64_t> m_stackLoaderRequests{0};
    std::atomic<std::uint64_t> m_roiSubsetReads{0};
    std::atomic<std::uint64_t> m_fullFrameFallbackReads{0};
    std::atomic<std::uint64_t> m_logicalDecodedPixelBytes{0};
    std::atomic<std::uint64_t> m_calibrationInvocations{0};
    std::atomic<std::uint64_t> m_calibratedSpillFilesCreated{0};
    std::atomic<std::uint64_t> m_calibratedSpillBytesWritten{0};
    std::atomic<std::uint64_t> m_calibratedSpillBytesRead{0};
    std::atomic<std::uint64_t> m_calibratedSpillRoiReads{0};
    std::atomic<std::uint64_t> m_calibratedSpillCleanupSuccesses{0};
    std::atomic<std::uint64_t> m_calibratedSpillCleanupFailures{0};
};
