/*
 * Copyright (c) 2018-2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_PROCESSOR_LOOP_H
#define _TLL_PROCESSOR_LOOP_H

#include "tll/channel.h"
#include "tll/logger.h"

#include <errno.h>

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

#ifdef __cplusplus

#include "tll/util/time.h"

#include <chrono>
#include <list>
#include <vector>

namespace tll::processor {

template <typename T>
struct List
{
	std::vector<T *> list;
	unsigned size = 0;

	using iterator = typename std::vector<T *>::iterator;
	using reference = typename std::vector<T *>::reference;

	iterator begin() { return list.begin(); }
	iterator end() { return list.begin() + size; }

	reference operator [] (size_t i) { return list[i]; }

	void rebuild()
	{
		auto to = begin();
		for (auto & i : *this) {
			if (!i) continue;
			std::swap(i, *to++);
		}
		size = to - begin();
	}

	void add(T *v)
	{
		for (auto & i : *this) {
			if (!i) {
				i = v;
				return;
			}
		}
		if (size < list.size()) {
			list[size++] = v;
			return;
		}
		list.push_back(v);
		size++;
	}

	void del(const T *v)
	{
		for (unsigned i = 0; i < size; i++) {
			if (list[i] == v) {
				list[i] = nullptr;
				break;
			}
		}

		for (; size > 0; size--) {
			if (list[size - 1] != nullptr)
				break;
		}
	}
};

struct Loop
{
	tll::Logger _log = {"tll.processor.loop"};
#ifdef __linux__
	int fd = { epoll_create1(0) };
	int efd = { eventfd(1, EFD_NONBLOCK) };
#endif
	std::list<tll::Channel *> list;
	List<tll::Channel> list_p;
	List<tll::Channel> list_pending;

	Loop()
	{
#ifdef __linux__
		epoll_event ev = {};
		ev.data.ptr = (void *) this;
		epoll_ctl(fd, EPOLL_CTL_ADD, efd, &ev);
#endif
	}

	~Loop()
	{
#ifdef __linux__
		if (efd != -1) ::close(efd);
		if (fd != -1) ::close(fd);
#endif
	}

	tll::Channel * poll(tll::duration timeout)
	{
#ifdef __linux__
		epoll_event ev = {};
		int r = epoll_wait(fd, &ev, 1, duration_cast<std::chrono::milliseconds>(timeout).count());
		if (!r)
			return 0;
		if (r < 0)
			return _log.fail(nullptr, "epoll failed: {}", strerror(errno));

		if (ev.data.ptr == this) {
			_log.debug("Poll on pending list");
			for (auto & c: list_pending)
				c->process();
			return nullptr;
		}

		auto c = (tll::Channel *) ev.data.ptr;
		_log.debug("Poll on {}", c->name());
		return c;
#endif
		return nullptr;
	}

	int process()
	{
		int r = 0;
		for (unsigned i = 0; i < list_p.size; i++) {
			if (list_p[i] == nullptr) continue;
			r |= list_p[i]->process() ^ EAGAIN;
		}
		for (unsigned i = 0; i < list_pending.size; i++) {
			if (list_pending[i] == nullptr) continue;
			r |= list_pending[i]->process() ^ EAGAIN;
		}
		return r == 0?EAGAIN:0;
	}

	int add(tll::Channel *c)
	{
		_log.info("Add channel {} with fd {}", c->name(), c->fd());
		c->callback_add(this, TLL_MESSAGE_MASK_CHANNEL | TLL_MESSAGE_MASK_STATE);
		list.push_back(c);
		if (c->dcaps() & dcaps::Process)
			list_p.add(c);
		if (c->dcaps() & dcaps::Pending)
			pending_add(c);
		return poll_add(c);
	}

	void pending_add(tll::Channel *c)
	{
		bool empty = list_pending.size == 0;
		list_pending.add(c);
		if (!empty) return;
#ifdef __linux__
		epoll_event ev = {};
		ev.events = EPOLLIN;
		ev.data.ptr = this;
		epoll_ctl(fd, EPOLL_CTL_MOD, efd, &ev);
#endif
	}

	void pending_del(const tll::Channel *c)
	{
		list_pending.del(c);
		if (list_pending.size) return;
#ifdef __linux__
		epoll_event ev = {};
		ev.data.ptr = this;
		epoll_ctl(fd, EPOLL_CTL_MOD, efd, &ev);
#endif
	}

	int poll_add(const tll::Channel *c)
	{
		if (c->fd() == -1)
			return 0;
		_log.info("Add channel {} to poll with fd {}", c->name(), c->fd());
		update_poll(c, c->dcaps(), true);
		return 0;
	}

	int poll_del(const tll::Channel *c)
	{
		if (c->fd() == -1)
			return 0;
#ifdef __linux__
		epoll_event ev = {};
		epoll_ctl(fd, EPOLL_CTL_DEL, c->fd(), &ev);
#endif
		return 0;
	}

	int del(const tll::Channel *c)
	{
		_log.info("Delete channel {}", c->name());
		for (auto it = list.begin(); it != list.end(); it++) {
			if (*it != c)
				continue;
			list.erase(it);
			break;
		}

		list_p.del(c);
		pending_del(c);
		return 0;
	}

	int update(const tll::Channel *c, unsigned caps, unsigned old)
	{
		if (c->fd() == -1)
			return 0;

		auto delta = caps ^ old;

		_log.debug("Update caps {}: {:b} -> {:b} (delta {:b})", c->name(), old, caps, delta);
		if (delta & (dcaps::CPOLLMASK | dcaps::Suspend)) {
			update_poll(c, caps);
		}

		if (delta & dcaps::Process) {
			if (caps & dcaps::Process)
				list_p.add((tll::Channel *) c);
			else
				list_p.del(c);
		}

		if (delta & dcaps::Pending) {
			if (caps & dcaps::Pending)
				pending_add((tll::Channel *) c);
			else
				pending_del(c);
		}

		return 0;
	}

	int update_poll(const tll::Channel *c, unsigned caps, bool add = false)
	{
#ifdef __linux__
		epoll_event ev = {};
		if (!(caps & dcaps::Suspend)) {
			if (caps & dcaps::CPOLLIN) ev.events |= EPOLLIN;
			if (caps & dcaps::CPOLLOUT) ev.events |= EPOLLOUT;
		}
		ev.data.ptr = (void *) c;
		epoll_ctl(fd, add?EPOLL_CTL_ADD:EPOLL_CTL_MOD, c->fd(), &ev);
#endif
		return 0;
	}

	int callback(const Channel *c, const tll_msg_t *msg)
	{
		if (msg->type == TLL_MESSAGE_STATE) {
			if (msg->msgid == tll::state::Active)
				return poll_add(c);
			else if (msg->msgid == tll::state::Closing)
				return poll_del(c);
			else if (msg->msgid == tll::state::Destroy)
				return del(c);
			return 0;
		} else if (msg->type != TLL_MESSAGE_CHANNEL) return 0;

		if (msg->msgid == TLL_MESSAGE_CHANNEL_ADD)
			return add(*((tll::Channel **) msg->data));
		else if (msg->msgid == TLL_MESSAGE_CHANNEL_DELETE)
			return del(*((tll::Channel **) msg->data));
		else if (msg->msgid == TLL_MESSAGE_CHANNEL_UPDATE)
			return update(c, c->dcaps(), *(long long *) msg->data);
		return 0;
	}
};

} // namespace tll::processor

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tll_processor_loop_t tll_processor_loop_t;

tll_processor_loop_t * tll_processor_loop_new();
void tll_processor_loop_free(tll_processor_loop_t *);

int tll_processor_loop_add(tll_processor_loop_t *, tll_channel_t *);
int tll_processor_loop_del(tll_processor_loop_t *, const tll_channel_t *);
tll_channel_t * tll_processor_loop_poll(tll_processor_loop_t *, long timeout);
int tll_processor_loop_process(tll_processor_loop_t *);

#ifdef __cplusplus
} // extern "C"
#endif

#endif//_TLL_PROCESSOR_LOOP_H