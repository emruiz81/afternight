# Numbers, validity and metadata

What an image is in this engine, and the rules that keep its samples, their meaning and their history
from being confused with one another. None of it is inferred from a C++ type or a flag: every part is
stated, and a combination that cannot describe one image is refused.

## One image, three separate facts

An image is **its samples, what they mean, and what has been done to them**, and each is held
separately:

| Fact | Type | Header |
|---|---|---|
| Where the samples are | `ImageLayout` - extent, element size, row and plane strides, alignment - and `StorageArrangement` | `Core/ImageLayout.h`, `Image/StorageArrangement.h` |
| What they are | `ImageDescriptor` - extent, `SampleType`, channel schema, colour space, numeric domain, filter array, coordinate origin, pixel aspect | `Image/ImageDescriptor.h` |
| What has been done | `ImageMetadata` - acquisition, semantics, astrometric solution, provenance, codec cards, output intent | `Image/ImageMetadata.h` |

`Image<SampleT>` (`Image/Image.h`) holds owned storage, a descriptor, metadata and two generation
counters, and nothing else: no algorithm methods and no implicit conversion of any kind - not between
sample types, not to a view, not to its storage. Reading is free. `write()` returns a write handle and
moves the **pixel generation** whether or not a sample changes; the **semantic generation** moves when
the description, the processing meaning or the astrometric solution changes. What combines the two
into a cache key belongs to whoever owns the cache. There are two counters because a change of meaning
invalidates a derived result without moving a pixel, and only the cache's owner knows the selection,
the parameters and the other inputs its key also needs. Write access always moves the pixel
generation: a needless recomputation costs time, and a stale hit costs a wrong answer.

A descriptor has no setters. To change one, take `toRequest()`, change the request and build a new
descriptor, which checks the combination again and names a refusal with a `DescriptorError`.
`Image/DescriptorText.h` is the lossless one-line text form that fixtures and failure messages use.

## Samples

**Eight resident sample types**, closed: `UInt8`, `Int8`, `UInt16`, `Int16`, `UInt32`, `Int32`,
`Float32`, `Float64` (`Image/SampleType.h`). `Image/SampleTraits.h` is the two-way table between a
`SampleType` and its C++ type, and proves at compile time that it agrees with `Image/SampleType.h`.
All eight share one untyped storage, `ImageStorageBytes`, so an image's memory is not a template
instantiation per type.

**Storage is owned, counted and released** (`Image/ImageStorage.h`). Its bytes come from the engine
context's memory resource and it holds a lease on that context. What the samples start as is a stated
`StorageInitialization` - `Uninitialized` or `Zeroed` - and never assumed. `reshape` re-describes
retained capacity without reaching the allocator, so capacity and logical size are different numbers.

**Views borrow and are checked once** (`Image/PlaneView.h`, `Image/ImageView.h`). A view pairs a typed
pointer with a validated layout - element size, buffer size and base alignment are checked when the
view is made - and a row is then a `std::span` that stops before the padding. Constness lives in the
sample type, as in `std::span`: `ImageView<const float>` is read-only. `viewsOverlap` answers whether
two views may alias, conservatively.

**Frames arriving from outside are described, not reinterpreted** (`Image/RawFrame.h`). A decoded
frame states its representation, byte order, geometry and meaning. `Image/RawSampleFormat.h` is a
width and a class, and whether a format is one of the eight resident types is derived from a closed
table: a 64-bit integer frame is carried and refused entry to the processing model rather than
silently converted. `Image/SampleBuffer.h` says who frees the bytes - `Owned` engine memory (the only
kind an image can adopt with no copy), `Mapped` and `DecoderOwned` bytes released through a lifetime
token, and `Borrowed` bytes that release nothing - and `checkAdoptable` names which of three things
blocks a zero-copy adoption.

## Precision

There is **one numeric contract** and no strict, reproducible or fast mode to choose between: one
contract keeps one set of reference outputs, so reproducing a defect never depends on a flag. Global
fast-math is prohibited, because whether a value is finite is meaningful here.

**Arithmetic is Float32 by default** (`Image/ComputePrecision.h`). Float64 is used only with one of six
stated reasons - `StoredInFloat64`, `ExactIntegerCodeValues`, `CalibrationDivide`,
`StackingAccumulation`, `ReductionAccumulator`, `AstrometricFitting` - and Float32 is refused where the
storage and the output promise need more (`PrecisionBelowContract`). `WorkingImage` and
`createWorkingImage` are how an operation states that it computes in float or double instead of
inheriting whatever the input happened to be. Legitimate negative and above-one intermediate values in
a floating working image are kept; nothing adds a clamp the caller did not ask for. Float64 is named
per use because a double vector holds half the lanes of a float one; the calibration divide, bound by
memory bandwidth at frame scale, pays about four per cent for it.

**Reductions that feed a decision are deterministic across thread counts.** A rejection threshold or a
normalisation factor is reduced over `deterministicChunkCount` chunks, a number that does not depend on
how many workers run them, so the same input gives the same answer at one thread and at thirty-two.
A reduction that is only reported may reassociate within its recorded bound. `ReductionDeterminism::All`
extends the guarantee to those as well by admitting the operation on exactly one worker, at a cost in
throughput (`Runtime/ExecutionPolicy.h`).

**Approximations are named.** A faster approximation of a transcendental has a named policy and an
error bound (`mathUlpBound` in `Algorithms/Kernel/MathKernel.h`); nothing becomes less accurate
because it became faster.

## Conversion

A conversion between sample types is **five named decisions, and none of them is a cast**
(`Image/ConversionPolicy.h`):

| Decision | Enumeration | Default |
|---|---|---|
| Rounding | `RoundingMode` | `NearestEven` |
| Out-of-range values | `RangePolicy` | `Saturate`, before converting |
| NaN and the infinities | `NonFinitePolicy` | `MapToRange`: NaN to zero, the infinities to the destination's extremes |
| Relating the two ranges | `DomainMapping` | `FullScale`: corresponding full scales |
| Signed integers | `SignedIntegerConvention` | `TwosComplement` |

`Image/SampleConversion.h` is the complete scalar matrix behind those rules. It computes in `double`,
which is exact for all six integer types; a `float` scale factor such as `float(UINT32_MAX)` is not.
`Image/ImageConversion.h` applies the same rules to whole images on the operation's threads,
refuses an aliasing destination, reports counters (`ConversionCounters`), and keeps repacking and
byte-order swapping as separate calls. Forcing `ComputeBackend::Reference` runs the scalar matrix and
leaving the backend automatic runs the compiled one, which is what makes the differential tests a
comparison. Rounding is to nearest because truncation would bias every conversion half a code low, an
offset that accumulates over a stack; non-finite and out-of-range values are mapped by rule because
x86 converts them - and calibration produces NaN wherever a flat is zero - to the most negative
integer; and full-scale mapping makes the round trip of 8- and 16-bit codes through Float32 exact.

The **numeric domain** says how values relate to light and is part of the descriptor, not the sample
type: `NormalizedLinear`, `EncodedNonlinear`, `RawSensorCode`, `PhysicalScaled`. A Float32 image can be
any of them, and an interop adapter refuses a difference between two domains by name rather than
rescaling.

## Validity and no-data

**Which samples mean something is stated** (`Image/ValidityDescriptor.h`), as one of five rules held
in the image's semantics:

| Rule | Meaning |
|---|---|
| `AllValid` | Every sample is data. The default. |
| `FiniteOnly` | A non-finite sample is not data. |
| `ZeroIsNoData` | An exact zero is not data - a compatibility convention some real data and stacking paths use, asked for by name and never acquired |
| `ExplicitMask` | A packed mask travelling beside the image says which samples are data |
| `DomainRange` | Samples inside a stated closed range are data |

Nothing infers a rule from the pixels. Because the rule is part of the semantics, changing it moves
the semantic generation.

**A mask is materialised only when a rule needs one** (`Image/ValidityMask.h`,
`Image/ValidityMaskView.h`): one bit per pixel per plane, set meaning valid, the bits past a row's last
pixel held at zero by every write, and memory taken from the engine context. It travels beside the
image rather than inside it.

**Every operation declares what it does to validity** - `Preserve`, `Combine`, `Regenerate` or
`Reject` - and `applyValidityPolicy` honours the declaration; two inputs whose rules disagree combine
to the mask rule and cannot be preserved. A reduction over no valid sample reports
`ErrorCode::EmptySelection` rather than a number. A mask is opt-in because every kernel must honour
one, which doubles the branches in its inner loop and the cases its tests cover; and no rule is
inferred, because a black sky and an unwritten pixel are the same bits.

## Metadata

**Metadata is six typed objects travelling as one** (`Image/ImageMetadata.h`):

| Object | Header | Holds |
|---|---|---|
| `AcquisitionMetadata` | `Image/AcquisitionMetadata.h` | What the instrument was doing. Fixed for the life of the image. |
| `ImageSemantics` | `Image/ImageSemantics.h` | The transfer function, the `ProcessingState` flags, and the validity rule |
| `AstrometricSolution` | `Image/AstrometricSolution.h` | Where the image points on the sky, when that is known |
| `Provenance` | `Image/Provenance.h` | Append-only lineage. It never participates in a computation. |
| `CodecMetadata` | `Image/CodecMetadata.h` | The container's own cards, carried for a writer of the same container |
| `OutputIntent` | `Image/OutputIntent.h` | Optional, and absent until something decides how the image is written |

Each object is typed because an untyped map of cards cannot tell a source fact from an output
decision, and lineage never takes part in a computation, because lineage that changed behaviour would
be hidden state.

**Metadata changes only through a stated transition.** `MetadataTransition` has no field for the
acquisition record, and a transition that carries no lineage entry is refused. `changesMeaning` names
the two objects - semantics and astrometric solution - whose change moves the semantic generation.
`Image::transition` is the only door, so an operation that changes what an image means says so in
its provenance. The cards go through it too: an operation drops them (`clearCodecMetadata`) or states
the cards the image carries from then on (`codec`) - a product recording on its processing-state
cards what its samples now are - so a result whose cards change keeps its samples where they are
rather than being copied into an image created with new metadata.

**`OutputIntent` is absent until someone decides it.** Keeping it optional is what stops a fact about
the source file - its bit depth, its compression - from turning into a decision about the output
without anyone having made that decision.

**Artifact identity is the writer's**: an output file carries the identity of the run that wrote it,
never the identity of its input (`IO/ArtifactIdentity.h`), and a file is written to a pending sibling
and renamed into place only when complete (`IO/OutputTransaction.h`).
