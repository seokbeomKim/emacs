# Emacs Multi-Engine Pool Architecture Design & Implementation Plan

## 1. Goal
Overcome Emacs's cooperative global-lock threading model by introducing a "Shared-Nothing Multi-Engine Pool". This involves separating the Lisp virtual machine state into isolated engines (instances) so that CPU-heavy tasks can run in parallel on background engines without blocking the main UI event loop.

## 2. Core Concept
- **UI Engine (Engine 0):** The main thread responsible for the `keyboard.c` event loop, `xdisp.c` redisplay, and responding to basic user inputs. It retains access to the global buffer list, but uses a non-blocking queue to receive updates from workers.
- **Worker Engines (Engine 1..N):** Background threads running independent Lisp VMs. Each has its own:
  - Garbage Collector (`alloc.c` isolation)
  - Memory Heap (No direct pointers shared with the UI engine)
  - Lisp Context (`current_thread`, `specpdl`, `catchlist`, etc.)
- **Message Passing:** Engines communicate via a serialized message queue. A worker calculates a result, serializes it (e.g., S-expression string or specialized IPC format), and posts it to the UI Engine's inbox.

## 3. Structural Changes Required

### Phase 1: Contextualize the Global Thread State (C-Level Refactoring)
Currently, `current_thread` is a global pointer, and there is a `global_lock` in `thread.c`.
- Introduce `struct Lisp_Engine` inside `engine.h`.
- `struct Lisp_Engine` wraps a group of threads or a single primary execution thread, its own heap allocator context, and its own obarray.
- Change `current_thread` from a simple global pointer to a thread-local variable (`_Thread_local struct thread_state *current_thread;` or derived from `current_engine`).
- Remove or bypass `sys_mutex_lock(&global_lock)` for worker engines, allowing them to run concurrently.

### Phase 2: Isolate the Garbage Collector (`alloc.c`)
The `garbage_collect()` function currently stops the world and scans all threads (`mark_threads()`).
- Add an `engine_id` to objects or segregate memory arenas per engine.
- Modify `garbage_collect()` to only scan the heap and stack of the calling `Lisp_Engine`.
- Disable cross-engine pointers. All shared data must be deep-copied or strictly read-only.

### Phase 3: Implement the Inter-Engine Queue & Event Loop Integration
- Implement a lock-free or mutex-guarded Message Queue in C.
- Modify `read_char()` in `keyboard.c` to poll the `main_ui_engine->incoming_queue`.
- When a message arrives (e.g., "Insert text 'foo' into buffer 'bar'"), the UI engine executes it in the main context.

### Phase 4: Expose Elisp API
- Create new built-in functions:
  - `(engine-run-async FUNCTION CALLBACK)`
  - `(engine-pool-status)`
- Create `engine.el` to wrap these primitives into an easy-to-use promise/async-await style API.

## 4. Targeted Prototyping (Current Action)
Instead of rewriting all of Emacs at once, we will create a minimally viable C-level skeleton:
1. Define `struct Lisp_Engine` in a new header `src/engine.h`.
2. Modify `thread.h` and `thread.c` to define thread-local engines.
3. Add a placeholder queue and worker spawn logic.
