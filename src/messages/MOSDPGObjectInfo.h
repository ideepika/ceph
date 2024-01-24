// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef CEPH_MOSDPGOBJECTINFO_H
#define CEPH_MOSDPGOBJECTINFO_H

#include "MOSDFastDispatchOp.h"

/*
 * PGObjectInfo - request another osd of their object info
 * to determine which ranges should be recovered.
 */

class MOSDPGObjectInfo : public MOSDFastDispatchOp {
private:
  static constexpr int HEAD_VERSION = 1;
  static constexpr int COMPAT_VERSION = 1;

public:
  enum {
    OP_GET_DIFF = 1,
    OP_HEAD = 2,
    OP_FULL = 3
  };
  const char *get_op_name(int o) const {
    switch (o) {
      case OP_GET_DIFF: return "get diff";
      case OP_HEAD: return "head eq reply";
      case OP_FULL: return "full diff reply";
      default: return "???";
    }
  }

  epoch_t get_map_epoch() const override {
    return map_epoch;
  }
  epoch_t get_min_epoch() const override {
    return query_epoch;
  }
  spg_t get_spg() const override {
    return pgid;
  }

  __u32 op = 0;
  epoch_t map_epoch = 0, query_epoch = 0;
  pg_shard_t from;
  spg_t pgid;

  MOSDPGObjectInfo()
    : MOSDFastDispatchOp{MSG_OSD_PG_OBJECT_INFO, HEAD_VERSION, COMPAT_VERSION} {}
  MOSDPGObjectInfo(__u32 o, pg_shard_t from, spg_t pgid, epoch_t map_e, epoch_t query_e)
    : MOSDFastDispatchOp{MSG_OSD_PG_OBJECT_INFO, HEAD_VERSION, COMPAT_VERSION},
      op(o),
      map_epoch(map_e), query_epoch(query_e),
      from(from),
      pgid(pgid) {}
private:
  ~MOSDPGObjectInfo() override {}

public:  
  std::string_view get_type_name() const override { return "pg_objects_info_check"; }
  void print(ostream& out) const override {
    out << "pg_objects_info_check(" << get_op_name(op) << ": ";
    out <<  pgid << ", ";
    out << "e " << map_epoch << "/" << query_epoch;
    out << ")";
  }

  void encode_payload(uint64_t features) override {
    using ceph::encode;
    encode(op, payload);
    encode(from, payload);
    encode(map_epoch, payload);
    encode(query_epoch, payload);
    encode(pgid.pgid, payload);
    encode(pgid.shard, payload);
  }
  void decode_payload() override {
    auto p = payload.cbegin();
    decode(op, p);
    decode(from, p);
    decode(map_epoch, p);
    decode(query_epoch, p);
    decode(pgid.pgid, p);
    decode(pgid.shard, p);
  }

private:
  template<class T, typename... Args>
  friend boost::intrusive_ptr<T> ceph::make_message(Args&&... args);
};

#endif