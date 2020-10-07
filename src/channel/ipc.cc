/*
 * Copyright (c) 2018-2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "channel/ipc.h"

#include "tll/channel/event.hpp"
#include "tll/util/size.h"

#include <unistd.h>

using namespace tll;

TLL_DEFINE_IMPL(ChIpc);
TLL_DEFINE_IMPL(ChIpcServer);

tll_channel_impl_t * ChIpc::_init_replace(const tll::UrlView &url)
{
	auto client = url.getT("mode", true, {{"client", true}, {"server", false}});
	if (!client)
		return _log.fail(nullptr, "Invalid mode field: {}", client.error());
	if (!*client)
		return &ChIpcServer::impl;
	return nullptr;
}

int ChIpc::_init(const UrlView &url, tll::Channel *master)
{
	this->master = channel_cast<ChIpcServer>(master);
	if (!this->master)
		return _log.fail(EINVAL, "Parent {} must be ipc://;mode=server channel", master?master->name():"NULL");
	_log.debug("Init child of master {}", tll_channel_name(master));

	return Event<ChIpc>::_init(url, master);
}

int ChIpc::_open(const PropsView &url)
{
	if (master->state() != TLL_STATE_ACTIVE)
		return _log.fail(EINVAL, "Parent is not active: {}", tll_state_str(master->state()));

	_qin.reset(new cqueue_t);
	_qout.reset(new squeue_t);
	_qin->event = this;
	_qout->event = master;
	_markers = master->_markers;
	{
		std::unique_lock<std::mutex> lock(master->_lock);
		_addr = master->_addr++;
		master->_clients.emplace(_addr, _qin);
	}

	if (Event<ChIpc>::_open(url))
		return _log.fail(EINVAL, "Failed to open event parent");
	state(TLL_STATE_ACTIVE);
	return 0;
}

int ChIpc::_close()
{
	Event<ChIpc>::_close();
	_qin.reset(nullptr);
	_qout.reset(nullptr);
	_markers.reset();
	return 0;
}

int ChIpc::_post(const tll_msg_t *msg, int flags)
{
	tll::util::OwnedMessage m(msg);
	m.addr = _addr;
	auto ref = _qout;
	if (_markers->push(_qout.get()))
		return EAGAIN;
	ref.release();
	_qout->push(std::move(m));
	if (_qout->event->event_notify())
		return _log.fail(EINVAL, "Failed to arm event");
	return 0;
}

int ChIpc::_process(long timeout, int flags)
{
	auto msg = _qin->pop();
	if (!msg)
		return EAGAIN;
	_callback_data(*msg);

	return event_clear_race([this]() -> bool { return !_qin->empty(); });
}

int ChIpcServer::_init(const UrlView &url, tll::Channel *master)
{
	auto reader = channel_props_reader(url);
	_size = reader.getT<tll::util::Size>("size", 64 * 1024);
	if (!reader)
		return _log.fail(EINVAL, "Invalid url: {}", reader.error());

	return Event<ChIpcServer>::_init(url, master);
}

int ChIpcServer::_open(const PropsView &url)
{
	_addr = 0;
	_clients.clear();
	_markers.reset(new MarkerQueue<ChIpc::squeue_t *, nullptr>(_size));
	if (Event<ChIpcServer>::_open(url))
		return _log.fail(EINVAL, "Failed to open event parent");
	state(TLL_STATE_ACTIVE);
	return 0;
}

int ChIpcServer::_close()
{
	Event<ChIpcServer>::_close();
	_clients.clear();
	_markers.reset();
	_addr = 0;
	return 0;
}

int ChIpcServer::_post(const tll_msg_t *msg, int flags)
{
	auto it = _clients.find(msg->addr);
	if (it == _clients.end()) return ENOENT;
	it->second->push(tll::util::OwnedMessage(msg));
	if (it->second->event->event_notify())
		return _log.fail(EINVAL, "Failed to arm event");
	return 0;
}

int ChIpcServer::_process(long timeout, int flags)
{
	auto q = _markers->pop();
	if (!q)
		return EAGAIN;
	auto msg = q->pop();
	while (!msg) {
		msg = q->pop();
	}
	_callback_data(*msg);
	q->unref();

	return event_clear_race([this]() -> bool { return !_markers->empty(); });
}
