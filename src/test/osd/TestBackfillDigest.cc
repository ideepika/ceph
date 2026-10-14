// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include <map>
#include <random>

#include <gtest/gtest.h>

#include "osd/BackfillDigest.h"

namespace {

constexpr int64_t POOL = 3;

hobject_t make_obj(std::mt19937 &rng, uint32_t ps, unsigned split_bits,
		   snapid_t snap = CEPH_NOSNAP)
{
  uint32_t hash = rng();
  if (split_bits) {
    uint32_t low = split_bits >= 32 ? 0xffffffff : (1u << split_bits) - 1;
    hash = (hash & ~low) | (ps & low);
  }
  return hobject_t(object_t("obj" + std::to_string(rng())), "", snap, hash,
		   POOL, "");
}

pg_log_entry_t entry(int op, const hobject_t &o, eversion_t v,
		     eversion_t prior)
{
  return pg_log_entry_t(op, o, v, prior, 0, osd_reqid_t(), utime_t(), 0);
}

// digest built the way refresh does it: one pass over current content
BackfillDigest scan(const std::map<hobject_t, eversion_t> &content,
		    uint32_t ps, unsigned split_bits, unsigned leaf_bits)
{
  BackfillDigest d;
  d.reset(POOL, ps, split_bits, leaf_bits);
  std::vector<uint64_t> v(d.num_leaves(), 0);
  for (const auto &[o, ver] : content) {
    v[d.leaf_of(o)] ^= BackfillDigest::object_hash(o, ver);
  }
  for (unsigned i = 0; i < v.size(); ++i) {
    d.set_leaf(i, v[i]);
  }
  return d;
}

} // namespace

class BackfillDigestLayout
  : public ::testing::TestWithParam<std::tuple<unsigned, unsigned>> {};

TEST_P(BackfillDigestLayout, LeavesAreContiguousListingRanges)
{
  auto [split_bits, leaf_bits] = GetParam();
  std::mt19937 rng(split_bits * 100 + leaf_bits);
  const uint32_t ps = split_bits ? rng() & ((1u << split_bits) - 1) : 0;
  BackfillDigest d;
  d.reset(POOL, ps, split_bits, leaf_bits);
  ASSERT_EQ(d.num_leaves(), 1u << d.get_leaf_bits());
  ASSERT_TRUE(d.leaf_begin(d.num_leaves()).is_max());

  std::vector<hobject_t> objs;
  for (int i = 0; i < 5000; ++i) {
    objs.push_back(make_obj(rng, ps, split_bits));
  }
  std::sort(objs.begin(), objs.end());
  unsigned last = 0;
  for (const auto &o : objs) {
    unsigned leaf = d.leaf_of(o);
    ASSERT_LT(leaf, d.num_leaves());
    EXPECT_LE(d.leaf_begin(leaf), o) << o << " leaf " << leaf;
    EXPECT_LT(o, d.leaf_begin(leaf + 1)) << o << " leaf " << leaf;
    // listing order never goes back to an earlier leaf
    EXPECT_GE(leaf, last);
    last = leaf;
  }
  for (unsigned i = 0; i + 1 < d.num_leaves(); ++i) {
    EXPECT_LT(d.leaf_begin(i), d.leaf_begin(i + 1));
  }
}

INSTANTIATE_TEST_SUITE_P(
  Layouts, BackfillDigestLayout,
  ::testing::Combine(::testing::Values(0u, 1u, 3u, 7u, 12u),
		     ::testing::Values(0u, 1u, 4u, 10u, 30u)));

TEST(BackfillDigest, LeafBitsClampToHashSpace)
{
  BackfillDigest d;
  d.reset(POOL, 5, 28, 10);
  EXPECT_EQ(d.get_leaf_bits(), 4u);
  d.reset(POOL, 0, 0, 30);
  EXPECT_EQ(d.get_leaf_bits(), 20u);
}

TEST(BackfillDigest, LogEntriesMatchRescan)
{
  const uint32_t ps = 5;
  const unsigned split_bits = 3, leaf_bits = 6;
  std::mt19937 rng(1);
  std::map<hobject_t, eversion_t> content;
  BackfillDigest d;
  d.reset(POOL, ps, split_bits, leaf_bits);
  for (unsigned i = 0; i < d.num_leaves(); ++i) {
    d.set_leaf(i, 0);
  }

  std::vector<hobject_t> heads;
  version_t ver = 0;
  for (int step = 0; step < 4000; ++step) {
    eversion_t v(1, ++ver);
    int what = heads.empty() ? 0 : rng() % 4;
    if (what == 0) {
      auto o = make_obj(rng, ps, split_bits);
      heads.push_back(o);
      d.apply(entry(pg_log_entry_t::MODIFY, o, v, eversion_t()));
      content[o] = v;
    } else if (what == 1) {
      auto &o = heads[rng() % heads.size()];
      if (!content.count(o)) {
	continue;
      }
      d.apply(entry(pg_log_entry_t::MODIFY, o, v, content[o]));
      content[o] = v;
    } else if (what == 2) {
      size_t i = rng() % heads.size();
      auto o = heads[i];
      if (!content.count(o)) {
	continue;
      }
      d.apply(entry(pg_log_entry_t::DELETE, o, v, content[o]));
      content.erase(o);
      heads.erase(heads.begin() + i);
    } else {
      // clone before a write: clone created at v, head written after
      auto &o = heads[rng() % heads.size()];
      if (!content.count(o)) {
	continue;
      }
      hobject_t c = o;
      c.snap = snapid_t(ver);
      d.apply(entry(pg_log_entry_t::CLONE, c, v, content[o]));
      content[c] = v;
      eversion_t v2(1, ++ver);
      d.apply(entry(pg_log_entry_t::MODIFY, o, v2, content[o]));
      content[o] = v2;
    }
    // errors never change content
    d.apply(entry(pg_log_entry_t::ERROR, heads.empty() ?
		  make_obj(rng, ps, split_bits) : heads[0],
		  eversion_t(1, ++ver), eversion_t()));
  }

  auto truth = scan(content, ps, split_bits, leaf_bits);
  EXPECT_EQ(d.get_leaves(), truth.get_leaves());
  auto eq = d.compare(truth.get_leaves());
  EXPECT_EQ(std::count(eq.begin(), eq.end(), 1), (long)d.num_leaves());
}

TEST(BackfillDigest, CompareFlagsOnlyCleanEqualLeaves)
{
  const uint32_t ps = 2;
  std::mt19937 rng(7);
  std::map<hobject_t, eversion_t> a;
  for (int i = 0; i < 2000; ++i) {
    a[make_obj(rng, ps, 2)] = eversion_t(1, i + 1);
  }
  auto b = a;
  auto changed = b.begin();
  std::advance(changed, 500);
  changed->second = eversion_t(2, 9999);
  auto removed = b.begin();
  std::advance(removed, 1500);
  b.erase(removed);

  auto da = scan(a, ps, 2, 5);
  auto db = scan(b, ps, 2, 5);
  auto eq = da.compare(db.get_leaves());
  unsigned leaf_changed = da.leaf_of(changed->first);
  EXPECT_EQ(eq[leaf_changed], 0);
  EXPECT_EQ(std::count(eq.begin(), eq.end(), 1),
	    (long)da.num_leaves() - (leaf_changed == da.leaf_of(
	      std::next(a.begin(), 1500)->first) ? 1 : 2));

  da.mark_dirty(a.begin()->first);
  eq = da.compare(db.get_leaves());
  EXPECT_EQ(eq[da.leaf_of(a.begin()->first)], 0);

  EXPECT_EQ(std::count(eq.begin(), eq.end(), 1),
	    std::count(eq.begin(), eq.end(), 1));
  auto none = da.compare(std::vector<uint64_t>(3, 0));
  EXPECT_EQ(std::count(none.begin(), none.end(), 1), 0);
}

TEST(BackfillDigest, DistinguishesObjectsAndVersions)
{
  hobject_t a(object_t("a"), "", CEPH_NOSNAP, 1, POOL, "");
  hobject_t b(object_t("b"), "", CEPH_NOSNAP, 1, POOL, "");
  hobject_t a_snap(object_t("a"), "", 4, 1, POOL, "");
  hobject_t a_ns(object_t("a"), "", CEPH_NOSNAP, 1, POOL, "ns");
  eversion_t v(3, 7);
  std::set<uint64_t> h = {
    BackfillDigest::object_hash(a, v), BackfillDigest::object_hash(b, v),
    BackfillDigest::object_hash(a_snap, v),
    BackfillDigest::object_hash(a_ns, v),
    BackfillDigest::object_hash(a, eversion_t(3, 8)),
    BackfillDigest::object_hash(a, eversion_t(4, 7)),
  };
  EXPECT_EQ(h.size(), 6u);
}

TEST(BackfillDigest, LostEntriesMarkDirty)
{
  BackfillDigest d;
  d.reset(POOL, 0, 0, 4);
  for (unsigned i = 0; i < d.num_leaves(); ++i) {
    d.set_leaf(i, 0);
  }
  hobject_t o(object_t("x"), "", CEPH_NOSNAP, 0x1234, POOL, "");
  d.apply(entry(pg_log_entry_t::LOST_DELETE, o, eversion_t(1, 2),
		eversion_t(1, 1)));
  EXPECT_TRUE(d.is_dirty(d.leaf_of(o)));
  EXPECT_EQ(d.num_dirty(), 1u);
}

TEST(BackfillDigest, EncodeDecode)
{
  std::mt19937 rng(3);
  std::map<hobject_t, eversion_t> c;
  for (int i = 0; i < 300; ++i) {
    c[make_obj(rng, 1, 1)] = eversion_t(1, i + 1);
  }
  auto d = scan(c, 1, 1, 7);
  d.mark_dirty(c.begin()->first);
  ceph::buffer::list bl;
  encode(d, bl);
  BackfillDigest e;
  auto p = bl.cbegin();
  decode(e, p);
  EXPECT_TRUE(e.matches(POOL, 1, 1, 7));
  EXPECT_EQ(e.get_leaves(), d.get_leaves());
  EXPECT_EQ(e.num_dirty(), 1u);
  EXPECT_FALSE(e.matches(POOL, 1, 2, 7));
  EXPECT_FALSE(e.matches(POOL, 0, 1, 7));

  BackfillDigest empty;
  EXPECT_TRUE(empty.empty());
  EXPECT_FALSE(empty.matches(POOL, 1, 1, 7));
}
