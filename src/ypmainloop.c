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
#include <strings.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

#include "ypmainloop.h"
#include "config.h"

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif


#define INITIAL_EV_LIST_SIZE  6
#define TIMER_MIN_RESOLUTIN  10         /* in [ms] */


enum event_type {
  EV_TYPE_EMPTY = 0,
  EV_TYPE_TIMER,
  EV_TYPE_PTIMER,
  EV_TYPE_IO
};

struct event_list {
  enum event_type type;
  int group_id;
  int fd;
  int interval;
  struct timeval tv;
  yp_ml_callback callback;
  void *callback_data;
  int processed;
};

struct ml_data_s {
  struct event_list *ev_list;
  int ev_list_size;

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

  bzero(&ml_data, sizeof(ml_data));

  /* preallocate event list */
  ml_data.ev_list_size = INITIAL_EV_LIST_SIZE;
  ml_data.ev_list = calloc(ml_data.ev_list_size, sizeof(ml_data.ev_list[0]));
  if (!ml_data.ev_list) {
    fprintf(stderr, "Cannot allocate memory for event list\n");
    return -ENOMEM;
  }

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
  long usec;
  int ret, index, i;
  int result;

  if (ml_data.is_running) {
    fprintf(stderr, "mainloop is already running\n");
    return 0;
  }
  ml_data.is_running = 1;
  result = 0;
  ml_data.thread  = pthread_self();
  
  while (ml_data.is_running) {
    /* find the timer to expire next */
    tv.tv_sec = 0;
    current = ml_data.ev_list;
    for (i = 0; i < ml_data.ev_list_size; i++, current++) {
      if ((current->type == EV_TYPE_TIMER) ||
          (current->type == EV_TYPE_PTIMER)) {
        if ((tv.tv_sec == 0) ||
            (current->tv.tv_sec < tv.tv_sec) ||
            ((current->tv.tv_sec == tv.tv_sec) &&
             (current->tv.tv_usec < tv.tv_usec))) {
          tv.tv_sec = current->tv.tv_sec;
          tv.tv_usec = current->tv.tv_usec;
        }
        /* reset 'processed' flag (used below) */
        current->processed = 0;
      }
    }
    if (tv.tv_sec == 0) {
      /* no timer -> wait for 1 hour */
      tv.tv_sec = 3600;
      tv.tv_usec = 0;
    }
    else {
      gettimeofday(&now, NULL);
      tv.tv_sec -= now.tv_sec;
      tv.tv_usec -= now.tv_usec;
      if (tv.tv_usec < 0) {
        tv.tv_sec--;
        tv.tv_usec += 1000000L;
      }
      if ((tv.tv_sec < 0)  ||
          ((tv.tv_sec == 0) &&
           (tv.tv_usec < (TIMER_MIN_RESOLUTIN * 1000L)))) {
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
      for (i = 0; i < ml_data.ev_list_size; i++, current++) {
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
    usec = (long) now.tv_usec + (TIMER_MIN_RESOLUTIN * 1000L);
    if (usec > 1000000L) {
      usec -= 1000000L;
      now.tv_sec++;
    }
    now.tv_usec = usec;
    
    /* run callbacks for timer events (in correct order!) */
    do {
      index = -1;
      tv.tv_sec = 0;
      current = ml_data.ev_list;
      for (i = 0; i < ml_data.ev_list_size; i++, current++) {
        if (!current->processed &&
            ((current->type == EV_TYPE_TIMER) ||
             (current->type == EV_TYPE_PTIMER))) {
          if ((current->tv.tv_sec < now.tv_sec) ||
              ((current->tv.tv_sec == now.tv_sec) &&
               (current->tv.tv_usec <= now.tv_usec))) {
            if ((tv.tv_sec == 0) ||
                (current->tv.tv_sec < tv.tv_sec) ||
                ((current->tv.tv_sec == tv.tv_sec) &&
                 (current->tv.tv_usec < tv.tv_usec))) {
              tv.tv_sec = current->tv.tv_sec;
              tv.tv_usec = current->tv.tv_usec;
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
        }
        else {
          /* reschedule timer */
          /* TODO: What happens if we were suspended for a while? */
          current->tv.tv_sec += (current->interval / 1000);
          current->tv.tv_usec += ((long) (current->interval % 1000) * 1000L);
          if (current->tv.tv_usec > 1000000L) {
            current->tv.tv_sec++;
            current->tv.tv_usec -= 1000000L;
          }
        }
        if (current->callback) {
          current->callback(index, ml_data.ev_list[index].group_id,
                            current->callback_data);
        }
      }
    } while (index >= 0);
  }
  
  close(ml_data.wakeup_write);
  close(ml_data.wakeup_read);
  
  /* Free memory */
  ml_data.ev_list_size = 0;
  free(ml_data.ev_list);
  FD_ZERO(&ml_data.select_master_set);
  ml_data.select_max_fd = 0;
  
  return result;
}

/*****************************************************************/

int yp_ml_shutdown()
{
  int is_running = ml_data.is_running;
  ml_data.is_running = 0;
  if (is_running)
    write(ml_data.wakeup_write, &ml_data, 1);    /* wake up mainloop */
  return 0;
}

/*****************************************************************/

static int yp_mlint_schedule_timer(int group_id, int delay,
                                   yp_ml_callback cb, void *private_data,
                                   enum event_type type)
{
  struct event_list *empty;
  int event_id;
  int i;
  
  empty = NULL;
  for (i = 0; i < ml_data.ev_list_size; i++) {
    if (ml_data.ev_list[i].type == EV_TYPE_EMPTY) {
      empty = &(ml_data.ev_list[i]);
      event_id = i;
      break;
    }
  }
  if (empty == NULL) {
    struct event_list *new_base;
    
    fprintf(stderr, "extending event list\n");
    event_id = ml_data.ev_list_size;
    ml_data.ev_list_size++;
    new_base = realloc(ml_data.ev_list,
                       ml_data.ev_list_size * sizeof(ml_data.ev_list[0]));
    if (new_base == NULL) {
      fprintf(stderr, "Cannot extend size of event list");
      return -ENOMEM;
    }
    ml_data.ev_list = new_base;
    empty = &(new_base[event_id]);
  }
  
  empty->type = type;
  empty->group_id = group_id;
  empty->processed = 1;
  empty->interval = delay;
  empty->callback = cb;
  empty->callback_data = private_data;
  
  gettimeofday(&empty->tv, NULL);
  empty->tv.tv_sec += (delay / 1000);
  empty->tv.tv_usec += ((long) (delay % 1000) * 1000L);
  if (empty->tv.tv_usec >= 1000000L) {
    empty->tv.tv_sec++;
    empty->tv.tv_usec -= 1000000L;
  }
  
  write(ml_data.wakeup_write, &i, 1);

  return event_id;
}

/*****************************************************************/

int yp_ml_schedule_timer(int group_id, int delay,
                         yp_ml_callback cb, void *private_data)
{
  return yp_mlint_schedule_timer(group_id, delay, cb, private_data,
                                 EV_TYPE_TIMER);
}

/*****************************************************************/

int yp_ml_schedule_periodic_timer(int group_id, int interval,
                                  yp_ml_callback cb, void *private_data)
{
  return yp_mlint_schedule_timer(group_id, interval, cb, private_data,
                                 EV_TYPE_PTIMER);
}

/*****************************************************************/

int yp_ml_poll_io(int group_id, int fd,
                  yp_ml_callback cb, void *private_data)
{
  struct event_list *empty;
  int event_id;
  int i;
  
  empty = NULL;
  for (i = 0; i < ml_data.ev_list_size; i++) {
    if (ml_data.ev_list[i].type == EV_TYPE_EMPTY) {
      empty = &(ml_data.ev_list[i]);
      event_id = i;
      break;
    }
  }
  if (empty == NULL) {
    struct event_list *new_base;
    
    fprintf(stderr, "extending event list\n");
    event_id = ml_data.ev_list_size;
    ml_data.ev_list_size++;
    new_base = realloc(ml_data.ev_list,
                       ml_data.ev_list_size * sizeof(ml_data.ev_list[0]));
    if (new_base == NULL) {
      fprintf(stderr, "Cannot extend size of event list");
      return -ENOMEM;
    }
    ml_data.ev_list = new_base;
    empty = &(new_base[event_id]);
  }
  empty->type = EV_TYPE_IO;
  empty->group_id = group_id;
  empty->fd = fd;
  empty->callback = cb;
  empty->callback_data = private_data;

  FD_SET(fd, &ml_data.select_master_set);
  if (ml_data.select_max_fd <= fd)
    ml_data.select_max_fd = fd + 1;
  
  write(ml_data.wakeup_write, &fd, 1);
  
  return event_id;
}

/*****************************************************************/

int yp_ml_remove_event(int event_id, int group_id)
{
  int count = 0;
  int need_wakeup = 0;
  int max_fd = 0;
  int i;
  struct event_list *current;
  
  current = ml_data.ev_list;
  for (i = 0; i < ml_data.ev_list_size; i++, current++) {
    if ((current->type != EV_TYPE_EMPTY) &&
        ((event_id < 0) || (event_id == i)) &&
        ((group_id < 0) || (current->group_id == group_id))) {
      if (current->type == EV_TYPE_IO) {
        FD_CLR(current->fd, &ml_data.select_master_set);
        need_wakeup = 1;
      }
      current->type = EV_TYPE_EMPTY;
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
    write(ml_data.wakeup_write, &max_fd, 1);
  }
  
  return count;
}

int yp_ml_count_events(int event_id, int group_id)
{
  int count = 0;
  int i;
  struct event_list *current;
  
  current = ml_data.ev_list;
  for (i = 0; i < ml_data.ev_list_size; i++, current++) {
    if ((current->type != EV_TYPE_EMPTY) &&
        ((event_id < 0) || (event_id == i)) &&
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


