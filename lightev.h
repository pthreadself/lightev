/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __AE_H__
#define __AE_H__

#define AE_OK 0
#define AE_ERR -1

#define AE_NONE 0
#define AE_READABLE 1
#define AE_WRITABLE 2

#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4

#define AE_NOMORE -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct lightev_eventloop;

/* Types and data structures */




typedef void lightev_io_function(struct lightev_eventloop *eventLoop, int fd, void *clientData, int mask);
typedef int lightev_timer_function(struct lightev_eventloop *eventloop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct lightev_eventloop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct lightev_eventloop *eventLoop);

/* File event structure */
typedef struct lightev_file_event {
    int mask; /* one of AE_(READABLE|WRITABLE) */
    lightev_io_function * read_action;
    lightev_io_function * write_action;
    void * arg;
} lightev_file_event;

/* Time event structure */
typedef struct lightev_time_event {
    long long id; /* time event identifier. */
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */
    lightev_timer_function *timeout_action;
    aeEventFinalizerProc *finalizerProc;
    void *clientData;
    struct lightev_time_event *next;
} lightev_time_event;

/* A fired event */
typedef struct lightev_fired_event {
    int fd;
    int mask;
} lightev_fired_event;

/* State of an event based program */
typedef struct lightev_eventloop {
    int maxfd;   /* highest file descriptor currently registered */
    int setsize; /* max number of file descriptors tracked */
    long long timeEventNextId;
    time_t lastTime;     /* Used to detect system clock skew */
    lightev_file_event *reg_events; /* Registered events */
    lightev_fired_event *fired; /* Fired events */
    lightev_time_event *timeEventHead;
    int stop;
    //void *apidata; /* This is used for polling API specific data */
    
    int epfd; /* epoll fd */
    struct epoll_event *events;

    aeBeforeSleepProc *beforesleep;
} lightev_eventloop;

/* Prototypes */
void lightev_eventloop_stop(lightev_eventloop *eventLoop);
int lightev_file_event_add(lightev_eventloop *eventLoop, int fd, int mask,
        lightev_io_function *proc, void *clientData);
void lightev_file_event_del(lightev_eventloop *eventLoop, int fd, int mask);
int lightev_file_event_mask(lightev_eventloop *eventLoop, int fd);
long long lightev_timer_event_add(lightev_eventloop *eventLoop, long long milliseconds,
        lightev_timer_function *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
int lightev_timer_event_del(lightev_eventloop *eventLoop, long long id);
int lightev_event_dispatch(lightev_eventloop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void lightev_run(lightev_eventloop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(lightev_eventloop *eventLoop, aeBeforeSleepProc *beforesleep);

#endif


