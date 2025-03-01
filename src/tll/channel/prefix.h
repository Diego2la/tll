/*
 * Copyright (c) 2018-2021 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _TLL_CHANNEL_PREFIX_H
#define _TLL_CHANNEL_PREFIX_H

#include "tll/channel/base.h"

namespace tll::channel {

/** Base class for prefix channels
 *
 * Provides common code for creation and lifecycle management of child channel.
 *
 * Derived class in addition to _init/_open/_close and _free functions can override _on_* group of functions:
 *  - @ref _on_init: change Url of child channel.
 *  - @ref _on_active, @ref _on_error, @ref _on_closing, @ref _on_closed: handle state changes.
 *  - @ref _on_data, @ref _on_state, @ref _on_other: handle Data, State or any other messages.
 *    In most cases instead of overriding _on_state it's better to use _on_active/... functions described above.
 */
template <typename T>
class Prefix : public Base<T>
{
protected:
	std::unique_ptr<Channel> _child;
public:

	static constexpr auto open_policy() { return Base<T>::OpenPolicy::Manual; }
	static constexpr auto child_policy() { return Base<T>::ChildPolicy::Single; }
	static constexpr auto close_policy() { return Base<T>::ClosePolicy::Long; }
	static constexpr auto process_policy() { return Base<T>::ProcessPolicy::Never; }

	const Scheme * scheme(int type) const
	{
		this->_log.debug("Request scheme {}", type);
		return _child->scheme(type);
	}

	int _init(const tll::Channel::Url &url, tll::Channel *master)
	{

		auto proto = url.proto();
		auto sep = proto.find("+");
		if (sep == proto.npos)
			return this->_log.fail(EINVAL, "Invalid url proto '{}': no + found", proto);
		auto pproto = proto.substr(0, sep);

		tll::Channel::Url curl = url.copy();
		curl.proto(proto.substr(sep + 1));
		curl.host(url.host());
		curl.set("name", fmt::format("{}/{}", this->name, pproto));
		curl.set("tll.internal", "yes");

		for (auto &k : std::vector<std::string_view> { "dump", "stat" }) {
			if (curl.has(k))
				curl.unset(k);
		}

		if (this->channelT()->_on_init(curl, url, master))
			return this->_log.fail(EINVAL, "Init hook returned error");

		_child = this->context().channel(curl, master);
		if (!_child)
			return this->_log.fail(EINVAL, "Failed to create child channel");
		_child->callback_add(this);
		this->_child_add(_child.get(), proto);

		return Base<T>::_init(url, master);
	}

	void _free()
	{
		_child.reset();
		return Base<T>::_free();
	}

	int _open(const tll::PropsView &params)
	{
		return _child->open(conv::to_string(params));
	}

	int _close(bool force)
	{
		return _child->close(force);
	}

	int _post(const tll_msg_t *msg, int flags)
	{
		return _child->post(msg, flags);
	}

	int callback(const Channel * c, const tll_msg_t *msg)
	{
		if (msg->type == TLL_MESSAGE_DATA)
			return this->channelT()->_on_data(msg);
		else if (msg->type == TLL_MESSAGE_STATE)
			return this->channelT()->_on_state(msg);
		return this->channelT()->_on_other(msg);
	}

	/// Modify Url of child channel
	int _on_init(tll::Channel::Url &curl, const tll::Channel::Url &url, const tll::Channel * master)
	{
		return 0;
	}

	/// Handle data messages
	int _on_data(const tll_msg_t *msg)
	{
		return this->_callback_data(msg);
	}

	/** Handle state messages
	 *
	 * In most cases override of this function is not needed. See @ref _on_active, @ref _on_error and @ref _on_closed.
	 */
	int _on_state(const tll_msg_t *msg)
	{
		auto s = (tll_state_t) msg->msgid;
		switch (s) {
		case tll::state::Active:
			if (this->channelT()->_on_active()) {
				this->state(tll::state::Error);
				return 0;
			}
			break;
		case tll::state::Error:
			return this->channelT()->_on_error();
		case tll::state::Closing:
			return this->channelT()->_on_closing();
		case tll::state::Closed:
			return this->channelT()->_on_closed();
		default:
			break;
		}
		this->state(s);
		return 0;
	}

	/// Handle non-state and non-data messages
	int _on_other(const tll_msg_t *msg)
	{
		return this->_callback(msg);
	}

	/// Channel is ready to enter Active state
	int _on_active() { this->state(tll::state::Active); return 0; }
	/// Channel is broken and needs to enter Error state
	int _on_error() { this->state(tll::state::Error); return 0; }
	/// Channel starts closing
	int _on_closing()
	{
		auto s = this->state();
		if (s == tll::state::Opening || s == tll::state::Active)
			this->state(tll::state::Closing);
		return 0;
	}

	/// Channel close is finished
	int _on_closed()
	{
		if (this->state() == tll::state::Closing)
			Base<T>::_close();
		return 0;
	}
};

} // namespace tll::channel

#endif//_TLL_CHANNEL_PREFIX_H
