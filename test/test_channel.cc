/*
 * Copyright (c) 2019-2021 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "gtest/gtest.h"

#include "tll/compat/filesystem.h"

#include "tll/channel/base.h"
#include "tll/channel/prefix.h"
#include "tll/channel/reopen.h"
#include "tll/processor/loop.h"
#include "tll/util/ownedmsg.h"

class Null : public tll::channel::Base<Null>
{
 public:
	static constexpr std::string_view channel_protocol() { return "null"; }

	int _init(const tll::Channel::Url &, tll::Channel *master) { return 0; }

	int _process(long timeout, int flags) { return EAGAIN; }
	int _post(const tll_msg_t *msg, int flags) { return 0; }
};

TLL_DEFINE_IMPL(Null);

class Echo : public tll::channel::Base<Echo>
{
 public:
	static constexpr std::string_view channel_protocol() { return "echo"; }
	static constexpr auto open_policy() { return OpenPolicy::Manual; }
	static constexpr auto close_policy() { return ClosePolicy::Long; }

	std::optional<const tll_channel_impl_t *> _init_replace(const tll::Channel::Url &url, tll::Channel *master)
	{
		auto null = url.getT("null", false);
		if (null && *null)
			return &Null::impl;
		return nullptr;
	}

	int _open(const tll::PropsView &) { return 0; }
	int _close(bool force) { return 0; }

	int _post(const tll_msg_t *msg, int flags) { return _callback(msg); }
	int _process(long timeout, int flags)
	{
		if (state() == tll::state::Opening) {
			state(tll::state::Active);
			return 0;
		}
		if (state() == tll::state::Closing) {
			return tll::channel::Base<Echo>::_close();
		}
		return EAGAIN;
	}
};

TLL_DEFINE_IMPL(Echo);

class Prefix : public tll::channel::Prefix<Prefix>
{
 public:
	static constexpr std::string_view channel_protocol() { return "prefix+"; }
};

TLL_DEFINE_IMPL(Prefix);

class Reopen : public tll::channel::Reopen<Reopen>
{
	std::unique_ptr<tll::Channel> _child;

 public:
	using Base = tll::channel::Reopen<Reopen>;

	static constexpr std::string_view channel_protocol() { return "reopen"; }
	static constexpr auto process_policy() { return ProcessPolicy::Never; }

	int _init(const tll::Channel::Url &url, tll::Channel *master)
	{
		_child = context().channel(fmt::format("tcp://*:9;mode=client;name={}/child;tll.internal=yes", name));
		if (_child)
			_reopen_reset(_child.get());
		_child_add(_child.get(), "tcp");
		return Base::_init(url, master);
	}

	int _close()
	{
		_child->close();
		return Base::_close();
	}
};

TLL_DEFINE_IMPL(Reopen);

TEST(Channel, Register)
{
	auto cfg = tll::Config();
	auto ctx = tll::channel::Context(cfg);

	ASSERT_EQ(ctx.channel("echo://;name=echo"), nullptr);
	ASSERT_EQ(ctx.reg(&Echo::impl), 0);
	ASSERT_NE(ctx.reg(&Echo::impl), 0);

	ASSERT_EQ(ctx.channel("alias://;name=alias"), nullptr);
	ASSERT_EQ(ctx.reg(&Echo::impl, "alias"), 0);
	auto c = ctx.channel("alias://;name=alias");
	ASSERT_NE(c.get(), nullptr);
	c.reset();

	ASSERT_EQ(ctx.unreg(&Echo::impl, "alias"), 0);
	ASSERT_EQ(ctx.channel("alias://;name=echo").get(), nullptr);
	c = ctx.channel("echo://;name=echo");
	ASSERT_NE(c.get(), nullptr);
	c.reset();

	ASSERT_EQ(ctx.channel("prefix+echo://;name=echo"), nullptr);
	ASSERT_EQ(ctx.reg(&Prefix::impl), 0);

	c = ctx.channel("prefix+echo://;name=echo");
	ASSERT_NE(c.get(), nullptr);
	c.reset();

	ASSERT_EQ(ctx.unreg(&Echo::impl), 0);
	ASSERT_NE(ctx.unreg(&Echo::impl), 0);
}

void _test_channel(tll::channel::Context &ctx, std::string_view url, const tll_channel_impl_t * impl, std::string_view eurl = "")
{
	if (eurl.empty())
		eurl = url;

	auto process = [](tll::Channel *c) {
		if (c->children()) { // Prefix test
			return static_cast<tll::Channel *>(c->children()->channel)->process();
		} else
			return c->process();
	};

	auto c = ctx.channel(url);
	ASSERT_NE(c.get(), nullptr);
	ASSERT_EQ(c->impl, impl);
	ASSERT_EQ(c->state(), tll::state::Closed);
	ASSERT_EQ(c->open(), 0);
	ASSERT_EQ(c->state(), tll::state::Opening);
	ASSERT_EQ(process(c.get()), 0);
	ASSERT_EQ(c->state(), tll::state::Active);
	ASSERT_EQ(process(c.get()), EAGAIN);

	auto cfg = c->config();
	ASSERT_EQ(std::string(cfg.get("state").value_or("")), "Active");
	ASSERT_EQ(tll::conv::to_string(tll::Channel::Url(*cfg.sub("url"))), eurl);

	tll_msg_t msg = { TLL_MESSAGE_DATA };
	msg.seq = 100;

	long long rseq = 0;
	//auto rlambda = [&rseq](const tll::Channel *c, const tll_msg_t *m, void *) -> int { rseq = m->seq; return 0; };
	auto rlambda = [](const tll_channel_t *, const tll_msg_t *m, void * user) -> int { *(long long *) user = m->seq; return 0; };

	c->callback_add(rlambda, &rseq);

	c->post(&msg);
	ASSERT_EQ(rseq, msg.seq);

	c->close();
	ASSERT_EQ(c->state(), tll::state::Closing);
	process(c.get());
	ASSERT_EQ(c->state(), tll::state::Closed);
}

TEST(Channel, Echo)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);

	_test_channel(ctx, "echo://;name=echo", &Echo::impl);
}

TEST(Channel, PrefixEcho)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);
	ASSERT_EQ(ctx.reg(&Prefix::impl), 0);

	return _test_channel(ctx, "prefix+echo://;name=echo", &Prefix::impl);
}

TEST(Channel, AliasEcho)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.alias_reg("null", "zero://"), EEXIST);
	ASSERT_EQ(ctx.alias_reg("alias", "echo://"), ENOENT);
	ASSERT_EQ(ctx.alias_reg("alias", "echo://host"), EINVAL);
	ASSERT_EQ(ctx.alias_reg("alias", "echo://;name=name"), EINVAL);

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);
	ASSERT_EQ(ctx.alias_reg("alias", "echo://"), 0);
	ASSERT_EQ(ctx.alias_reg("alias", "echo://"), EEXIST);

	return _test_channel(ctx, "alias://;name=echo", &Echo::impl, "echo://;name=echo");
}

TEST(Channel, AliasPrefix)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);
	ASSERT_EQ(ctx.reg(&Prefix::impl), 0);

	ASSERT_EQ(ctx.alias_reg("alias+", "prefix+://"), 0);
	ASSERT_EQ(ctx.alias_reg("other", "echo://"), 0);

	return _test_channel(ctx, "alias+other://;name=echo", &Prefix::impl, "prefix+other://;name=echo");
}

TEST(Channel, AliasIndirect)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);
	ASSERT_EQ(ctx.reg(&Prefix::impl), 0);

	ASSERT_EQ(ctx.alias_reg("other+", "prefix+://"), 0);
	ASSERT_EQ(ctx.alias_reg("alias", "other+echo://"), 0);

	return _test_channel(ctx, "alias://;name=echo", &Prefix::impl, "prefix+echo://;name=echo");
}

TEST(Channel, AliasNull)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);
	ASSERT_EQ(ctx.alias_reg("alias", "echo://;null=yes"), 0);

	auto c = ctx.channel("alias://;name=alias");
	ASSERT_NE(c.get(), nullptr);
	ASSERT_EQ(c->impl, &Null::impl);
	ASSERT_EQ(tll::conv::to_string(tll::Channel::Url(*c->config().sub("url"))), "echo://;name=alias;null=yes");
}

TEST(Channel, InitReplace)
{
	auto ctx = tll::channel::Context(tll::Config());

	ASSERT_EQ(ctx.reg(&Echo::impl), 0);

	auto c = ctx.channel("echo://;name=echo-null;null=yes");
	ASSERT_NE(c.get(), nullptr);
	ASSERT_EQ(c->impl, &Null::impl);

	ASSERT_EQ(c->state(), tll::state::Closed);
	ASSERT_EQ(c->open(), 0);
	ASSERT_EQ(c->state(), tll::state::Active);
	ASSERT_EQ(c->process(), EAGAIN);
}

int poll_for(tll::Channel * c, tll::duration timeout = std::chrono::seconds(1))
{
	auto start = tll::time::now();
	while (tll::time::now() < start + timeout) {
		auto r = c->process();
		if (r != EAGAIN)
			return r;
		usleep(100);
	}
	return ETIMEDOUT;
}

class Accum
{
	std::unique_ptr<tll::Channel> _ptr;
public:
	using data_t = std::vector<tll::util::OwnedMessage>;
	data_t result;

	Accum(std::unique_ptr<tll::Channel> && ptr) : _ptr(std::move(ptr))
	{
		_ptr->callback_add([](const tll_channel_t *, const tll_msg_t *m, void * user) -> int { static_cast<data_t *>(user)->emplace_back(m); return 0; }, &result);
	}

	~Accum() { reset(); }


	void reset() { _ptr.reset(); }

	tll::Channel * operator -> () { return get(); }
	const tll::Channel * operator -> () const { return get(); }

	tll::Channel * get() { return _ptr.get(); }
	const tll::Channel * get() const { return _ptr.get(); }

	operator bool () const { return _ptr.get() != nullptr; }
};

TEST(Channel, Tcp)
{
	auto ctx = tll::channel::Context(tll::Config());
	Accum s = ctx.channel("tcp://./test-tcp.sock;mode=server;name=server;dump=yes");
	ASSERT_NE(s.get(), nullptr);

	{
		auto socket = std::filesystem::path("./test-tcp.sock");
		if (std::filesystem::exists(socket))
			std::filesystem::remove(socket);
	}

	s->open();

	ASSERT_EQ(s->state(), tll::state::Active);

	ASSERT_NE(s->children(), nullptr); // Only one server socket
	ASSERT_EQ(s->children()->next, nullptr);
	auto socket = static_cast<tll::Channel *>(s->children()->channel);

	Accum c0 = ctx.channel("tcp://./test-tcp.sock;mode=client;name=c0;dump=yes");
	Accum c1 = ctx.channel("tcp://./test-tcp.sock;mode=client;name=c1;dump=yes");
	ASSERT_NE(c0.get(), nullptr);
	ASSERT_NE(c1.get(), nullptr);

	c0->open();

	ASSERT_EQ(s->children()->next, nullptr);
	ASSERT_EQ(poll_for(socket), 0);
	ASSERT_NE(s->children()->next, nullptr);

	auto s0 = static_cast<tll::Channel *>(s->children()->next->channel);

	if (c0->state() == tll::state::Opening) { // Unix socket connects immediately
		ASSERT_EQ(poll_for(c0.get()), 0);
		ASSERT_EQ(c0->state(), tll::state::Active);
	}

	c1->open();

	ASSERT_EQ(s->children()->next->next, nullptr);
	ASSERT_EQ(poll_for(socket), 0);
	ASSERT_NE(s->children()->next->next, nullptr);

	auto s1 = static_cast<tll::Channel *>(s->children()->next->next->channel);

	if (c1->state() == tll::state::Opening) { // Unix socket connects immediately
		ASSERT_EQ(poll_for(c1.get()), 0);
		ASSERT_EQ(c1->state(), tll::state::Active);
	}

	ASSERT_EQ(s0->process(), EAGAIN);
	ASSERT_EQ(s1->process(), EAGAIN);

	tll_msg_t m = {};
	m.seq = 1;
	m.data = "xxx";
	m.size = 3;

	s.result.clear();
	c0.result.clear();
	c1.result.clear();

	ASSERT_EQ(c0->post(&m), 0);

	ASSERT_EQ(s.result.size(), 0u);

	{
		ASSERT_EQ(poll_for(s0), 0);
		ASSERT_EQ(s.result.size(), 1u);
		auto m = s.result.front();
		ASSERT_EQ(m.type, TLL_MESSAGE_DATA);
		ASSERT_EQ(m.seq, 1);
		ASSERT_EQ(std::string_view((char *) m.data, m.size), "xxx");
	}

	c0->process();
	c1->process();

	ASSERT_EQ(c0.result.size(), 0u);
	ASSERT_EQ(c1.result.size(), 0u);

	s.result[0].seq = 10;
	ASSERT_EQ(s->post(&s.result[0]), 0);

	ASSERT_EQ(poll_for(c0.get()), 0);

	{
		ASSERT_EQ(c0.result.size(), 1u);
		auto m = c0.result.front();
		ASSERT_EQ(m.type, TLL_MESSAGE_DATA);
		ASSERT_EQ(m.seq, 10);
		ASSERT_EQ(std::string_view((char *) m.data, m.size), "xxx");
	}

	c0->process();
	c1->process();

	ASSERT_EQ(c1.result.size(), 0u);
}

TEST(Channel, Reopen)
{
	auto ctx = tll::channel::Context(tll::Config());
	ASSERT_EQ(ctx.reg(&Reopen::impl), 0);
	Accum s = ctx.channel("reopen://;reopen-timeout-min=100ms;reopen-timeout-max=3s;name=reopen");
	ASSERT_NE(s.get(), nullptr);

	s->open();

	ASSERT_EQ(s->state(), tll::state::Active);

	ASSERT_NE(s->children(), nullptr);
	ASSERT_NE(s->children()->next, nullptr);
	ASSERT_EQ(s->children()->next->next, nullptr);
}
