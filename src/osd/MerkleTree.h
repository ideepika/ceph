//
// Created by root on 28/08/18.
//

#ifndef CEPH_MERKLETREE_H
#define CEPH_MERKLETREE_H

#include <cstdint>
#include <utility>
#include <list>
#include "include/buffer.h"
#include "xxHash/xxhash.h"
#include "common/hobject.h"
#include "osd/osd_types.h"
#include "HashRangeIndex.h"

#define XXSEED 0

inline uint64_t join_hash(uint64_t l, uint64_t r) {
  uint64_t joined[2] = { l, r };
  return XXH64(joined, sizeof(joined), XXSEED);
}

inline uint64_t hash_pair(hobject_t hobj, eversion_t version) {
  size_t result_hash_size = sizeof(uint32_t) + sizeof(version_t) + sizeof(epoch_t);
  uint32_t hash = hobj.get_hash();
  char buffer[result_hash_size];
  char *pt = buffer;
  memcpy(pt, &hash, sizeof(uint32_t));
  pt+=sizeof(uint32_t);
  memcpy(pt, &version.version, sizeof(version_t));
  pt+=sizeof(version_t);
  memcpy(pt, &version.epoch, sizeof(epoch_t));
  return XXH64(buffer, result_hash_size, XXSEED);
}

class MerkleNode {
public:
  uint64_t value = 0;
  MerkleNode* left = 0;
  MerkleNode* right = 0;

  MerkleNode() = default;
  MerkleNode(const MerkleNode& n) = default;
  MerkleNode(uint64_t value) {
    this->value = value;
  }

  MerkleNode(MerkleNode* l, MerkleNode* r) : left(l), right(r) {
    this->value = join_hash(l->value, r->value);
  }

  ~MerkleNode() {
    if (left) delete left;
    if (right) delete right;
  }

  void encode(bufferlist &bl) const;
  void decode(bufferlist::const_iterator &bl);
};
WRITE_CLASS_ENCODER(MerkleNode)

class MerkleTree {
private:
  size_t leaf_len; //must be a power of 2
  size_t tree_depth;
  size_t reduce_bits;

  MerkleNode* root;
  MerkleNode** leaf_arr;

  void init(size_t len) {
    omin = len;
    leaf_len = len;
    leaf_arr = new MerkleNode*[len];
    for (size_t i = 0; i < len; ++i) {
      leaf_arr[i] = new MerkleNode;
    }
    tree_depth = static_cast<size_t>(log2(len));
    reduce_bits = sizeof(decltype(hobject_t().get_hash())) * 8 - tree_depth; //decltype in case hobject_t::hash type changes
    root = nullptr;
  }

public:
  MerkleTree() = default;
  explicit MerkleTree(size_t len) {
    init(len);
  }

  ~MerkleTree() {
    delete root;
  }

  MerkleNode* get_root() {
    return root;
  }
  int objects = 0;
  int ranges_used = 0;
  int omin = 0;
  int omax = 0;

  bool built = false;
  void build_tree();
  void update_leaf(hobject_t hobj, eversion_t version);
  std::string print_leaves();
  std::string print_tree(MerkleNode* node);
  void update_object(const hobject_t& hobj, const uint32_t& delta_hash);
  void compare(MerkleTree& other, HashRangeIndex& range_idx, pg_shard_t from);
  void encode(bufferlist &bl) const;
  void decode(bufferlist::const_iterator &bl);

  size_t size() const {
    return leaf_len*2-1;
  }
};
WRITE_CLASS_ENCODER(MerkleTree)

#endif //CEPH_MERKLETREE_H