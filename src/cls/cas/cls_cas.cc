// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <errno.h>

#include "objclass/objclass.h"
#include "cls_cas_ops.h"
#include "cls_cas_internal.h"

#include "include/compat.h"
#include "osd/osd_types.h"

using ceph::bufferlist;
using ceph::decode;

CLS_VER(1,0)
CLS_NAME(cas)


//
// helpers
//

static int chunk_read_refcount(
  cls_method_context_t hctx,
  chunk_obj_refcount *objr)
{
  bufferlist bl;
  objr->refs.clear();
  int ret = cls_cxx_getxattr(hctx, CHUNK_REFCOUNT_ATTR, &bl);
  if (ret == -ENODATA) {
    return 0;
  }
  if (ret < 0)
    return ret;

  try {
    auto iter = bl.cbegin();
    decode(*objr, iter);
  } catch (ceph::buffer::error& err) {
    CLS_LOG(0, "ERROR: chunk_read_refcount(): failed to decode refcount entry\n");
    return -EIO;
  }

  return 0;
}

static int chunk_set_refcount(
  cls_method_context_t hctx,
  const struct chunk_obj_refcount& objr)
{
  bufferlist bl;

  encode(objr, bl);

  int ret = cls_cxx_setxattr(hctx, CHUNK_REFCOUNT_ATTR, &bl);
  if (ret < 0)
    return ret;

  return 0;
}


//
// methods
//

static int chunk_create_or_get_ref(cls_method_context_t hctx,
				   bufferlist *in, bufferlist *out)
{
  auto in_iter = in->cbegin();

  cls_cas_chunk_create_or_get_ref_op op;
  try {
    decode(op, in_iter);
  } catch (ceph::buffer::error& err) {
    CLS_LOG(1, "ERROR: failed to decode entry\n");
    return -EINVAL;
  }

  chunk_obj_refcount objr;
  int ret = chunk_read_refcount(hctx, &objr);
  if (ret == -ENOENT) {
    // new chunk; init refs
    CLS_LOG(10, "create oid=%s\n",
	    op.source.oid.name.c_str());
    ret = cls_cxx_write_full(hctx, &op.data);
    if (ret < 0) {
      return ret;
    }
    objr.refs.insert(op.source);
    ret = chunk_set_refcount(hctx, objr);
    if (ret < 0) {
      return ret;
    }
  } else if (ret < 0) {
    return ret;
  } else {
    // existing chunk; inc ref
    if (op.flags & cls_cas_chunk_create_or_get_ref_op::FLAG_VERIFY) {
      bufferlist old;
      cls_cxx_read(hctx, 0, 0, &old);
      if (!old.contents_equal(op.data)) {
	return -ENOMSG;
      }
    }
    CLS_LOG(10, "inc ref oid=%s\n",
	    op.source.oid.name.c_str());

    if (objr.refs.count(op.source)) {
      return -EEXIST;
    }
    objr.refs.insert(op.source);

    ret = chunk_set_refcount(hctx, objr);
    if (ret < 0) {
      return ret;
    }
  }
  return 0;
}

static int chunk_get_ref(cls_method_context_t hctx,
			 bufferlist *in, bufferlist *out)
{
  auto in_iter = in->cbegin();

  cls_cas_chunk_get_ref_op op;
  try {
    decode(op, in_iter);
  } catch (ceph::buffer::error& err) {
    CLS_LOG(1, "ERROR: failed to decode entry\n");
    return -EINVAL;
  }

  chunk_obj_refcount objr;
  int ret = chunk_read_refcount(hctx, &objr);
  if (ret < 0) {
    CLS_LOG(1, "ERROR: failed to read attr\n");
    return ret;
  }

  // existing chunk; inc ref
  CLS_LOG(10, "oid=%s\n", op.source.oid.name.c_str());
  
  if (objr.refs.count(op.source)) {
    return -EEXIST;
  }
  objr.refs.insert(op.source);

  ret = chunk_set_refcount(hctx, objr);
  if (ret < 0) {
    return ret;
  }
  return 0;
}

static int chunk_put_ref(cls_method_context_t hctx,
			 bufferlist *in, bufferlist *out)
{
  auto in_iter = in->cbegin();

  cls_cas_chunk_put_ref_op op;
  try {
    decode(op, in_iter);
  } catch (ceph::buffer::error& err) {
    CLS_LOG(1, "ERROR: failed to decode entry\n");
    return -EINVAL;
  }

  chunk_obj_refcount objr;
  int ret = chunk_read_refcount(hctx, &objr);
  if (ret < 0)
    return ret;

  if (objr.refs.empty()) {// shouldn't happen!
    CLS_LOG(0, "ERROR was called without any references!\n");
    return -ENOLINK;
  }

  auto p = objr.refs.find(op.source);
  if (p == objr.refs.end()) {
    CLS_LOG(10, "oid=%s (no ref)\n", op.source.oid.name.c_str());
    return -ENOLINK;
  }
  objr.refs.erase(p);

  if (objr.refs.empty()) {
    CLS_LOG(10, "oid=%s (last ref)\n", op.source.oid.name.c_str());
    return cls_cxx_remove(hctx);
  }

  CLS_LOG(10, "oid=%s (dec)\n", op.source.oid.name.c_str());
  ret = chunk_set_refcount(hctx, objr);
  if (ret < 0)
    return ret;

  return 0;
}

static int chunk_set_refs(cls_method_context_t hctx,
			  bufferlist *in, bufferlist *out)
{
  auto in_iter = in->cbegin();

  cls_cas_chunk_set_refs_op op;
  try {
    decode(op, in_iter);
  } catch (ceph::buffer::error& err) {
    CLS_LOG(1, "ERROR: cls_cas_chunk_set(): failed to decode entry\n");
    return -EINVAL;
  }

  if (!op.refs.size()) {
    return cls_cxx_remove(hctx);
  }

  chunk_obj_refcount objr;
  objr.refs = op.refs;

  int ret = chunk_set_refcount(hctx, objr);
  if (ret < 0)
    return ret;

  return 0;
}

static int chunk_read_refs(cls_method_context_t hctx,
			   bufferlist *in, bufferlist *out)
{
  chunk_obj_refcount objr;

  cls_cas_chunk_read_refs_ret read_ret;
  int ret = chunk_read_refcount(hctx, &objr);
  if (ret < 0)
    return ret;

  for (auto &p : objr.refs) {
    read_ret.refs.insert(p);
  }

  encode(read_ret, *out);

  return 0;
}

static int references_chunk(cls_method_context_t hctx,
			    bufferlist *in, bufferlist *out)
{
  auto in_iter = in->cbegin();
  std::string fp_oid;
  bufferlist indata, outdata;
  try {
    decode (fp_oid, in_iter);
  }
  catch (ceph::buffer::error& e) {
    return -EINVAL;
  }
  CLS_LOG(10, "fp_oid: %s \n", fp_oid.c_str());

  bool ret = cls_has_chunk(hctx, fp_oid);
  if (ret) {
    return 0;
  }
  return -ENOENT;
}

CLS_INIT(cas)
{
  CLS_LOG(1, "Loaded cas class!");

  cls_handle_t h_class;
  cls_method_handle_t h_chunk_create_or_get_ref;
  cls_method_handle_t h_chunk_get_ref;
  cls_method_handle_t h_chunk_put_ref;
  cls_method_handle_t h_chunk_set_refs;
  cls_method_handle_t h_chunk_read_refs;
  cls_method_handle_t h_references_chunk;

  cls_register("cas", &h_class);

  cls_register_cxx_method(h_class, "chunk_create_or_get_ref",
			  CLS_METHOD_RD | CLS_METHOD_WR,
			  chunk_create_or_get_ref,
			  &h_chunk_create_or_get_ref);
  cls_register_cxx_method(h_class, "chunk_get_ref",
			  CLS_METHOD_RD | CLS_METHOD_WR,
			  chunk_get_ref,
			  &h_chunk_get_ref);
  cls_register_cxx_method(h_class, "chunk_put_ref",
			  CLS_METHOD_RD | CLS_METHOD_WR,
			  chunk_put_ref,
			  &h_chunk_put_ref);
  cls_register_cxx_method(h_class, "chunk_set_refs",
			  CLS_METHOD_RD | CLS_METHOD_WR,
			  chunk_set_refs,
			  &h_chunk_set_refs);
  cls_register_cxx_method(h_class, "chunk_read_refs", CLS_METHOD_RD,
			  chunk_read_refs,
			  &h_chunk_read_refs);
  cls_register_cxx_method(h_class, "references_chunk", CLS_METHOD_RD,
			  references_chunk,
			  &h_references_chunk);

  return;
}

