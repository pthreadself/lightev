/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/epoll.h>
#include "lightev.h"
#include "mm.h"

/* API implementations: event loop operations  */
lightev_eventloop *lightev_eventloop_create(int setsize) {
    lightev_eventloop *eventloop;
    int i;

    if ((eventloop = lightev_mm_malloc(sizeof(*eventloop))) == NULL) goto err;
    eventloop->reg_events = lightev_mm_malloc(sizeof(lightev_file_event)*setsize);
    eventloop->fired = lightev_mm_malloc(sizeof(lightev_fired_event)*setsize);
    if (eventloop->reg_events == NULL || eventloop->fired == NULL) goto err;
    eventloop->setsize = setsize;
    eventloop->lastTime = time(NULL);
    eventloop->timeEventHead = NULL;
    eventloop->timeEventNextId = 0;
    eventloop->stop = 0;
    eventloop->maxfd = -1;
    eventloop->beforesleep = NULL;

    eventloop->events = lightev_mm_malloc(sizeof(struct epoll_event)*eventloop->setsize);
    if (!eventloop->events) {
        goto err;
    }
    eventloop->epfd = epoll_create(1024); /* 1024 is just an hint for the kernel */
    if (eventloop->epfd == -1) {
        goto err;
    }

    /* Events with mask == AE_NONE are not set. So let's initialize the
    * vector with it. */
    for (i = 0; i < setsize; i++)
        eventloop->reg_events[i].mask = AE_NONE;
    return eventloop;

err:
    if (eventloop) {
        lightev_mm_free(eventloop->events);
        lightev_mm_free(eventloop->fired);
        lightev_mm_free(eventloop);
    }
    return NULL;
}

void lightev_eventloop_del(lightev_eventloop *eventloop) {
    close(eventloop->epfd);
    lightev_mm_free(eventloop->reg_events);
    lightev_mm_free(eventloop->events);
    lightev_mm_free(eventloop->fired);
    lightev_mm_free(eventloop);
}

void lightev_eventloop_stop(lightev_eventloop *eventloop) {
    eventloop->stop = 1;
}

/* API implementations: file event operations  */
int lightev_file_event_add(lightev_eventloop *eventloop, int fd, int mask,
    lightev_io_function *proc, void *clientData)
{
    if (fd >= eventloop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    lightev_file_event *fe = &eventloop->reg_events[fd];

    if (backend_add_event(eventloop, fd, mask) == -1)
        return AE_ERR;
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->read_action = proc;
    if (mask & AE_WRITABLE) fe->write_action = proc;
    fe->arg = clientData;
    if (fd > eventloop->maxfd)
        eventloop->maxfd = fd;
    return AE_OK;
}

void lightev_file_event_del(lightev_eventloop *eventloop, int fd, int mask)
{
    if (fd >= eventloop->setsize) return;
    lightev_file_event *fe = &eventloop->reg_events[fd];

    if (fe->mask == AE_NONE) return;
    fe->mask = fe->mask & (~mask);
    if (fd == eventloop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventloop->maxfd - 1; j >= 0; j--)
            if (eventloop->reg_events[j].mask != AE_NONE) break;
        eventloop->maxfd = j;
    }
    backend_del_event(eventloop, fd, mask);
}

int lightev_file_event_mask(lightev_eventloop *eventloop, int fd) {
    if (fd >= eventloop->setsize) return 0;
    lightev_file_event *fe = &eventloop->reg_events[fd];

    return fe->mask;
}

/* API implementations: timer event operations  */
long long lightev_timer_event_add(lightev_eventloop *eventloop, long long milliseconds,
    lightev_timer_function *proc, void *clientData,
    lightev_event_finalizer_function *finalizerProc)
{
    long long id = eventloop->timeEventNextId++;
    lightev_time_event *te;

    te = lightev_mm_malloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    te->id = id;
    add_ms_to_now(milliseconds, &te->when_sec, &te->when_ms);
    te->timeout_action = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->next = eventloop->timeEventHead;
    eventloop->timeEventHead = te;
    return id;
}

int lightev_timer_event_del(lightev_eventloop *eventloop, long long id)
{
    lightev_time_event *te, *prev = NULL;

    te = eventloop->timeEventHead;
    while (te) {
        if (te->id == id) {
            if (prev == NULL)
                eventloop->timeEventHead = te->next;
            else
                prev->next = te->next;
            if (te->finalizerProc)
                te->finalizerProc(eventloop, te->clientData);
            lightev_mm_free(te);
            return AE_OK;
        }
        prev = te;
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */
}

/* API implementations: run operations  */

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int lightev_event_dispatch(lightev_eventloop *eventloop, int flags)
{
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    if (eventloop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        lightev_time_event *shortest = NULL;
        struct timeval tv, *tvp;

        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            shortest = find_nearest_timer(eventloop);
        if (shortest) {
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            get_time(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms + 1000) - now_ms) * 1000;
                tvp->tv_sec--;
            }
            else {
                tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
            }
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        }
        else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            }
            else {
                /* Otherwise we can block */
                tvp = NULL; /* wait forever */
            }
        }

        numevents = backend_epoll_event(eventloop, tvp);
        for (j = 0; j < numevents; j++) {
            lightev_file_event *fe = &eventloop->reg_events[eventloop->fired[j].fd];
            int mask = eventloop->fired[j].mask;
            int fd = eventloop->fired[j].fd;
            int rfired = 0;

            /* note the fe->mask & mask & ... code: maybe an already processed
                 * event removed an element that fired and we still didn't
                 * processed, so we check if the event is still valid. */
            if (fe->mask & mask & AE_READABLE) {
                rfired = 1;
                fe->read_action(eventloop, fd, fe->arg, mask);
            }
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->write_action != fe->read_action)
                    fe->write_action(eventloop, fd, fe->arg, mask);
            }
            processed++;
        }
    }
    /* Check time events */
    if (flags & AE_TIME_EVENTS)
        processed += process_timer_events(eventloop);

    return processed; /* return the number of processed file/time events */
}

void lightev_run(lightev_eventloop *eventloop) {
    eventloop->stop = 0;
    while (!eventloop->stop) {
        if (eventloop->beforesleep != NULL)
            eventloop->beforesleep(eventloop);
        lightev_event_dispatch(eventloop, AE_ALL_EVENTS);
    }
}

/* internal implementations */

static int backend_add_event(lightev_eventloop *eventloop, int fd, int mask) {
    struct epoll_event ee;
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    int op = eventloop->reg_events[fd].mask == AE_NONE ?
    EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= eventloop->reg_events[fd].mask; /* Merge old events */
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (epoll_ctl(eventloop->epfd, op, fd, &ee) == -1) return -1;
    return 0;
}

static void backend_del_event(lightev_eventloop *eventloop, int fd, int delmask) {
    struct epoll_event ee;
    int mask = eventloop->reg_events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(eventloop->epfd, EPOLL_CTL_MOD, fd, &ee);
    }
    else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(eventloop->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int backend_epoll_event(lightev_eventloop *eventloop, struct timeval *tvp) {
    int retval, numevents = 0;

    retval = epoll_wait(eventloop->epfd, eventloop->events, eventloop->setsize,
        tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = eventloop->events + j;

            if (e->events & EPOLLIN) mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE;
            eventloop->fired[j].fd = e->data.fd;
            eventloop->fired[j].mask = mask;
        }
    }
    return numevents;
}

static void get_time(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
}

static void add_ms_to_now(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    get_time(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds / 1000;
    when_ms = cur_ms + milliseconds % 1000;
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static lightev_time_event *find_nearest_timer(lightev_eventloop *eventloop)
{
    lightev_time_event *te = eventloop->timeEventHead;
    lightev_time_event *nearest = NULL;

    while (te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
            (te->when_sec == nearest->when_sec &&
            te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process time events */
static int process_timer_events(lightev_eventloop *eventloop) {
    int processed = 0;
    lightev_time_event *te;
    long long maxId;
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    if (now < eventloop->lastTime) {
        te = eventloop->timeEventHead;
        while (te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    eventloop->lastTime = now;

    te = eventloop->timeEventHead;
    maxId = eventloop->timeEventNextId - 1;
    while (te) {
        long now_sec, now_ms;
        long long id;

        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        get_time(&now_sec, &now_ms);
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            retval = te->timeout_action(eventloop, id, te->clientData);
            processed++;
            /* After an event is processed our time event list may
             * no longer be the same, so we restart from head.
             * Still we make sure to don't process events registered
             * by event handlers itself in order to don't loop forever.
             * To do so we saved the max ID we want to handle.
             *
             * FUTURE OPTIMIZATIONS:
             * Note that this is NOT great algorithmically. Redis uses
             * a single time event so it's not a problem but the right
             * way to do this is to add the new elements on head, and
             * to flag deleted elements in a special way for later
             * deletion (putting references to the nodes to delete into
             * another linked list). */
            if (retval != AE_NOMORE) {
                add_ms_to_now(retval, &te->when_sec, &te->when_ms);
            }
            else {
                lightev_timer_event_del(eventloop, id);
            }
            te = eventloop->timeEventHead;
        }
        else {
            te = te->next;
        }
    }
    return processed;
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds)) == 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    }
    else {
        return retval;
    }
}

void aeSetBeforeSleepProc(lightev_eventloop *eventloop, aeBeforeSleepProc *beforesleep) {
    eventloop->beforesleep = beforesleep;
}