# Language Models and Agents

**Status: planned** — nothing of it exists. It comes near the end of milestone 10 ([ROADMAP.md](../ROADMAP.md)),
after the network, the formats and the application framework, and before the engine packages. This record is a stub
that keeps the plan decided on 2026-09-24; the design comes before any of it is built.

**`std` brings clients for the language model APIs that matter, one protocol of messages and threads that every one
of them speaks, and an agent framework on top.** A program switches the provider of a thread by changing one value,
and a stream of a model's answer is an ordinary `Source`.

## 1. The guideline

- **Two packages.** `std/language-model` holds the protocol and the clients; `std/agent` holds the agent loop, the
  tools and the sessions. The agent package depends on the model package, never the reverse.
- **One protocol for every provider.** A message, a thread of messages, the content blocks inside a message (text,
  images, documents, tool calls, tool results, reasoning) and the usage of a request are types of `std/language-model`,
  not of one provider. Every client translates them to its API and back, so a thread started with one provider can be
  continued with another.
- **Streaming first.** A request answers a stream of events (a text delta, a tool call as it is written, the usage at
  the end) as a `Source` of [STREAMS.md](STREAMS.md); waiting for the whole answer is a reduction of that stream, not a
  second API. Cancelling the task that reads the stream cancels the request ([CONCURRENCY.md](CONCURRENCY.md)).
- **Clients for the important providers**: Anthropic, OpenAI and the APIs compatible with it (which covers most local
  servers), Google and Mistral, over `std/http` with the bodies of [ENCODING.md](ENCODING.md). Credentials come from
  the environment or a configuration, never from the code.
- **Tools are typed.** A tool is a TorbScript function whose parameter type is `Decode` and whose result is `Encode`;
  the JSON Schema the provider needs is derived from the type at compile time, not written by hand.
- **The agent framework follows pi** ([pi.dev](https://pi.dev)): a small agent loop - send the thread, run the tool
  calls, append their results, repeat until the model answers without one - that emits every step as an event, with
  sessions that can be saved, resumed and branched, and without a framework of chains and graphs around it.

## 2. What exists to build on

- [STREAMS.md](STREAMS.md) and [CONCURRENCY.md](CONCURRENCY.md) - the stream of events and the cancellation of a
  request.
- `std/http` and `std/json` - the transport and the bodies.
- [SCRIPTS.md](SCRIPTS.md) - a sandbox for code an agent writes and runs.

## 3. Before it is built

A design with **one conversation carried through every layer** - a thread with a tool call, streamed from one provider
and continued with another - and a comparison of the message models of the providers, so that the protocol fits all of
them before the first client exists.
