/****************************************************************************
 *
 *  File: ypmainloop.h
 *
 *  Copyright (C) 2008  Thomas Reitmayr <treitmayr@devbase.at>
 *
 ****************************************************************************
 *
 *  This file is part of Yeaphone.
 *
 *  Yeaphone is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

#include "ypmainloop.h"
#include "config.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif


#define INITIAL_EV_LIST_SIZE 10
#define TIMER_MIN_RESOLUTIN  10         /* in [ms] */


#define TIMEVAL_TO_MS(tv) (((tv)->tv_sec * 1000L) + ((tv)->tv_usec / 1000))
#define MS_TO_TIMEVAL(ms, tv) \
  do { \
    (tv)->tv_sec = (ms) / 1000; \
    (tv)->tv_usec = ((ms) % 1000) * 1000L; \
  } while (0)


enum event_type {
  EV_TYPE_EMPTY = 0,
  EV_TYPE_TIMER,
  EV_TYPE_PTIMER,
  EV_TYPE_IO
};

struct event_list {
  enum event_type type;
  int event_id;
  int group_id;
  int fd;
  struct timeval interval;
  struct timeval expire;
  yp_ml_callback callback;
  void *callback_data;
  int processed;
};

struct ml_data_s {
  struct event_list *ev_list;
  int ev_list_used;
  int ev_list_allocated;
  int event_id_max;

  fd_set select_master_set;
  int select_max_fd;
  
  int wakeup_read, wakeup_write;
  int is_running;
  int is_awake;
  pthread_t thread;
};

static struct ml_data_s ml_data;

/*****************************************************************/

int yp_ml_init()
{
  int fd[2];
  int ret = 0;

  /*if (ml_data.is_running) {
    fprintf(stderr, "Cannot call yp_ml_init while mainloop is running\n");
    abort();
  }*/

  memset(&ml_data, 0, sizeof(ml_data));

  /* preallocate event list */
  ml_data.ev_list_used = 0;
  ml_data.ev_list_allocated = INITIAL_EV_LIST_SIZE;
  ml_data.ev_list = calloc(ml_data.ev_list_allocated, sizeof(ml_data.ev_list[0]));
  if (!ml_data.ev_list) {
    fprintf(stderr, "Cannot allocate memory for event list\n");
    return -ENOMEM;
  }
  ml_data.event_id_max = 0;

  ret = pipe(fd);
  if (ret != 0) {
    perror("Cannot create internal pipe");
    free(ml_data.ev_list);
    return ret;
  }
  ml_data.wakeup_read = fd[0];
  ml_data.wakeup_write = fd[1];
  
  /* prepare bit fields for 'select' call */
  FD_SET(fd[0], &ml_data.select_master_set);
  ml_data.select_max_fd = fd[0] + 1;
  
  return 0;
}

/*****************************************************************/

int yp_ml_run()
{
  struct event_list *current;
  fd_set read_set;
  /*fd_set write_set;*/
  fd_set except_set;
  struct timeval tv, now;
  int ret, index, i;
  int result;

  if (ml_data.is_running) {
    fprintf(stderr, "mainloop is already running\n");
    return 0;
  }
  if (ml_data.select_max_fd == 0) {
    fprintf(stderr, "mainloop not initialized\n");
    return -EFAULT;
  }
  ml_data.is_running = 1;
  result = 0;
  ml_data.thread  = pthread_self();
  
  while (ml_data.is_running) {
    /* find the timer to expire next */
    tv.tv_sec = 0;
    current = ml_data.ev_list;
    for (i = 0; i < ml_data.ev_list_used; i++, current++) {
      if ((current->type == EV_TYPE_TIMER) ||
          (current->type == EV_TYPE_PTIMER)) {
        if ((tv.tv_sec == 0) ||
            timercmp(&current->expire, &tv, <)) {
          tv.tv_sec = current->expire.tv_sec;
          tv.tv_usec = current->expire.tv_usec;
        }
        /* reset all 'processed' flags (used below) */
        current->processed = 0;
      }
    }
    if (tv.tv_sec == 0) {
      /* no timer -> wait for 1 hour */
      tv.tv_sec = 3600;
      tv.tv_usec = 0;
    }
    else {
      /* calculate the duration to wait in 'select' */
      gettimeofday(&now, NULL);
      timersub(&tv, &now, &tv);      /* tv -= now */
      if ((tv.tv_sec < 0)  || (tv.tv_usec < 0)) {
        /* timer already expired -> 'select' will not wait */
        tv.tv_sec = 0;
        tv.tv_usec = 0;
      }
    }
    
    /* wait for a timer or io event */
    memcpy(&read_set, &ml_data.select_master_set, sizeof(fd_set));
    /*memcpy(&write_set, &ml_data.select_master_set, sizeof(fd_set));*/
    memcpy(&except_set, &ml_data.select_master_set, sizeof(fd_set));

    ret = select(ml_data.select_max_fd,
                 &read_set, NULL/*&write_set*/, &except_set, &tv);

    if (ret > 0) {
      /* io event */
      current = ml_data.ev_list;
      for (i = 0; i < ml_data.ev_list_used; i++, current++) {
        if ((current->type == EV_TYPE_IO) &&
            (current->callback != NULL) &&
            (FD_ISSET(current->fd, &read_set) ||
             FD_ISSET(current->fd, &except_set))) {
          current->callback(i, current->group_id, current->callback_data);
        }
      }
      if (FD_ISSET(ml_data.wakeup_read, &except_set)) {
        fprintf(stderr, "mainloop caught exception on internal pipe\n");
        ml_data.is_running = 0;
        result = -EFAULT;
        break;
      }
      if (FD_ISSET(ml_data.wakeup_read, &read_set)) {
        char buf[10];
        while (read(ml_data.wakeup_read, buf, sizeof(buf)) == sizeof(buf)) ;
      }
    }
    else
    if (ret < 0) {
      /* error */
      perror("mainloop caught error");
      ml_data.is_running = 0;
      result = ret;
      break;
    }

    gettimeofday(&now, NULL);
    
    /* apply the minimum resolution */
    now.tv_usec += TIMER_MIN_RESOLUTIN * 1000L;
    if (now.tv_usec > 1000000L) {
      now.tv_usec -= 1000000L;
      now.tv_sec++;
    }

    /* run callbacks for timer events (in correct order!) */
    do {
      index = -1;
      tv.tv_sec = 0;
      current = ml_data.ev_list;
      for (i = 0; i < ml_data.ev_list_used; i++, current++) {
        if (!current->processed &&
            ((current->type == EV_TYPE_TIMER) ||
             (current->type == EV_TYPE_PTIMER))) {
          if (timercmp(&current->expire, &now, <=)) {
            if ((tv.tv_sec == 0) ||
                timercmp(&current->expire, &tv, <)) {
              tv.tv_sec = current->expire.tv_sec;
              tv.tv_usec = current->expire.tv_usec;
              index = i;
            }
          }
        }
      }
      if (index >= 0) {
        current = &(ml_data.ev_list[index]);
        current->processed = 1;
        if (current->type == EV_TYPE_TIMER) {
          /* remove timer */
          current->type = EV_TYPE_EMPTY;
          /* try to reduce the number of "used" entries */
          if (index + 1 == ml_data.ev_list_used) {
            for (i = index; i >= 0; i--) {
              if (ml_data.ev_list[i].type != EV_TYPE_EMPTY)
                break;
              ml_data.ev_list_used--;
            }
          }
        }
        else {
          /* reschedule timer */
          timeradd(&current->expire, &current->interval, &current->expire);
          /* TODO: What happens if we were suspended for a while? */
        }
        if (current->callback) {
          current->callback(ml_data.ev_list[index].event_id,
                            ml_data.ev_list[index].group_id,
                            current->callback_data);
        }
      }
    } while (index >= 0);
  }
  
  if (result != 0)
    yp_ml_shutdown();
  
  return result;
}

/*****************************************************************/

int yp_ml_stop()
{
  int is_running = ml_data.is_running;
  ssize_t res;

  ml_data.is_running = 0;
  if (is_running)
    res = write(ml_data.wakeup_write, &is_running, 1);   /* wake up mainloop */
  return 0;
}

/*****************************************************************/

int yp_ml_shutdown()
{
  ml_data.is_running = 0;

  if (ml_data.wakeup_write >= 0) {
    close(ml_data.wakeup_write);
    ml_data.wakeup_write = -1;
  }
  if (ml_data.wakeup_read >= 0) {
    close(ml_data.wakeup_read);
    ml_data.wakeup_read = -1;
  }

  /* Free memory */
  ml_data.ev_list_allocated = 0;
  ml_data.ev_list_used = 0;
  if (ml_data.ev_list) {
    free(ml_data.ev_list);
    ml_data.ev_list = NULL;
  }
  ml_data.event_id_max = 0;
  FD_ZERO(&ml_data.select_master_set);
  ml_data.select_max_fd = 0;

  return 0;
}

/*****************************************************************/

struct event_list *get_free_entry(int *index)
{
  struct event_list *entry;
  int i, idx;
  
  entry = NULL;
  for (i = 0; i < ml_data.ev_list_used; i++) {
    if (ml_data.ev_list[i].type == EV_TYPE_EMPTY) {
      entry = &(ml_data.ev_list[i]);
      idx = i;
      break;
    }
  }
  if (entry == NULL) {
    if (ml_data.ev_list_used >= ml_data.ev_list_allocated) {
      struct event_list *new_base;
      fprintf(stderr, "extending event list\n");
      ml_data.ev_list_allocated += 3;        /* add 3 records (2 spare) */
      new_base = realloc(ml_data.ev_list,
                         ml_data.ev_list_allocated * sizeof(ml_data.ev_list[0]));
      if (new_base == NULL) {
        fprintf(stderr, "Cannot extend size of event list");
        return NULL;
      }
      ml_data.ev_list = new_base;
    }
    idx = ml_data.ev_list_used++;
    entry = &(ml_data.ev_list[idx]);
  }
  if (index)
    *index = idx;
  return entry;
}

/*****************************************************************/

/* Returns the number of periods of tm_check until the timers
 * overlap.
 * 0 .. there is no (reasonable) overlap detected.
 * 1 .. a perfect match, every tick overlaps with tm_exist
 * >1 .. every X ticks overlap.
 */
static inline int timer_overlap_score(int tm_check,
                                      struct timeval *tv_exist)
{
  int tm_exist = TIMEVAL_TO_MS(tv_exist);
  int i;
  if (!tm_check || !tm_exist) {
    /* invalid durations */
    return 0;
  }
  if (tm_check == tm_exist) {
    /* same interval */
    return 1;
  }
  if (tm_exist > tm_check) {
    for (i = 1; i <= 4; i++)
      if ((tm_exist * i) % tm_check == 0)
        return (tm_exist * i) / tm_check;
  }
  else {  /* tm_exist < tm_check */
    for (i = 1; i <= 4; i++)
      if ((tm_check * i) % tm_exist == 0)
        return i;
  }
  return 0;
}

/*****************************************************************/

static int yp_mlint_schedule_timer(int group_id, int delay,
                                   int allow_optimize,
                                   yp_ml_callback cb, void *private_data,
                                   enum event_type type)
{
  struct event_list *entry;
  struct timeval now;
  int index, i;
  int score, best_score, best_index;
  ssize_t res;
  
  entry = get_free_entry(&index);
  if (entry == NULL)
    return -ENOMEM;

  entry->type = type;
  entry->event_id = ++ml_data.event_id_max;
  entry->group_id = group_id;
  entry->processed = 1;
  MS_TO_TIMEVAL(delay, &entry->interval);
  entry->fd = 0;
  entry->callback = cb;
  entry->callback_data = private_data;
  
  gettimeofday(&now, NULL);

  best_index = -1;
  if (allow_optimize) {
    for (i = 0; i < ml_data.ev_list_used; i++) {
      if (i == index)
        continue;
      if (ml_data.ev_list[i].type == EV_TYPE_PTIMER) {
        score = timer_overlap_score(delay, &ml_data.ev_list[i].interval);
        if (score == 0)
          continue;
        if (score == 1) {
          /* found perfect match */
          best_index = i;
          break;
        }
        if ((best_index < 0) || (score < best_score)) {
          best_score = score;
          best_index = i;
        }
      }
    }
  }
  if (best_index >= 0) {
    struct timeval *tv_ref = &ml_data.ev_list[best_index].expire;
    struct timeval tv_diff;
    int ms_diff;
    
    entry->expire.tv_sec = tv_ref->tv_sec;
    entry->expire.tv_usec = tv_ref->tv_usec;
    
    if (timercmp(tv_ref, &now, >)) {
      /* reference expires in the future (regular case) */
      timersub(tv_ref, &now, &tv_diff);
      ms_diff = TIMEVAL_TO_MS(&tv_diff);
      ms_diff = (ms_diff / delay) * delay;
      if (ms_diff > 0) {
        /* rewind by 'ms_diff' milliseconds */
        MS_TO_TIMEVAL(ms_diff, &tv_diff);
        timersub(&entry->expire, &tv_diff, &entry->expire); /* expire -= diff */
      }
    }
    else {
      /* reference already expired */
      timersub(&now, tv_ref, &tv_diff);
      ms_diff = TIMEVAL_TO_MS(&tv_diff);
      ms_diff = ((ms_diff / delay) + 1) * delay;
      /* advance by 'msdiff' milliseconds */
      MS_TO_TIMEVAL(ms_diff, &tv_diff);
      timeradd(&entry->expire, &tv_diff, &entry->expire);   /* expire += diff */
    }
  }
  else {
    /* no optimization: expire = now + interval */
    timeradd(&now, &entry->interval, &entry->expire);
  }
  
  res = write(ml_data.wakeup_write, &best_index, 1);

  return entry->event_id;
}

/*****************************************************************/

int yp_ml_schedule_timer(int group_id, int delay,
                         yp_ml_callback cb, void *private_data)
{
  return yp_mlint_schedule_timer(group_id, delay, 0, cb, private_data,
                                 EV_TYPE_TIMER);
}

/*****************************************************************/

int yp_ml_schedule_periodic_timer(int group_id, int interval,
                                  int allow_optimize,
                                  yp_ml_callback cb, void *private_data)
{
  return yp_mlint_schedule_timer(group_id, interval, allow_optimize,
                                 cb, private_data,
                                 EV_TYPE_PTIMER);
}

/*****************************************************************/

int yp_ml_reschedule_periodic_timer(int event_id, int interval,
                                    int allow_optimize)
{
  fprintf(stderr, "%s not yet implemented!\n", __FUNCTION__);
  abort();
}

/*****************************************************************/

int yp_ml_poll_io(int group_id, int fd,
                  yp_ml_callback cb, void *private_data)
{
  struct event_list *entry;
  ssize_t res;
  
  entry = get_free_entry(NULL);
  if (entry == NULL)
    return -ENOMEM;

  entry->type = EV_TYPE_IO;
  entry->event_id = ++ml_data.event_id_max;
  entry->group_id = group_id;
  entry->fd = fd;
  entry->callback = cb;
  entry->callback_data = private_data;

  FD_SET(fd, &ml_data.select_master_set);
  if (ml_data.select_max_fd <= fd)
    ml_data.select_max_fd = fd + 1;
  
  res = write(ml_data.wakeup_write, &fd, 1);
  
  return entry->event_id;
}

/*****************************************************************/

int yp_ml_remove_event(int event_id, int group_id)
{
  int count = 0;
  int need_wakeup = 0;
  int max_fd = 0;
  int i;
  ssize_t res;
  struct event_list *current;
  
  current = &ml_data.ev_list[ml_data.ev_list_used - 1];
  for (i = ml_data.ev_list_used; i > 0 ; i--, current--) {
    if ((current->type != EV_TYPE_EMPTY) &&
        ((event_id < 0) || (current->event_id == i)) &&
        ((group_id < 0) || (current->group_id == group_id))) {
      if (current->type == EV_TYPE_IO) {
        FD_CLR(current->fd, &ml_data.select_master_set);
        need_wakeup = 1;
      }
      if (i == ml_data.ev_list_used) {
        /* last entry -> shrink the list */
        ml_data.ev_list_used--;
      }
      else {
        /* other entry -> mark empty */
        current->type = EV_TYPE_EMPTY;
      }
      count++;
    }
    else
    if (current->type == EV_TYPE_IO) {
      /* recalculate max_fd */
      if (max_fd < current->fd)
        max_fd = current->fd;
    }
  }
  
  if (need_wakeup) {
    ml_data.select_max_fd = max_fd + 1;
    res = write(ml_data.wakeup_write, &max_fd, 1);
  }
  
  return count;
}

/*****************************************************************/

int yp_ml_count_events(int event_id, int group_id)
{
  int count = 0;
  int i;
  struct event_list *current;
  
  current = ml_data.ev_list;
  for (i = 0; i < ml_data.ev_list_used; i++, current++) {
    if ((current->type != EV_TYPE_EMPTY) &&
        ((event_id < 0) || (current->event_id == i)) &&
        ((group_id < 0) || (current->group_id == group_id))) {
      count++;
    }
  }
  
  return count;
}

/*****************************************************************/

int yp_ml_same_thread(void) {
#ifdef HAVE_PTHREAD_H
  return (ml_data.is_running) ? pthread_equal(pthread_self(), ml_data.thread) : 1;
#else
  return 1;
#endif
}


