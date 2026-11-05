// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What must hold of every byte string an extension host could send the application, or the application a host.
///
/// The extension protocol is the one boundary the product reads from another process: a host runs code a person
/// installed, and everything it says arrives as CBOR over a local socket, framed by a length the sender states. Its
/// library is the Apache-2.0 half of the licensing boundary and links nothing of the engine, so this target links it
/// and Qt's core alone.
///
/// The claims:
///
/// * **A decoder answers.** A message, a frame, a buffer's description and an error record each decode or are refused
///   with a reason; none may crash, and none may succeed without the value it promised.
/// * **What decoded encodes, and what it encodes decodes the same.** A message read off the wire serializes, the bytes
///   read back as a message, and serializing that gives the same bytes again; a buffer's description written as CBOR
///   reads back as one. A typed message - a hello, an execute, a commit, a result - decoded from a message re-encodes
///   into a message the same typed decoder accepts.
/// * **Framing consumes or stops.** Consuming frames out of a buffer either takes a whole frame, waits for more bytes
///   without taking any, or refuses the frame; it never takes more bytes than were there and never loops forever.

#include "../FuzzEngine/FuzzHarness.h"

#include "AfterNightExtensionProtocol.h"

#include <QByteArray>
#include <QCborMap>
#include <QCborValue>

#include <cstdint>
#include <string>
#include <vector>

namespace an::fuzz
{
namespace
{

namespace protocol = an::ui::extensions;

/// The modes a seed's first byte selects.
enum Mode : std::uint8_t
{
    Message = 0,
    Frames = 1,
    BufferDescriptor = 2,
    ErrorRecord = 3,
    ModeCount = 4
};

/// A buffer's description of the shape the application lends: a grey Float32 frame.
[[nodiscard]] protocol::SharedBufferDescriptor sourceBuffer()
{
    protocol::SharedBufferDescriptor descriptor;
    descriptor.buffer_id = "source";
    descriptor.symbolic_name = "source";
    descriptor.mapping_name = "session-1.source";
    descriptor.mapped_file_path = "buffers/session-1/source.bin";
    descriptor.width = 8;
    descriptor.height = 4;
    descriptor.channels = 1;
    descriptor.row_stride = 8u * sizeof(float);
    descriptor.plane_stride = descriptor.row_stride * 4u;
    descriptor.byte_size = descriptor.plane_stride;
    return descriptor;
}

[[nodiscard]] std::vector<std::uint8_t> seed(Mode mode, const QByteArray& payload)
{
    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(mode)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

[[nodiscard]] std::vector<protocol::ExtensionProtocolMessage> sampleMessages()
{
    std::vector<protocol::ExtensionProtocolMessage> messages;
    protocol::RuntimeHelloMessage hello;
    hello.extension_id = "an.demo";
    hello.session_id = "session-1";
    hello.host_version = "1.0.0";
    hello.capabilities = {"execute", "preview"};
    messages.push_back(protocol::toProtocolMessage(hello));
    protocol::ExecutionExecuteMessage execute;
    execute.session_id = "session-1";
    execute.request_id = "request-1";
    execute.params_token = "params-1";
    execute.params.insert(QStringLiteral("strength"), 0.5);
    execute.buffers = {sourceBuffer()};
    messages.push_back(protocol::toProtocolMessage(execute));
    protocol::ExecutionPreviewMessage previewRequest;
    previewRequest.session_id = "session-1";
    previewRequest.request_id = "request-5";
    previewRequest.params_token = "params-2";
    previewRequest.params.insert(QStringLiteral("strength"), 0.25);
    previewRequest.buffers = {sourceBuffer()};
    messages.push_back(protocol::toProtocolMessage(previewRequest));
    protocol::ExecutionCancelMessage cancel;
    cancel.session_id = "session-1";
    cancel.request_id = "request-2";
    cancel.target_request_id = "request-1";
    messages.push_back(protocol::toProtocolMessage(cancel));
    protocol::ExecutionResultMessage result;
    result.session_id = "session-1";
    result.request_id = "request-3";
    result.target_request_id = "request-1";
    result.result_kind = "preview";
    result.status = "succeeded";
    protocol::SharedBufferDescriptor preview = sourceBuffer();
    preview.buffer_role = protocol::SharedBufferRole::Preview;
    preview.access_mode = protocol::SharedBufferAccessMode::ReadWrite;
    result.output_buffers = {preview};
    messages.push_back(protocol::toProtocolMessage(result));
    protocol::ViewCommitMessage commit;
    commit.session_id = "session-1";
    commit.request_id = "request-4";
    commit.history_step_name = "Demo";
    protocol::SharedBufferDescriptor destination = sourceBuffer();
    destination.buffer_role = protocol::SharedBufferRole::Destination;
    destination.access_mode = protocol::SharedBufferAccessMode::ReadWrite;
    commit.result_buffers = {destination};
    messages.push_back(protocol::toProtocolMessage(commit));
    return messages;
}

/// Runs a typed decoder; when it accepts, re-encodes the typed message and requires the decoder to accept that too.
template <typename TypedT, typename DecodeT>
void checkTyped(const protocol::ExtensionProtocolMessage& message, DecodeT decode, const char* accepted)
{
    TypedT typed;
    std::string error;
    const bool ok = decode(message, &typed, &error);
    if (!ok)
    {
        return;
    }
    countReached(accepted);
    const protocol::ExtensionProtocolMessage again = protocol::toProtocolMessage(typed);
    TypedT reread;
    std::string rereadError;
    AN_FUZZ_CHECK(decode(again, &reread, &rereadError), "a typed message re-encoded from one it accepted is refused");
}

void checkMessage(const QByteArray& bytes)
{
    protocol::ExtensionProtocolMessage message;
    std::string error;
    if (!protocol::deserializeProtocolMessage(bytes, &message, &error))
    {
        AN_FUZZ_CHECK(!error.empty(), "a message refused without a reason");
        countReached("messages-refused");
        return;
    }
    countReached("messages-read");
    std::string serializeError;
    const QByteArray serialized = protocol::serializeProtocolMessage(message, &serializeError);
    if (serialized.isEmpty())
    {
        // A message this build reads and cannot write back is refused at serialization with a reason.
        AN_FUZZ_CHECK(!serializeError.empty(), "a message that cannot be written back says nothing");
        countReached("messages-not-rewritable");
        return;
    }
    protocol::ExtensionProtocolMessage reread;
    std::string rereadError;
    AN_FUZZ_CHECK(protocol::deserializeProtocolMessage(serialized, &reread, &rereadError),
                  "a message serialized from one that was read cannot be read");
    AN_FUZZ_CHECK(protocol::serializeProtocolMessage(reread) == serialized,
                  "a message read back serializes otherwise than it did");
    countReached("messages-round-tripped");

    checkTyped<protocol::RuntimeHelloMessage>(message, protocol::runtimeHelloFromProtocolMessage, "typed-hello");
    checkTyped<protocol::ExecutionExecuteMessage>(message, protocol::executionExecuteFromProtocolMessage,
                                                  "typed-execute");
    checkTyped<protocol::ExecutionPreviewMessage>(message, protocol::executionPreviewFromProtocolMessage,
                                                  "typed-preview");
    checkTyped<protocol::ExecutionCancelMessage>(message, protocol::executionCancelFromProtocolMessage, "typed-cancel");
    checkTyped<protocol::ExecutionResultMessage>(message, protocol::executionResultFromProtocolMessage, "typed-result");
    checkTyped<protocol::ViewCommitMessage>(message, protocol::viewCommitFromProtocolMessage, "typed-commit");
}

void checkFrames(const QByteArray& bytes)
{
    QByteArray buffered = bytes;
    // Each pass takes a frame, waits for more, or refuses: at most one pass per byte plus the last.
    for (qsizetype pass = 0; pass <= bytes.size(); ++pass)
    {
        const qsizetype before = buffered.size();
        protocol::ExtensionProtocolMessage message;
        std::string error;
        const protocol::ProtocolFrameDecodeStatus status =
            protocol::tryConsumeFramedProtocolMessage(&buffered, &message, &error);
        AN_FUZZ_CHECK(buffered.size() <= before, "consuming a frame grew the buffer");
        if (status == protocol::ProtocolFrameDecodeStatus::Complete)
        {
            AN_FUZZ_CHECK(buffered.size() < before, "a complete frame consumed nothing");
            countReached("frames-complete");
            continue;
        }
        if (status == protocol::ProtocolFrameDecodeStatus::NeedMoreData)
        {
            AN_FUZZ_CHECK(buffered.size() == before, "a frame waiting for more bytes took some");
            countReached("frames-waiting");
        }
        else
        {
            AN_FUZZ_CHECK(!error.empty(), "a frame refused without a reason");
            countReached("frames-refused");
        }
        return;
    }
    AN_FUZZ_CHECK(false, "consuming frames did not stop");
}

/// Decodes CBOR as every reader in the product does: a value Qt reports a parse error for is refused before anything
/// looks inside it. Qt hands back what it decoded before the error, and a map cut off inside a key is not safe to
/// search - a lookup in one reads past its data - so a structure the product never sees is not what is fuzzed here.
///
/// @param bytes what arrived.
/// @param value the value decoded, when the bytes were well-formed CBOR.
/// @return whether they were.
[[nodiscard]] bool decodeCbor(const QByteArray& bytes, QCborValue& value)
{
    QCborParserError parserError;
    value = QCborValue::fromCbor(bytes, &parserError);
    if (parserError.error != QCborError::NoError)
    {
        countReached("cbor-refused");
        return false;
    }
    return true;
}

void checkBufferDescriptor(const QByteArray& bytes)
{
    QCborValue value;
    if (!decodeCbor(bytes, value))
    {
        return;
    }
    protocol::SharedBufferDescriptor descriptor;
    std::string error;
    if (!protocol::sharedBufferDescriptorFromCborValue(value, &descriptor, &error))
    {
        AN_FUZZ_CHECK(!error.empty(), "a buffer's description refused without a reason");
        countReached("descriptors-refused");
        return;
    }
    countReached("descriptors-read");
    static_cast<void>(protocol::validateSharedBufferDescriptor(descriptor));
    protocol::SharedBufferDescriptor reread;
    std::string rereadError;
    AN_FUZZ_CHECK(protocol::sharedBufferDescriptorFromCborValue(
                      QCborValue(protocol::sharedBufferDescriptorToCborMap(descriptor)), &reread, &rereadError),
                  "a buffer's description written from one that was read cannot be read");
}

void checkErrorRecord(const QByteArray& bytes)
{
    QCborValue value;
    if (!decodeCbor(bytes, value))
    {
        return;
    }
    protocol::ExtensionProtocolError record;
    std::string error;
    if (!protocol::protocolErrorFromCborValue(value, &record, &error))
    {
        AN_FUZZ_CHECK(!error.empty(), "an error record refused without a reason");
        countReached("errors-refused");
        return;
    }
    countReached("errors-read");
    protocol::ExtensionProtocolError reread;
    std::string rereadError;
    AN_FUZZ_CHECK(protocol::protocolErrorFromCborValue(QCborValue(protocol::protocolErrorToCborMap(record)), &reread,
                                                       &rereadError),
                  "an error record written from one that was read cannot be read");
}

} // namespace

TargetInfo targetInfo()
{
    return {.name = "Fuzz_ConsumerProtocol", .maxInputBytes = 16384u};
}

std::vector<std::vector<std::uint8_t>> seedCorpus()
{
    std::vector<std::vector<std::uint8_t>> seeds;
    QByteArray stream;
    for (const protocol::ExtensionProtocolMessage& message : sampleMessages())
    {
        seeds.push_back(seed(Message, protocol::serializeProtocolMessage(message)));
        const QByteArray framed = protocol::frameProtocolMessage(message);
        seeds.push_back(seed(Frames, framed));
        stream.append(framed);
    }
    // Two frames back to back, and the second cut short.
    seeds.push_back(seed(Frames, stream.left(stream.size() - 3)));
    seeds.push_back(
        seed(BufferDescriptor, QCborValue(protocol::sharedBufferDescriptorToCborMap(sourceBuffer())).toCbor()));
    protocol::ExtensionProtocolError error;
    error.code = protocol::makeProtocolErrorCode(protocol::ExtensionProtocolErrorDomain::Buffers, 3);
    error.message = "the request names no buffer";
    seeds.push_back(seed(ErrorRecord, QCborValue(protocol::protocolErrorToCborMap(error)).toCbor()));
    return seeds;
}

} // namespace an::fuzz

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    an::fuzz::Input input(data, size);
    // The mode is taken from the front, so a seed's first byte names what follows it.
    const std::uint32_t mode = input.takeBelow(an::fuzz::ModeCount);
    const QByteArray bytes = QByteArray::fromStdString(input.restAsText());
    switch (mode)
    {
    case an::fuzz::Message:
        an::fuzz::checkMessage(bytes);
        break;
    case an::fuzz::Frames:
        an::fuzz::checkFrames(bytes);
        break;
    case an::fuzz::BufferDescriptor:
        an::fuzz::checkBufferDescriptor(bytes);
        break;
    default:
        an::fuzz::checkErrorRecord(bytes);
        break;
    }
    return 0;
}
