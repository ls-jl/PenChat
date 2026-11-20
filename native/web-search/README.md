# PenChat Web Search component

Edge Search **0.14.0**, MIT, is a separately built native worker. The LVGL app
uses a protocol-neutral tool registry; API adapters only translate calls/results.
There is no shell, browser engine, JavaScript execution or code-execution tool.

## Updating and building

`upstream/` contains maintenance source and licenses only. `source-lock.json`
verifies every original file. Review a component update in that directory, update
the lock, then review/rebase `patches/` separately. Never edit `.deps/edge-search`.
The native transport's complete source inventory is
`upstream/tools/impersonate-sources.json`; XML and prepared-bundle hashes live in
`dependencies.lock.json`. Prepared sources and all binaries remain ignored.

```sh
bash scripts/build-web-search.sh host
bash scripts/build-web-search.sh rk3562
bash scripts/build-web-search.sh cv1826-s
```

The default is source-built **curl-impersonate 2.3.2-edge2**, privately namespaced.
Each target gets its own compiler, CRT, XML and transport. Modern Clang and GCC12
C++ headers compile against the target SDK's glibc/GCC6 runtime. The small
`sdk_runtime.cpp` adapter and explicit stream constructors avoid newer runtime
symbols; do not substitute the unrelated glibc 2.38 cross experiments.
Set `PENCHAT_EDGE_CXX_HEADERS` to a compatible target GCC12 include directory;
the existing local SDK cache is the development default. Source download cache
can be selected with `PENCHAT_EDGE_SOURCE_CACHE`.

The worker lives at `assets/bin/penchat-web-search`, and its sole private TLS
library at `assets/web-search/lib/libedge-curl-impersonate.so.4`. No component OTA
is implemented. Replacing a component requires a reviewed application package.

### HTTPS trust store

The worker explicitly loads `assets/web-search/cacert.pem` relative to its own
executable, including search, source reading and image downloads. This is the
unchanged Mozilla public CA bundle converted by curl, dated 2026-11-05 (118 roots,
200,116 bytes). `trust/source.json` pins its official download, SHA-256, date and
MPL-2.0 attribution; `source-lock.json` verifies these resources before building.
The PEM, provenance and full MPL-2.0 license ship with both target payloads.
No build-host/private roots or certificate-verification bypass are included.
Missing/invalid resources produce a tool error while leaving the app running.
Update the dated upstream bundle and reviewed hashes together, then rebuild;
no runtime root download or automatic trust-store replacement is performed.

Focused resource checks: `python3 tests/test_web_trust.py`. The package component
check also verifies the exact bundled CA resource and notices.

## IPC and ownership

Protocol **1** uses a length-prefixed UTF-8 JSON frame on an inherited Unix
socketpair: 4-byte network-endian length, then JSON. Requests are at most 16KiB;
responses at most 4MiB. `hello` checks the component and protocol versions.
Every response repeats its monotonically increasing request ID. The worker never
receives Provider credentials, HTTP authentication headers or a database path.

One caller owns each browser instance. Tool browsing has a 6MiB retained cache;
manual source reading uses a separate 2MiB instance. These are retained-cache
limits, not RSS caps. Calls run outside LVGL. Cancellation/IPC EOF closes and
reaps the worker; SIGUSR1 also supports cooperative transfer cancellation through
the reviewed source overlay. No completed tool executes again on restart.

Tools: `web_search`, `web_open`, `web_click`, `web_read`, `web_find`,
`web_read_image`. Opens require discovered sources, reads use page IDs/byte
cursors, and image reads require a returned image link. Page IDs are ephemeral;
historical sources must be reopened. Host-only source reading and source seeding
are not advertised to the model. All network paths retain public-address checks,
HTTPS validation, body limits and the no-script policy.

Per answer: 8 model requests, 24 tool operations, 120 seconds accumulated tool
time, 15 seconds per operation, and the Provider's overall timeout. The final
model round has no tools. Calls are executed only after complete, valid arguments.
There is no automatic AI request replay or protocol/model fallback.

## Images and storage

`web_read_image` downloads an actual discovered image; optional `inspect` supplies
pixels only when the model declares vision support. Displaying an image does not
imply the model inspected it. At most four distinct images, 2MiB each, and the
existing 16MP decode bound. Images are named by SHA-256 in private assets.

The model embeds `![caption](penchat-image:img_N)`. References belong to one
assistant message, never identify arbitrary local files or trigger arbitrary URL
fetches. Replies retain the mapping, sources, public records and same-identity
protocol exchanges. Base64 is materialized only for the HTTP body, not saved in
metadata. Assets use ordinary ownership/refcount/GC and are excluded from system
album writes. Exported Markdown rewrites references to bundled relative files;
imports/copies rebuild asset IDs and preserve the message-scoped references.

See [integration guide](../../docs/WEB_SEARCH.md) for UI behavior and pending
manual acceptance. Third-party notices remain in `upstream/licenses/` and are
included with each target's transport notices in the package.
