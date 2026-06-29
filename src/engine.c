#include <config.h>
#include "lisp.h"
#include "engine.h"

struct Lisp_Engine *main_ui_engine;

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Thread_local struct Lisp_Engine *current_engine;
#elif defined (__GNUC__) || defined (__INTEL_COMPILER) || defined (__SUNPRO_C)
__thread struct Lisp_Engine *current_engine;
#endif

void
init_engines (void)
{
  main_ui_engine = xzalloc (sizeof (struct Lisp_Engine));
  main_ui_engine->engine_id = 0;
  main_ui_engine->is_ui_engine = true;

  sys_mutex_init (&main_ui_engine->incoming_queue.lock);
  sys_cond_init (&main_ui_engine->incoming_queue.cond);

  /* Link to current main thread state */
  /* This requires thread.h to expose main_thread or current_thread during init */

  current_engine = main_ui_engine;
}

int
spawn_lisp_engine (void)
{
  /* Prototype: Spawns a background OS thread, sets up a fresh Lisp_Engine context,
     and enters a message-waiting loop. */
  return -1; /* Not yet implemented */
}
