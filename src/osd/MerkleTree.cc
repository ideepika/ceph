//
// Created by root on 28/08/18.
//

#include "MerkleTree.h"

void MerkleTree::build_tree() {
  //reduce the leaves by building towards the root
  auto tmp_leaves = new MerkleNode*[leaf_len];
  std::memcpy(tmp_leaves, leaf_arr, leaf_len * sizeof(MerkleNode*));

  for (size_t stride = 2; stride <= leaf_len; stride <<= 1) {
    for (size_t i = 0; i < leaf_len; i += stride) {
      tmp_leaves[i] = new MerkleNode(tmp_leaves[i], tmp_leaves[i + stride/2]);
    }
  }

  root = tmp_leaves[0];
  built = true;

  delete[] tmp_leaves;
}

std::string MerkleTree::print_tree(MerkleNode* node) {
  std::string result;
  if(node) {
    result += print_tree(node->left);
    result += std::to_string(node->value) + ",";
    result += print_tree(node->right);
  }

  return result;
}

std::string MerkleTree::print_leaves() {
  std::string result;
  for(size_t i = 0; i < leaf_len; i++) {
      result += " "+std::to_string(leaf_arr[i]->value);
  }
  return result;
}

void MerkleTree::update_object(const hobject_t& hobj, const uint32_t& delta_hash)
{
  MerkleNode* node = root;

  int obj_pos = hobj.get_hash() >> reduce_bits; //chunk position

  std::vector<MerkleNode*> update_list;
  update_list.push_back(node);
  for (size_t bit = leaf_len >> 1; bit > 0; bit >>= 1) {
    node = obj_pos & bit ? node->right : node->left;
    update_list.push_back(node);
  }

  if (update_list[tree_depth]->value == 0) ranges_used += 1;
  if (obj_pos > omax) omax = obj_pos;
  if (obj_pos < omin) omin = obj_pos;
  objects += 1;

  update_list[tree_depth]->value ^= delta_hash;
  for (auto it = ++update_list.rbegin(); it != update_list.rend(); ++it) {
    (*it)->value = join_hash((*it)->left->value, (*it)->right->value);
  }
}

void MerkleTree::compare(MerkleTree& other, HashRangeIndex& range_idx, pg_shard_t from) {
  range_idx.clear();
  if(this->root->value == other.root->value) {
    return;
  }

  std::deque<std::pair<uint32_t, uint32_t>> skip_ranges;
  std::deque<std::pair<uint,uint> > range_diff;
  std::deque<MerkleNode*> lst1;
  std::deque<MerkleNode*> lst2;
  lst1.push_back(this->root->left);
  lst1.push_back(this->root->right);
  lst2.push_back(other.root->left);
  lst2.push_back(other.root->right);
  range_diff.emplace_back(0, leaf_len-1);

  bool isLeftChild = false;
  while (!lst1.empty()) {
    isLeftChild = !isLeftChild;
    auto curr1 = lst1.front();
    lst1.pop_front();
    auto curr2 = lst2.front();
    lst2.pop_front();

    if (curr1->value != curr2->value) {
      auto top = range_diff.front();
      if (curr1->left) { //if left child exists, right child also exists, because of full tree
        lst1.push_back(curr1->left);
        lst1.push_back(curr1->right);
        lst2.push_back(curr2->left);
        lst2.push_back(curr2->right);

        uint m = (top.first + top.second) / 2;
        range_diff.emplace_back(
                isLeftChild ? top.first : m + 1,
                isLeftChild ? m : top.second
        );
      } else {
        uint32_t val = isLeftChild ? top.first : top.second;
        uint32_t range_start = val << reduce_bits;
        uint32_t range_end = range_start + (1 << reduce_bits) - 1;
        skip_ranges.emplace_back(range_start, range_end);
      }
    }
    if (!isLeftChild) {
      range_diff.pop_front();
    }
  }

  auto it = skip_ranges.begin();
  while(it != skip_ranges.end()) {
    auto next = it+1;

    if(it->second+1 == next->first) {
      it->second = next->second;
      skip_ranges.erase(next);
    } else {
      ++it;
    }
  }
    
  size_t len = skip_ranges.size();
  skip_ranges.emplace_back(0, (uint32_t)-1);
  for (size_t i = 0; i < len; i++) {
    auto& beg = skip_ranges[i];
    auto& end = skip_ranges[len+i];
    auto swap_val = beg.second + 1 > beg.second ? beg.second + 1 : beg.second;
    skip_ranges.emplace_back(swap_val, end.second);
    end.second = beg.first > 0 ? beg.first - 1 : 0;
  }
  skip_ranges.erase(skip_ranges.begin(), skip_ranges.begin()+len);

  range_idx.fill(skip_ranges, from);
}

void MerkleNode::encode(bufferlist &bl) const {
  ENCODE_START(1, 1, bl)
  encode(value, bl);
  ENCODE_FINISH(bl)
}

void MerkleNode::decode(bufferlist::const_iterator &bl) {
  DECODE_START(1, bl)
  decode(value, bl);
  DECODE_FINISH(bl)
}

void MerkleTree::encode(bufferlist &bl) const {
  std::vector<uint64_t> v;
  v.reserve(leaf_len);

  for(size_t i = 0; i < leaf_len; ++i) {
    v.push_back(leaf_arr[i]->value);
  }

  ENCODE_START(1, 1, bl)
  encode(leaf_len, bl);
  encode(v, bl);
  ENCODE_FINISH(bl)
}

void MerkleTree::decode(bufferlist::const_iterator &bl) {
  std::vector<uint64_t> tmp;

  DECODE_START(1, bl)
  decode(leaf_len, bl);
  decode(tmp, bl);

  init(leaf_len);
  for(size_t i = 0; i < leaf_len; ++i) {
    leaf_arr[i]->value = tmp[i];
  }
  build_tree();
  DECODE_FINISH(bl)
}

void MerkleTree::update_leaf(hobject_t hobj, eversion_t version) {
  leaf_arr[hobj.get_hash() >> reduce_bits]->value ^= hash_pair(hobj, version);
}
