// -*- mode: c; tab-width: 8; indent-tabs-mode: 1; st-rulers: [70] -*-
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2013 Pagoda Box, Inc.  All rights reserved.
 */

#include "narc.h"
#include "stream.h"
#include "zmalloc.h"	/* total memory usage aware version of malloc/free */
#include "sds.h"	/* dynamic safe strings */

// temporary
#include "tcp_client.h"

#include <stdio.h>	/* standard buffered input/output */
#include <stdlib.h>	/* standard library definitions */
#include <unistd.h>	/* standard symbolic constants and types */
#include <uv.h>		/* Event driven programming library */

/*============================ Utility functions ============================ */

int 
file_exists(char *filename)
{
	struct stat buffer;   
	return (stat(filename, &buffer) == 0);
}

void
init_buffer(char *buffer)
{
	memset(buffer, '\0', NARC_MAX_BUFF_SIZE);
}

void
init_line(char *line)
{
	memset(line, '\0', NARC_MAX_MESSAGE_SIZE);
}

void
lock_stream(narc_stream *stream)
{
	stream->lock = NARC_STREAM_LOCKED;
}

int
stream_locked(narc_stream *stream)
{
	return (stream->lock == NARC_STREAM_LOCKED);
}

void
unlock_stream(narc_stream *stream)
{
	stream->lock = NARC_STREAM_UNLOCKED;
}

int
stream_unlocked(narc_stream *stream)
{
	return (stream->lock == NARC_STREAM_UNLOCKED);
}

void
handle_message(char *id, char *message)
{
	if (!strcasecmp(message, "exit")) {
		stop();
	} else {
		char *s;
		s = sdscatprintf(sdsempty(), "%s %s\n", id, message);
		submit_tcp_message(s);
	}
}

/*============================== Callbacks ================================= */

void
handle_file_open(uv_fs_t *req)
{
	narc_stream *stream = req->data;

	if (req->result == -1) {
		narc_log(NARC_WARNING, "Error opening %s (%d/%d): errno %d", 
			stream->file, 
			stream->attempts,
			server.max_attempts,
			req->errorno);

		if (stream->attempts == server.max_attempts)
			narc_log(NARC_WARNING, "Reached max open attempts: %s", stream->file);
		else {
			stream->attempts += 1;
			start_file_open_timer(stream);
		}
	} else {
		narc_log(NARC_NOTICE, "File opened: %s", stream->file);

		stream->fd = req->result;

		start_file_watcher(stream);
		start_file_stat(stream);
	}

	uv_fs_req_cleanup(req);
	zfree(req);
}

void
handle_file_open_timeout(uv_timer_t* timer, int status)
{
	start_file_open((narc_stream *)timer->data);
	zfree(timer);
}

void 
handle_file_change(uv_fs_event_t *handle, const char *filename, int events, int status) 
{
	narc_stream *stream = handle->data;

	if (events == UV_CHANGE)
		start_file_stat(stream);

	else if (!file_exists(stream->file)) {
		narc_log(NARC_WARNING, "File deleted: %s, attempting to re-open", stream->file);
		start_file_open(stream);
		zfree(handle);
	}
}

void
handle_file_stat(uv_fs_t* req)
{
	narc_stream *stream = req->data;
	uv_statbuf_t *stat  = req->ptr;

	if (stream->size < 0)
		lseek(stream->fd, 0, SEEK_END);

	if (stat->st_size < stream->size)
		lseek(stream->fd, 0, SEEK_SET);

	stream->size = stat->st_size;

	start_file_read(stream);

	uv_fs_req_cleanup(req);
	zfree(req);
}

void
handle_file_read(uv_fs_t *req)
{
	narc_stream *stream = req->data;

	if (stream->index == 0)
		init_line(stream->line);

	if (req->result < 0)
		narc_log(NARC_WARNING, "Read error (%s): %s", stream->file, uv_strerror(uv_last_error(uv_default_loop())));

	if (req->result > 0) {
		for (int i = 0; i < req->result; i++) {
			if (stream->buffer[i] == '\n' || stream->index == NARC_MAX_MESSAGE_SIZE -1) {
				stream->line[stream->index] = '\0';
				handle_message(stream->id, stream->line);
				init_line(stream->line);
				stream->index = 0;
			} else {
				stream->line[stream->index] = stream->buffer[i];
				stream->index += 1;
			}
		}
	}

	unlock_stream(stream);

	if (req->result == NARC_MAX_BUFF_SIZE -1)
		start_file_read(stream);

	uv_fs_req_cleanup(req);
	zfree(req);
}

/*================================= Watchers =================================== */

void
start_file_open(narc_stream *stream)
{
	uv_fs_t *req = zmalloc(sizeof(uv_fs_t));
	if (uv_fs_open(server.loop, req, stream->file, O_RDONLY, 0, handle_file_open) == UV_OK)
		req->data = (void *)stream;
}

void
start_file_watcher(narc_stream *stream)
{
	uv_fs_event_t *event = zmalloc(sizeof(uv_fs_event_t));
	if (uv_fs_event_init(server.loop, event, stream->file, handle_file_change, 0) == UV_OK)
		event->data = (void *)stream;
}

void
start_file_open_timer(narc_stream *stream)
{
	uv_timer_t *timer = zmalloc(sizeof(uv_timer_t));
	if (uv_timer_init(server.loop, timer) == UV_OK) {
		if (uv_timer_start(timer, handle_file_open_timeout, server.retry_delay, 0) == UV_OK)
			timer->data = (void *)stream;
	}
}

void
start_file_stat(narc_stream *stream)
{
	uv_fs_t *req = zmalloc(sizeof(uv_fs_t));
	if (uv_fs_stat(server.loop, req, stream->file, handle_file_stat) == UV_OK)
		req->data = (void *)stream;
}

void
start_file_read(narc_stream *stream)
{
	if (stream_locked(stream))
		return;

	init_buffer(stream->buffer);

	uv_fs_t *req = zmalloc(sizeof(uv_fs_t));
	if (uv_fs_read(server.loop, req, stream->fd, stream->buffer, sizeof(stream->buffer) -1, -1, handle_file_read) == UV_OK) {
		lock_stream(stream);
		req->data = (void *)stream;
	}
}

/*================================= API =================================== */

narc_stream
*new_stream(char *id, char *file)
{
	narc_stream *stream = zmalloc(sizeof(narc_stream));

	stream->id       = id;
	stream->file     = file;
	stream->attempts = 0;
	stream->size     = -1;
	stream->index    = 0;
	stream->lock     = NARC_STREAM_UNLOCKED;

	return stream;
}

void
free_stream(narc_stream *stream)
{
	zfree(stream->id);
	zfree(stream->file);
	zfree(stream);
}

void
init_stream(narc_stream *stream)
{
	start_file_open(stream);
}
