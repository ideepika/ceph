// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 sts=2 expandtab

#ifndef CEPH_MOSDPGBACKFILLDIGEST_H
#define CEPH_MOSDPGBACKFILLDIGEST_H

#include "MOSDFastDispatchOp.h"

/*
 * Backfill digest exchange. The primary sends its BackfillDigest leaves
 * to a backfill target; the target answers with the leaves it holds
 * identically, which both sides then leave out of their backfill scans.
 */
class MOSDPGBackfillDigest : public MOSDFastDispatchOp {
private:
  static constexpr int HEAD_VERSION = 1;
  static constexpr int COMPAT_VERSION = 1;

public:
  enum {
    OP_REQUEST = 1,
    OP_REPLY = 2,
  };
  static const char *get_op_name(int o) {
    switch (o) {
    case OP_REQUEST: return "request";
    case OP_REPLY: return "reply";
    default: return "???";
    }
  }

  __u32 op = 0;
  epoch_t map_epoch = 0, query_epoch = 0;
  pg_shard_t from;
  spg_t pgid;
  eversion_t since;		///< primary version the leaves describe
  __u32 split_bits = 0;
  __u32 leaf_bits = 0;
  std::vector<uint64_t> leaves;	///< request: primary's leaves
  std::vector<uint8_t> equal;	///< reply: leaves the target can skip

  epoch_t get_map_epoch() const override {
    return map_epoch;
  }
  epoch_t get_min_epoch() const override {
    return query_epoch;
  }
  spg_t get_spg() const override {
    return pgid;
  }

  MOSDPGBackfillDigest()
    : MOSDFastDispatchOp{MSG_OSD_PG_BACKFILL_DIGEST, HEAD_VERSION,
			 COMPAT_VERSION} {}
  MOSDPGBackfillDigest(__u32 o, pg_shard_t from, spg_t pgid, epoch_t map_e,
		       epoch_t query_e)
    : MOSDFastDispatchOp{MSG_OSD_PG_BACKFILL_DIGEST, HEAD_VERSION,
			 COMPAT_VERSION},
      op(o), map_epoch(map_e), query_epoch(query_e), from(from),
      pgid(pgid) {}
private:
  ~MOSDPGBackfillDigest() final {}

public:
  std::string_view get_type_name() const override {
    return "pg_backfill_digest";
  }
  void print(std::ostream &out) const override {
    out << "pg_backfill_digest(" << get_op_name(op) << " " << pgid
	<< " e " << map_epoch << "/" << query_epoch
	<< " since " << since << " leaves " << leaves.size()
	<< " equal " << equal.size() << ")";
  }

  void encode_payload(uint64_t features) override {
    using ceph::encode;
    encode(op, payload);
    encode(map_epoch, payload);
    encode(query_epoch, payload);
    encode(from, payload);
    encode(pgid, payload);
    encode(since, payload);
    encode(split_bits, payload);
    encode(leaf_bits, payload);
    encode(leaves, payload);
    encode(equal, payload);
  }
  void decode_payload() override {
    using ceph::decode;
    auto p = payload.cbegin();
    decode(op, p);
    decode(map_epoch, p);
    decode(query_epoch, p);
    decode(from, p);
    decode(pgid, p);
    decode(since, p);
    decode(split_bits, p);
    decode(leaf_bits, p);
    decode(leaves, p);
    decode(equal, p);
  }

private:
  template<class T, typename... Args>
  friend boost::intrusive_ptr<T> ceph::make_message(Args&&... args);
};

#endif
