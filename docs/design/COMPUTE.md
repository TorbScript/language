# Tensors, Gradients and the GPU

**Status: planned** — none of `std/tensor`, `std/gradient`, `std/gpu` or `std/learning` exists. `std/linear` and
`std/geometry` do ([LINEAR.md](LINEAR.md)). This record is a stub that keeps the plan decided on 2026-09-21; its full
design, with research into JAX, PyTorch, Burn, tinygrad, Mojo and Swift for TensorFlow, comes before the first of
these packages is built.

**Training and inference are possible with the standard library, and they stand on the same foundation as geometry
and the game engine of [ECS.md](ECS.md) rather than on a tower of their own.** Four layers, each of which is useful
without the ones above it.

## 1. The layers

**Layer 0 - the language and the runtime, shared with the engine.**

- `Buffer<Item>`, the heap storage primitive ([COLLECTIONS.md](COLLECTIONS.md), gap 6): contiguous memory, an
  in-place write where there is one owner, disjoint `var` windows.
- The trait `Real` of `std/linear`, and the small floating point types `Float16` and `BFloat16` beside `Float32`.
- Foreign functions, which the graphics interfaces need as well (CONCEPT, "Foreign Functions").
- Data-parallel loops over disjoint windows of a buffer, the same gap as parallel systems in an ECS
  ([CONCURRENCY.md](CONCURRENCY.md)).
- SIMD in the C back end.

**Layer 1 - pure values.** `std/linear` and `std/geometry` (small fixed vectors and matrices, which exist),
`std/tensor` (n-dimensional, broadcasting, slices as windows, on `Buffer<Item>`; the shape is checked at run time,
while `Matrix<Scalar, Rows, Columns>` stays in `std/linear` with its size in the type), [`std/random`](RANDOM.md)
(seedable and splittable, for reproducible training) and `std/statistics`. Value semantics fit: a tensor is a value,
and a change through a `var` path with a single owner happens without a copy, which is what JAX needs "donation" for.

**Layer 2 - differentiation and computation.**

- `std/gradient`: **forward mode as `Dual<Scalar>` with `Real`**, so that every function generic over `Real` -
  geometry, animation, physics included - can be differentiated without being written for it. **Reverse mode through
  quoted expressions**: `gradient { x => ... }` receives the expression tree, as the query provider of
  `examples/query-provider` does. Quoted expressions are the language's one mechanism of this kind, and they replace
  macros and tracing.
- `std/gpu`: an abstract layer for computation and drawing in the model of WebGPU, mapped onto Direct3D, Metal, Vulkan
  and WebGPU. The same path - a quoted expression becomes a kernel or a shader - serves `std/render` and `std/tensor`.

**Layer 3 - applications.** `std/learning`: layers, optimizers, losses and the training loop, with the data arriving
as a `Source<Batch, Failure>`, so the stream protocol of [STREAMS.md](STREAMS.md) is the data pipeline. Model formats
(safetensors, ONNX, GGUF) through the encoding layer ([ENCODING.md](ENCODING.md)); quantized inference over integer
and `Fixed` scalars. Beside it, on the same layers: `std/ecs`, `std/render`, `std/collision`, `std/animation`.

## 2. The order

`std/linear` and `std/geometry` (they bring `Real`; done), then `Buffer<Item>`, then `std/tensor` on the CPU and
`Dual`, which are pure TorbScript without foreign functions; then the design of foreign functions; then `std/gpu`;
then reverse mode, `std/learning`, `std/render` and `std/ecs`.

## 3. What these packages will test in the language

- Arithmetic on size parameters (`Rows * 2`): probably not, so the shape moves to run time where it has to.
- Operator traits with a foreign `Other` and `Output` (a tensor times a scalar).
- Buffers beyond 2^31 elements, and memory-mapped files.
- Quoted expressions that contain control flow.
- A guaranteed in-place change where there is one owner.
