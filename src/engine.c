#include <config.h>
#include "lisp.h"
#include "engine.h"
#include "systhread.h"
#include "character.h"
#include "buffer.h"

struct Lisp_Engine *main_ui_engine;

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Thread_local struct Lisp_Engine *current_engine;
#elif defined (__GNUC__) || defined (__INTEL_COMPILER) || defined (__SUNPRO_C)
__thread struct Lisp_Engine *current_engine;
#endif

static void
enqueue_message (struct message_queue *q, Lisp_Object function, Lisp_Object callback)
{
  struct engine_message *msg = xzalloc (sizeof (struct engine_message));
  msg->function = function;
  msg->callback = callback;
  msg->next = NULL;

  sys_mutex_lock (&q->lock);
  if (q->tail)
    {
      q->tail->next = msg;
      q->tail = msg;
    }
  else
    {
      q->head = msg;
      q->tail = msg;
    }
  sys_cond_signal (&q->cond);
  sys_mutex_unlock (&q->lock);
}

static struct engine_message *
dequeue_message (struct message_queue *q)
{
  sys_mutex_lock (&q->lock);
  while (!q->head)
    sys_cond_wait (&q->cond, &q->lock);

  struct engine_message *msg = q->head;
  q->head = msg->next;
  if (!q->head)
    q->tail = NULL;
  sys_mutex_unlock (&q->lock);

  return msg;
}

static void *
run_worker_engine (void *arg)
{
  struct Lisp_Engine *engine = arg;
  current_engine = engine;

  /* Align stack bottom and stack top for GC */
  union {
    Lisp_Object o;
    void *p;
    char c;
  } stack_pos;

  struct thread_state *thr = xzalloc (sizeof (struct thread_state));
  thr->m_lisp_eval_depth = 0;
  thr->m_stack_bottom = thr->stack_top = &stack_pos.c;

  /* Allocate small specpdl binding stack for this thread */
  ptrdiff_t size = 50;
  union specbinding *pdlvec = xmalloc ((1 + size) * sizeof (union specbinding));
  thr->m_specpdl = pdlvec + 1;
  thr->m_specpdl_end = thr->m_specpdl + size;
  thr->m_specpdl_ptr = thr->m_specpdl;

  /* Link to thread list */
  all_threads = thr;
  current_thread = thr;
  engine->active_thread = thr;
  engine->all_threads = thr;

  /* Put a dummy catcher at top-level so that handlerlist is never NULL.
     This is important since handlerlist->nextfree holds the freelist
     which would otherwise leak every time we unwind back to top-level. */
  struct handler *sentinel = xzalloc (sizeof (struct handler));
  thr->m_handlerlist = sentinel->nextfree = sentinel;
  thr->m_handlerlist_sentinel = sentinel;

  acquire_global_lock (thr);
  struct handler *c = push_handler (Qunbound, CATCHER);
  eassert (c == sentinel);
  sentinel->nextfree = NULL;
  sentinel->next = NULL;
  release_global_lock ();

  while (engine->active)
    {
      struct engine_message *msg = dequeue_message (&engine->incoming_queue);
      if (msg)
        {
          /* Acquire the global lock before any Lisp allocations or execution */
          acquire_global_lock (thr);

          if (NILP (thr->name))
            thr->name = build_string ("worker-thread");
          thr->m_current_buffer = main_ui_engine->active_thread->m_current_buffer;

          /* Execute function in worker engine thread-local context.
             Using safe_calln ensures a clean unwind environment. */
          Lisp_Object result = safe_calln (msg->function);

          /* Release global lock after Lisp execution is done */
          release_global_lock ();

          /* If there's a callback, post the result back to the UI engine queue */
          if (!NILP (msg->callback))
            {
              enqueue_message (&main_ui_engine->incoming_queue, msg->callback, result);
            }
          xfree (msg);
        }
    }

  return NULL;
}

void
init_engines (void)
{
  main_ui_engine = xzalloc (sizeof (struct Lisp_Engine));
  main_ui_engine->engine_id = 0;
  main_ui_engine->is_ui_engine = true;
  main_ui_engine->active = true;

  sys_mutex_init (&main_ui_engine->incoming_queue.lock);
  sys_cond_init (&main_ui_engine->incoming_queue.cond);

  main_ui_engine->active_thread = current_thread;
  main_ui_engine->all_threads = all_threads;

  current_engine = main_ui_engine;
}

void
process_engine_callbacks (void)
{
  if (!main_ui_engine)
    return;

  struct message_queue *q = &main_ui_engine->incoming_queue;

  /* Quick lock-free check */
  if (!q->head)
    return;

  sys_mutex_lock (&q->lock);
  struct engine_message *msg = q->head;
  q->head = NULL;
  q->tail = NULL;
  sys_mutex_unlock (&q->lock);

  while (msg)
    {
      struct engine_message *next = msg->next;

      if (!NILP (msg->function))
        {
          safe_calln (msg->function, msg->callback);
        }

      xfree (msg);
      msg = next;
    }
}

int
spawn_lisp_engine (void)
{
  static int next_engine_id = 1;
  struct Lisp_Engine *engine = xzalloc (sizeof (struct Lisp_Engine));
  engine->engine_id = next_engine_id++;
  engine->is_ui_engine = false;
  engine->active = true;

  sys_mutex_init (&engine->incoming_queue.lock);
  sys_cond_init (&engine->incoming_queue.cond);

  sys_thread_t thr;
  if (!sys_thread_create (&thr, run_worker_engine, engine))
    {
      xfree (engine);
      return -1;
    }

  return engine->engine_id;
}

/* Elisp Primitives */

DEFUN ("engine-run-async", Fengine_run_async, Sengine_run_async, 1, 2, 0,
       doc: /* Run FUNCTION asynchronously on a background worker engine.
If CALLBACK is provided, it will be executed in the UI engine with the result. */)
  (Lisp_Object function, Lisp_Object callback)
{
  /* Auto-spawn first worker engine if none exist yet */
  static int worker_spawned = 0;
  static struct Lisp_Engine *worker_engine = NULL;

  if (!worker_spawned)
    {
      worker_engine = xzalloc (sizeof (struct Lisp_Engine));
      worker_engine->engine_id = 1;
      worker_engine->is_ui_engine = false;
      worker_engine->active = true;

      sys_mutex_init (&worker_engine->incoming_queue.lock);
      sys_cond_init (&worker_engine->incoming_queue.cond);

      sys_thread_t thr;
      if (sys_thread_create (&thr, run_worker_engine, worker_engine))
        {
          worker_spawned = 1;
        }
      else
        {
          xfree (worker_engine);
          error ("Could not spawn worker engine");
        }
    }

  if (worker_spawned)
    {
      enqueue_message (&worker_engine->incoming_queue, function, callback);
      return Qt;
    }

  return Qnil;
}

DEFUN ("engine-pool-status", Fengine_pool_status, Sengine_pool_status, 0, 0, 0,
       doc: /* Return a list containing status info of the engine pool. */)
  (void)
{
  Lisp_Object status = list1 (build_string ("Multi-Engine Pool Active"));
  return status;
}

DEFUN ("process-engine-callbacks", Fprocess_engine_callbacks, Sprocess_engine_callbacks, 0, 0, 0,
       doc: /* Manually process any pending engine callbacks on the main thread. */)
  (void)
{
  process_engine_callbacks ();
  return Qnil;
}

void
syms_of_engine (void)
{
  defsubr (&Sengine_run_async);
  defsubr (&Sengine_pool_status);
  defsubr (&Sprocess_engine_callbacks);
}
