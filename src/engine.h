#ifndef EMACS_ENGINE_H
#define EMACS_ENGINE_H

#include "lisp.h"
#include "systhread.h"

struct engine_message {
  Lisp_Object function;
  Lisp_Object callback;
  struct engine_message *next;
};

struct message_queue {
  sys_mutex_t lock;
  sys_cond_t cond;
  struct engine_message *head;
  struct engine_message *tail;
};

struct Lisp_Engine {
  int engine_id;
  bool is_ui_engine;
  bool active;

  /* The active thread in this engine */
  struct thread_state *active_thread;
  struct thread_state *all_threads;

  struct message_queue incoming_queue;
  struct Lisp_Engine *next;
};

/* Global pointer to the main UI engine */
extern struct Lisp_Engine *main_ui_engine;

/* Global lock for the engine list */
extern sys_mutex_t engine_list_lock;
extern struct Lisp_Engine *engine_list;

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

/* Process callbacks sent to the UI engine */
extern void process_engine_callbacks (void);

/* Spawn a new worker engine */
extern int spawn_lisp_engine (void);

/* Mark engines and their message queues for GC */
extern void mark_engines (void);

/* Periodically yield global lock in worker engines */
extern void engine_maybe_yield (void);

/* Initialize Lisp symbols and functions of the engine module */
extern void syms_of_engine (void);

#endif /* EMACS_ENGINE_H */
