// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 sts=2 expandtab

#include "osd/BackfillDigest.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "xxHash/xxhash.h"

uint64_t BackfillDigest::object_hash(const hobject_t &o, const eversion_t &v)
{
  std::string b;
  b.reserve(o.oid.name.size() + o.get_key().size() + o.nspace.size() + 40);
  b.append(o.oid.name);
  b.push_back('\0');
  b.append(o.get_key());
  b.push_back('\0');
  b.append(o.nspace);
  b.push_back('\0');
  auto put = [&b](const auto &x) {
    b.append(reinterpret_cast<const char *>(&x), sizeof(x));
  };
  put(uint64_t(o.snap));
  put(uint32_t(o.get_hash()));
  put(uint64_t(o.pool));
  put(uint32_t(v.epoch));
  put(uint64_t(v.version));
  return XXH64(b.data(), b.size(), 0);
}

void BackfillDigest::reset(int64_t _pool, uint32_t _ps, unsigned _split_bits,
			   unsigned _leaf_bits)
{
  pool = _pool;
  ps = _ps;
  split_bits = std::min(_split_bits, 32u);
  leaf_bits = std::min({_leaf_bits, 32u - split_bits, 20u});
  leaves.assign(1u << leaf_bits, 0);
  dirty.assign(1u << leaf_bits, 1);
}

bool BackfillDigest::matches(int64_t _pool, uint32_t _ps,
			     unsigned _split_bits, unsigned _leaf_bits) const
{
  return !leaves.empty() && pool == _pool && ps == _ps &&
    split_bits == std::min(_split_bits, 32u) &&
    leaf_bits == std::min({_leaf_bits, 32u - split_bits, 20u});
}

unsigned BackfillDigest::leaf_of(const hobject_t &o) const
{
  if (leaf_bits == 0) {
    return 0;
  }
  uint32_t below = uint32_t(uint64_t(o.get_bitwise_key_u32()) << split_bits);
  return below >> (32 - leaf_bits);
}

hobject_t BackfillDigest::leaf_begin(unsigned leaf) const
{
  if (leaf >= num_leaves()) {
    return hobject_t::get_max();
  }
  uint32_t prefix = split_bits ?
    hobject_t::_reverse_bits(ps) & ~uint32_t(uint64_t(0xffffffff) >> split_bits) :
    0;
  uint32_t key = prefix;
  if (leaf_bits) {
    key |= leaf << (32 - split_bits - leaf_bits);
  }
  hobject_t h;
  h.pool = pool;
  h.set_bitwise_key_u32(key);
  return h;
}

void BackfillDigest::toggle(const hobject_t &o, const eversion_t &v)
{
  if (leaves.empty()) {
    return;
  }
  leaves[leaf_of(o)] ^= object_hash(o, v);
}

void BackfillDigest::apply(const pg_log_entry_t &e)
{
  if (leaves.empty() || e.is_error()) {
    return;
  }
  switch (e.op) {
  case pg_log_entry_t::LOST_REVERT:
  case pg_log_entry_t::LOST_DELETE:
  case pg_log_entry_t::LOST_MARK:
    mark_dirty(e.soid);
    return;
  default:
    break;
  }
  if (e.is_delete()) {
    if (e.prior_version != eversion_t()) {
      toggle(e.soid, e.prior_version);
    }
  } else if (e.is_update()) {
    if (e.op != pg_log_entry_t::CLONE && e.prior_version != eversion_t()) {
      toggle(e.soid, e.prior_version);
    }
    toggle(e.soid, e.version);
  }
}

void BackfillDigest::mark_dirty(const hobject_t &o)
{
  if (!leaves.empty()) {
    dirty[leaf_of(o)] = 1;
  }
}

void BackfillDigest::mark_all_dirty()
{
  std::fill(dirty.begin(), dirty.end(), 1);
}

unsigned BackfillDigest::num_dirty() const
{
  return std::count(dirty.begin(), dirty.end(), 1);
}

std::vector<uint8_t> BackfillDigest::compare(
  const std::vector<uint64_t> &other) const
{
  std::vector<uint8_t> eq(leaves.size(), 0);
  if (other.size() != leaves.size()) {
    return eq;
  }
  for (unsigned i = 0; i < leaves.size(); ++i) {
    eq[i] = !dirty[i] && leaves[i] == other[i];
  }
  return eq;
}

void BackfillDigest::encode(ceph::buffer::list &bl) const
{
  ENCODE_START(1, 1, bl);
  encode(pool, bl);
  encode(ps, bl);
  encode(split_bits, bl);
  encode(leaf_bits, bl);
  encode(leaves, bl);
  encode(dirty, bl);
  ENCODE_FINISH(bl);
}

void BackfillDigest::decode(ceph::buffer::list::const_iterator &bl)
{
  DECODE_START(1, bl);
  decode(pool, bl);
  decode(ps, bl);
  decode(split_bits, bl);
  decode(leaf_bits, bl);
  decode(leaves, bl);
  decode(dirty, bl);
  DECODE_FINISH(bl);
  if (leaves.size() != (leaf_bits < 32 ? (1ull << leaf_bits) : 0) ||
      dirty.size() != leaves.size()) {
    leaves.clear();
    dirty.clear();
  }
}
