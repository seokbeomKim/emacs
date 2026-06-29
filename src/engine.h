#ifndef EMACS_ENGINE_H
#define EMACS_ENGINE_H

#include "lisp.h"
#include "systhread.h"

/*
 * Lisp_Engine represents an isolated Lisp Virtual Machine instance.
 * - Engine 0 is the main UI engine.
 * - Engines 1..N are worker engines for background processing.
 */
struct message_queue {
  /* Placeholder for inter-engine message queue */
  sys_mutex_t lock;
  sys_cond_t cond;
  void *head;
  void *tail;
};

struct Lisp_Engine {
  int engine_id;
  bool is_ui_engine;

  /* The active thread in this engine */
  struct thread_state *active_thread;
  struct thread_state *all_threads;

  /* In a full implementation, this would contain:
     - An isolated heap (struct emacs_heap)
     - A private obarray (Symbol table)
  */

  struct message_queue incoming_queue;
};

/* Global pointer to the main UI engine */
extern struct Lisp_Engine *main_ui_engine;

/* Thread-local pointer to the current engine */
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
extern _Thread_local struct Lisp_Engine *current_engine;
#elif defined (__GNUC__) || defined (__INTEL_COMPILER) || defined (__SUNPRO_C)
extern __thread struct Lisp_Engine *current_engine;
#else
#error "Thread-local storage is required for Multi-Engine support."
#endif

/* Initialize the multi-engine pool */
extern void init_engines (void);

/* Spawn a new worker engine */
extern int spawn_lisp_engine (void);

#endif /* EMACS_ENGINE_H */
