// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 sts=2 expandtab

#pragma once

#include <cstdint>
#include <vector>

#include "common/hobject.h"
#include "include/encoding.h"
#include "osd/osd_types.h"

/**
 * BackfillDigest
 *
 * Per-PG digest of (object, version) pairs, split into leaves by the
 * bitwise (listing order) hash key, so each leaf is one contiguous
 * range of a collection listing. A leaf value is the XOR of a 64-bit
 * hash of every object in it, which lets log entries update it in
 * place. Leaves whose content changed by a path that does not carry
 * the old version (recovery, removal) are marked dirty and rebuilt by
 * listing just that range.
 *
 * Two OSDs whose clean leaf values match hold the same objects at the
 * same versions in that range, so backfill does not need to list it.
 */
class BackfillDigest {
public:
  static uint64_t object_hash(const hobject_t &o, const eversion_t &v);

  /// start over, all leaves dirty
  void reset(int64_t pool, uint32_t ps, unsigned split_bits,
	     unsigned leaf_bits);
  bool matches(int64_t pool, uint32_t ps, unsigned split_bits,
	       unsigned leaf_bits) const;
  bool empty() const { return leaves.empty(); }

  unsigned num_leaves() const { return leaves.size(); }
  unsigned get_leaf_bits() const { return leaf_bits; }
  unsigned get_split_bits() const { return split_bits; }
  uint32_t get_ps() const { return ps; }
  int64_t get_pool() const { return pool; }

  unsigned leaf_of(const hobject_t &o) const;
  /// first possible object of a leaf; leaf == num_leaves() is the end
  hobject_t leaf_begin(unsigned leaf) const;

  void toggle(const hobject_t &o, const eversion_t &v);
  void apply(const pg_log_entry_t &e);
  void mark_dirty(const hobject_t &o);
  void mark_all_dirty();

  bool is_dirty(unsigned leaf) const { return dirty[leaf]; }
  unsigned num_dirty() const;
  uint64_t get_leaf(unsigned leaf) const { return leaves[leaf]; }
  void set_leaf(unsigned leaf, uint64_t v) {
    leaves[leaf] = v;
    dirty[leaf] = 0;
  }
  const std::vector<uint64_t> &get_leaves() const { return leaves; }

  /// 1 for every leaf that is clean here and equal in other
  std::vector<uint8_t> compare(const std::vector<uint64_t> &other) const;

  void encode(ceph::buffer::list &bl) const;
  void decode(ceph::buffer::list::const_iterator &bl);

private:
  int64_t pool = -1;
  uint32_t ps = 0;
  unsigned split_bits = 0;
  unsigned leaf_bits = 0;
  std::vector<uint64_t> leaves;
  std::vector<uint8_t> dirty;
};
WRITE_CLASS_ENCODER(BackfillDigest)
