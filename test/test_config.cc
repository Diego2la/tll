/*
 * Copyright (c) 2018-2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "gtest/gtest.h"

#include "tll/config.h"

#include <fmt/format.h>

TEST(Config, Get)
{
	tll::Config cfg;
	ASSERT_FALSE(cfg.has("a.b.c"));

	cfg.set("a.b.c", "");
	ASSERT_TRUE(cfg.has("a.b.c"));
	ASSERT_EQ(*cfg.get("a.b.c"), "");

	cfg.set("a.b.c", "1");
	ASSERT_TRUE(cfg.has("a.b.c"));
	ASSERT_EQ(*cfg.get("a.b.c"), "1");

	auto sub = cfg.sub("a.b");
	ASSERT_TRUE(sub);
	ASSERT_TRUE(sub->has("c"));
	ASSERT_EQ(*sub->get("c"), "1");

	sub->set("c", "2");

	ASSERT_EQ(*sub->get("c"), "2");
	ASSERT_EQ(*cfg.get("a.b.c"), "2");

	int v = 10;
	cfg.set_ptr("a.b.d", &v);

	ASSERT_EQ(*sub->get("d"), "10");
	v = 20;
	ASSERT_EQ(*sub->get("d"), "20");
}

template <typename T>
void compare_keys(const std::map<std::string, T> &m, std::list<std::string_view> l)
{
	std::list<std::string_view> r;
	for (auto & i : m) r.push_back(i.first);
	ASSERT_EQ(r, l);
}

TEST(Config, Browse)
{

	auto c = tll::Config::load("yamls://{a: 1, b: 2, c: [10, 20, 30], x: {y: {z: string}}}");
	ASSERT_TRUE(c);
	compare_keys<tll::Config>(c->browse("**"), {"a", "b", "c.0000", "c.0001", "c.0002", "x.y.z"});
	compare_keys<tll::Config>(c->list(), {"a", "b", "c", "x"});

	std::optional<tll::ConstConfig> s = c->sub("x");
	ASSERT_TRUE(s);

	compare_keys<tll::ConstConfig>(s->browse("**"), {"y.z"});
	compare_keys<tll::ConstConfig>(s->list(), {"y"});
}

TEST(Config, Copy)
{
	auto c = tll::Config::load("yamls://{a: 1, b: 2, c: [10, 20, 30], x: {y: {z: string}}}");
	ASSERT_TRUE(c);
	compare_keys<tll::Config>(c->browse("**"), {"a", "b", "c.0000", "c.0001", "c.0002", "x.y.z"});

	auto c1 = c->copy();
	compare_keys<tll::Config>(c->browse("**"), {"a", "b", "c.0000", "c.0001", "c.0002", "x.y.z"});

	c->set("a", "987");
	c->set("x.y.z", "str");
	ASSERT_EQ(*c1.get("a"), "1");
	ASSERT_EQ(*c1.get("x.y.z"), "string");
}

TEST(Config, Merge)
{

	auto c = tll::Config::load("yamls://{a: 1, b.c: 1}");
	ASSERT_TRUE(c);
	auto c1 = tll::Config::load("yamls://b.d: 2");
	ASSERT_TRUE(c1);

	ASSERT_EQ(c->merge(*c1), 0);

	compare_keys(c->browse("**"), {"a", "b.c", "b.d"});
}
