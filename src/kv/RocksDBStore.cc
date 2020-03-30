// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <set>
#include <map>
#include <string>
#include <memory>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "rocksdb/db.h"
#include "rocksdb/table.h"
#include "rocksdb/env.h"
#include "rocksdb/slice.h"
#include "rocksdb/cache.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/utilities/convenience.h"
#include "rocksdb/merge_operator.h"

#include "common/perf_counters.h"
#include "common/PriorityCache.h"
#include "include/common_fwd.h"
#include "include/str_list.h"
#include "include/stringify.h"
#include "include/str_map.h"
#include "KeyValueDB.h"
#include "RocksDBStore.h"

#include "common/debug.h"

#define dout_context cct
#define dout_subsys ceph_subsys_rocksdb
#undef dout_prefix
#define dout_prefix *_dout << "rocksdb: "

using std::function;
using std::list;
using std::map;
using std::ostream;
using std::pair;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

using ceph::bufferlist;
using ceph::bufferptr;
using ceph::Formatter;

static const char* sharding_def_dir = "sharding";
static const char* sharding_def_file = "sharding/def";
static const char* sharding_recreate = "sharding/recreate_columns";

static bufferlist to_bufferlist(rocksdb::Slice in) {
  bufferlist bl;
  bl.append(bufferptr(in.data(), in.size()));
  return bl;
}

static rocksdb::SliceParts prepare_sliceparts(const bufferlist &bl,
					      vector<rocksdb::Slice> *slices)
{
  unsigned n = 0;
  for (auto& buf : bl.buffers()) {
    (*slices)[n].data_ = buf.c_str();
    (*slices)[n].size_ = buf.length();
    n++;
  }
  return rocksdb::SliceParts(slices->data(), slices->size());
}


//
// One of these for the default rocksdb column family, routing each prefix
// to the appropriate MergeOperator.
//
class RocksDBStore::MergeOperatorRouter
  : public rocksdb::AssociativeMergeOperator
{
  RocksDBStore& store;
public:
  const char *Name() const override {
    // Construct a name that rocksDB will validate against. We want to
    // do this in a way that doesn't constrain the ordering of calls
    // to set_merge_operator, so sort the merge operators and then
    // construct a name from all of those parts.
    store.assoc_name.clear();
    map<std::string,std::string> names;

    for (auto& p : store.merge_ops) {
      names[p.first] = p.second->name();
    }
    for (auto& p : store.cf_handles) {
      names.erase(p.first);
    }
    for (auto& p : names) {
      store.assoc_name += '.';
      store.assoc_name += p.first;
      store.assoc_name += ':';
      store.assoc_name += p.second;
    }
    return store.assoc_name.c_str();
  }

  explicit MergeOperatorRouter(RocksDBStore &_store) : store(_store) {}

  bool Merge(const rocksdb::Slice& key,
	     const rocksdb::Slice* existing_value,
	     const rocksdb::Slice& value,
	     std::string* new_value,
	     rocksdb::Logger* logger) const override {
    // for default column family
    // extract prefix from key and compare against each registered merge op;
    // even though merge operator for explicit CF is included in merge_ops,
    // it won't be picked up, since it won't match.
    for (auto& p : store.merge_ops) {
      if (p.first.compare(0, p.first.length(),
			  key.data(), p.first.length()) == 0 &&
	  key.data()[p.first.length()] == 0) {
	if (existing_value) {
	  p.second->merge(existing_value->data(), existing_value->size(),
			  value.data(), value.size(),
			  new_value);
	} else {
	  p.second->merge_nonexistent(value.data(), value.size(), new_value);
	}
	break;
      }
    }
    return true; // OK :)
  }
};

//
// One of these per non-default column family, linked directly to the
// merge operator for that CF/prefix (if any).
//
class RocksDBStore::MergeOperatorLinker
  : public rocksdb::AssociativeMergeOperator
{
private:
  std::shared_ptr<KeyValueDB::MergeOperator> mop;
public:
  explicit MergeOperatorLinker(const std::shared_ptr<KeyValueDB::MergeOperator> &o) : mop(o) {}

  const char *Name() const override {
    return mop->name();
  }

  bool Merge(const rocksdb::Slice& key,
	     const rocksdb::Slice* existing_value,
	     const rocksdb::Slice& value,
	     std::string* new_value,
	     rocksdb::Logger* logger) const override {
    if (existing_value) {
      mop->merge(existing_value->data(), existing_value->size(),
		 value.data(), value.size(),
		 new_value);
    } else {
      mop->merge_nonexistent(value.data(), value.size(), new_value);
    }
    return true;
  }
};

int RocksDBStore::set_merge_operator(
  const string& prefix,
  std::shared_ptr<KeyValueDB::MergeOperator> mop)
{
  // If you fail here, it's because you can't do this on an open database
  ceph_assert(db == nullptr);
  merge_ops.push_back(std::make_pair(prefix,mop));
  return 0;
}

class CephRocksdbLogger : public rocksdb::Logger {
  CephContext *cct;
public:
  explicit CephRocksdbLogger(CephContext *c) : cct(c) {
    cct->get();
  }
  ~CephRocksdbLogger() override {
    cct->put();
  }

  // Write an entry to the log file with the specified format.
  void Logv(const char* format, va_list ap) override {
    Logv(rocksdb::INFO_LEVEL, format, ap);
  }

  // Write an entry to the log file with the specified log level
  // and format.  Any log with level under the internal log level
  // of *this (see @SetInfoLogLevel and @GetInfoLogLevel) will not be
  // printed.
  void Logv(const rocksdb::InfoLogLevel log_level, const char* format,
	    va_list ap) override {
    int v = rocksdb::NUM_INFO_LOG_LEVELS - log_level - 1;
    dout(ceph::dout::need_dynamic(v));
    char buf[65536];
    vsnprintf(buf, sizeof(buf), format, ap);
    *_dout << buf << dendl;
  }
};

rocksdb::Logger *create_rocksdb_ceph_logger()
{
  return new CephRocksdbLogger(g_ceph_context);
}

static int string2bool(const string &val, bool &b_val)
{
  if (strcasecmp(val.c_str(), "false") == 0) {
    b_val = false;
    return 0;
  } else if (strcasecmp(val.c_str(), "true") == 0) {
    b_val = true;
    return 0;
  } else {
    std::string err;
    int b = strict_strtol(val.c_str(), 10, &err);
    if (!err.empty())
      return -EINVAL;
    b_val = !!b;
    return 0;
  }
}
  
int RocksDBStore::tryInterpret(const string &key, const string &val, rocksdb::Options &opt)
{
  if (key == "compaction_threads") {
    std::string err;
    int f = strict_iecstrtoll(val.c_str(), &err);
    if (!err.empty())
      return -EINVAL;
    //Low priority threadpool is used for compaction
    opt.env->SetBackgroundThreads(f, rocksdb::Env::Priority::LOW);
  } else if (key == "flusher_threads") {
    std::string err;
    int f = strict_iecstrtoll(val.c_str(), &err);
    if (!err.empty())
      return -EINVAL;
    //High priority threadpool is used for flusher
    opt.env->SetBackgroundThreads(f, rocksdb::Env::Priority::HIGH);
  } else if (key == "compact_on_mount") {
    int ret = string2bool(val, compact_on_mount);
    if (ret != 0)
      return ret;
  } else if (key == "disableWAL") {
    int ret = string2bool(val, disableWAL);
    if (ret != 0)
      return ret;
  } else {
    //unrecognize config options.
    return -EINVAL;
  }
  return 0;
}

int RocksDBStore::ParseOptionsFromString(const string &opt_str, rocksdb::Options &opt)
{
  return ParseOptionsFromStringStatic(cct, opt_str, opt,
    [&](const string& k, const string& v, rocksdb::Options& o) {
      return tryInterpret(k, v, o);
    }
  );
}

int RocksDBStore::ParseOptionsFromStringStatic(
  CephContext *cct,
  const string& opt_str,
  rocksdb::Options& opt,
  function<int(const string&, const string&, rocksdb::Options&)> interp)
{
  // keep aligned with func tryInterpret
  const set<string> need_interp_keys = {"compaction_threads", "flusher_threads", "compact_on_mount", "disableWAL"};

  map<string, string> str_map;
  int r = get_str_map(opt_str, &str_map, ",\n;");
  if (r < 0)
    return r;
  map<string, string>::iterator it;
  for (it = str_map.begin(); it != str_map.end(); ++it) {
    string this_opt = it->first + "=" + it->second;
    rocksdb::Status status =
      rocksdb::GetOptionsFromString(opt, this_opt, &opt);
    if (!status.ok()) {
      if (interp != nullptr) {
	r = interp(it->first, it->second, opt);
      } else if (!need_interp_keys.count(it->first)) {
	r = -1;
      }
      if (r < 0) {
        derr << status.ToString() << dendl;
        return -EINVAL;
      }
    }
    lgeneric_dout(cct, 0) << " set rocksdb option " << it->first
      << " = " << it->second << dendl;
  }
  return 0;
}

int RocksDBStore::init(string _options_str)
{
  options_str = _options_str;
  rocksdb::Options opt;
  //try parse options
  if (options_str.length()) {
    int r = ParseOptionsFromString(options_str, opt);
    if (r != 0) {
      return -EINVAL;
    }
  }
  return 0;
}

int RocksDBStore::create_db_dir()
{
  if (env) {
    unique_ptr<rocksdb::Directory> dir;
    env->NewDirectory(path, &dir);
  } else {
    int r = ::mkdir(path.c_str(), 0755);
    if (r < 0)
      r = -errno;
    if (r < 0 && r != -EEXIST) {
      derr << __func__ << " failed to create " << path << ": " << cpp_strerror(r)
	   << dendl;
      return r;
    }
  }
  return 0;
}

int RocksDBStore::install_cf_mergeop(
  const string &key_prefix,
  rocksdb::ColumnFamilyOptions *cf_opt)
{
  ceph_assert(cf_opt != nullptr);
  cf_opt->merge_operator.reset();
  for (auto& i : merge_ops) {
    if (i.first == key_prefix) {
      cf_opt->merge_operator.reset(new MergeOperatorLinker(i.second));
    }
  }
  return 0;
}

int RocksDBStore::create_and_open(ostream &out,
				  const std::string& cfs)
{
  int r = create_db_dir();
  if (r < 0)
    return r;
  return do_open(out, true, false, cfs);
}

int RocksDBStore::load_rocksdb_options(bool create_if_missing, rocksdb::Options& opt)
{
  rocksdb::Status status;

  if (options_str.length()) {
    int r = ParseOptionsFromString(options_str, opt);
    if (r != 0) {
      return -EINVAL;
    }
  }

  if (cct->_conf->rocksdb_perf)  {
    dbstats = rocksdb::CreateDBStatistics();
    opt.statistics = dbstats;
  }

  opt.create_if_missing = create_if_missing;
  if (kv_options.count("separate_wal_dir")) {
    opt.wal_dir = path + ".wal";
  }

  // Since ceph::for_each_substr doesn't return a value and
  // std::stoull does throw, we may as well just catch everything here.
  try {
    if (kv_options.count("db_paths")) {
      list<string> paths;
      get_str_list(kv_options["db_paths"], "; \t", paths);
      for (auto& p : paths) {
	size_t pos = p.find(',');
	if (pos == std::string::npos) {
	  derr << __func__ << " invalid db path item " << p << " in "
	       << kv_options["db_paths"] << dendl;
	  return -EINVAL;
	}
	string path = p.substr(0, pos);
	string size_str = p.substr(pos + 1);
	uint64_t size = atoll(size_str.c_str());
	if (!size) {
	  derr << __func__ << " invalid db path item " << p << " in "
	       << kv_options["db_paths"] << dendl;
	  return -EINVAL;
	}
	opt.db_paths.push_back(rocksdb::DbPath(path, size));
	dout(10) << __func__ << " db_path " << path << " size " << size << dendl;
      }
    }
  } catch (const std::system_error& e) {
    return -e.code().value();
  }

  if (cct->_conf->rocksdb_log_to_ceph_log) {
    opt.info_log.reset(new CephRocksdbLogger(cct));
  }

  if (priv) {
    dout(10) << __func__ << " using custom Env " << priv << dendl;
    opt.env = static_cast<rocksdb::Env*>(priv);
  }

  opt.env->SetAllowNonOwnerAccess(false);

  // caches
  if (!set_cache_flag) {
    cache_size = cct->_conf->rocksdb_cache_size;
  }
  uint64_t row_cache_size = cache_size * cct->_conf->rocksdb_cache_row_ratio;
  uint64_t block_cache_size = cache_size - row_cache_size;

  if (cct->_conf->rocksdb_cache_type == "binned_lru") {
    bbt_opts.block_cache = rocksdb_cache::NewBinnedLRUCache(
      cct,
      block_cache_size,
      cct->_conf->rocksdb_cache_shard_bits);
  } else if (cct->_conf->rocksdb_cache_type == "lru") {
    bbt_opts.block_cache = rocksdb::NewLRUCache(
      block_cache_size,
      cct->_conf->rocksdb_cache_shard_bits);
  } else if (cct->_conf->rocksdb_cache_type == "clock") {
    bbt_opts.block_cache = rocksdb::NewClockCache(
      block_cache_size,
      cct->_conf->rocksdb_cache_shard_bits);
    if (!bbt_opts.block_cache) {
      derr << "rocksdb_cache_type '" << cct->_conf->rocksdb_cache_type
           << "' chosen, but RocksDB not compiled with LibTBB. "
           << dendl;
      return -EINVAL;
    }
  } else {
    derr << "unrecognized rocksdb_cache_type '" << cct->_conf->rocksdb_cache_type
      << "'" << dendl;
    return -EINVAL;
  }
  bbt_opts.block_size = cct->_conf->rocksdb_block_size;

  if (row_cache_size > 0)
    opt.row_cache = rocksdb::NewLRUCache(row_cache_size,
				     cct->_conf->rocksdb_cache_shard_bits);
  uint64_t bloom_bits = cct->_conf.get_val<uint64_t>("rocksdb_bloom_bits_per_key");
  if (bloom_bits > 0) {
    dout(10) << __func__ << " set bloom filter bits per key to "
	     << bloom_bits << dendl;
    bbt_opts.filter_policy.reset(rocksdb::NewBloomFilterPolicy(bloom_bits));
  }
  using std::placeholders::_1;
  if (cct->_conf.with_val<std::string>("rocksdb_index_type",
				    std::bind(std::equal_to<std::string>(), _1,
					      "binary_search")))
    bbt_opts.index_type = rocksdb::BlockBasedTableOptions::IndexType::kBinarySearch;
  if (cct->_conf.with_val<std::string>("rocksdb_index_type",
				    std::bind(std::equal_to<std::string>(), _1,
					      "hash_search")))
    bbt_opts.index_type = rocksdb::BlockBasedTableOptions::IndexType::kHashSearch;
  if (cct->_conf.with_val<std::string>("rocksdb_index_type",
				    std::bind(std::equal_to<std::string>(), _1,
					      "two_level")))
    bbt_opts.index_type = rocksdb::BlockBasedTableOptions::IndexType::kTwoLevelIndexSearch;
  if (!bbt_opts.no_block_cache) {
    bbt_opts.cache_index_and_filter_blocks =
        cct->_conf.get_val<bool>("rocksdb_cache_index_and_filter_blocks");
    bbt_opts.cache_index_and_filter_blocks_with_high_priority =
        cct->_conf.get_val<bool>("rocksdb_cache_index_and_filter_blocks_with_high_priority");
    bbt_opts.pin_l0_filter_and_index_blocks_in_cache =
      cct->_conf.get_val<bool>("rocksdb_pin_l0_filter_and_index_blocks_in_cache");
  }
  bbt_opts.partition_filters = cct->_conf.get_val<bool>("rocksdb_partition_filters");
  if (cct->_conf.get_val<Option::size_t>("rocksdb_metadata_block_size") > 0)
    bbt_opts.metadata_block_size = cct->_conf.get_val<Option::size_t>("rocksdb_metadata_block_size");

  opt.table_factory.reset(rocksdb::NewBlockBasedTableFactory(bbt_opts));
  dout(10) << __func__ << " block size " << cct->_conf->rocksdb_block_size
           << ", block_cache size " << byte_u_t(block_cache_size)
	   << ", row_cache size " << byte_u_t(row_cache_size)
	   << "; shards "
	   << (1 << cct->_conf->rocksdb_cache_shard_bits)
	   << ", type " << cct->_conf->rocksdb_cache_type
	   << dendl;

  opt.merge_operator.reset(new MergeOperatorRouter(*this));
  comparator = opt.comparator;
  return 0;
}

void RocksDBStore::add_column_family(const std::string& cf_name, uint32_t hash_l, uint32_t hash_h,
				     size_t shard_idx, rocksdb::ColumnFamilyHandle *handle) {
  dout(10) << __func__ << " column_name=" << cf_name << " shard_idx=" << shard_idx <<
    " hash_l=" << hash_l << " hash_h=" << hash_h << " handle=" << (void*) handle << dendl;
  bool exists = cf_handles.count(cf_name) > 0;
  auto& column = cf_handles[cf_name];
  if (exists) {
    ceph_assert(hash_l == column.hash_l);
    ceph_assert(hash_h == column.hash_h);
  } else {
    ceph_assert(hash_l < hash_h);
    column.hash_l = hash_l;
    column.hash_h = hash_h;
  }
  if (column.handles.size() <= shard_idx)
    column.handles.resize(shard_idx + 1);
  column.handles[shard_idx] = handle;
}

bool RocksDBStore::is_column_family(const std::string& prefix) {
  return cf_handles.count(prefix);
}

rocksdb::ColumnFamilyHandle *RocksDBStore::get_cf_handle(const std::string& prefix, const std::string& key) {
  auto iter = cf_handles.find(prefix);
  if (iter == cf_handles.end()) {
    return nullptr;
  } else {
    if (iter->second.handles.size() == 1) {
      return iter->second.handles[0];
    } else {
      uint32_t hash_l = std::min<uint32_t>(iter->second.hash_l, key.size());
      uint32_t hash_h = std::min<uint32_t>(iter->second.hash_h, key.size());
      uint32_t hash = ceph_str_hash_rjenkins(&key[hash_l], hash_h - hash_l);
      return iter->second.handles[hash % iter->second.handles.size()];
    }
  }
}

rocksdb::ColumnFamilyHandle *RocksDBStore::get_cf_handle(const std::string& prefix, const char* key, size_t keylen) {
  auto iter = cf_handles.find(prefix);
  if (iter == cf_handles.end()) {
    return nullptr;
  } else {
    if (iter->second.handles.size() == 1) {
      return iter->second.handles[0];
    } else {
      uint32_t hash_l = std::min<uint32_t>(iter->second.hash_l, keylen);
      uint32_t hash_h = std::min<uint32_t>(iter->second.hash_h, keylen);
      uint32_t hash = ceph_str_hash_rjenkins(&key[hash_l], hash_h - hash_l);
      return iter->second.handles[hash % iter->second.handles.size()];
    }
  }
}

/**
 * Definition of sharding:
 * space-separated list of: column_def [ '=' options ]
 * column_def := column_name '(' shard_count ')'
 * column_def := column_name '(' shard_count ',' hash_begin '-' ')'
 * column_def := column_name '(' shard_count ',' hash_begin '-' hash_end ')'
 * I=write_buffer_size=1048576 O(6) m(7,10-) prefix(4,0-10)=disable_auto_compactions=true,max_bytes_for_level_base=1048576
 */
bool RocksDBStore::parse_sharding_def(const std::string_view text_def_in,
				     std::vector<ColumnFamily>& sharding_def,
				     char const* *error_position,
				     std::string *error_msg)
{
  std::string_view text_def = text_def_in;
  char const* error_position_local = nullptr;
  std::string error_msg_local;
  if (error_position == nullptr) {
    error_position = &error_position_local;
  }
  *error_position = nullptr;
  if (error_msg == nullptr) {
    error_msg = &error_msg_local;
    error_msg->clear();
  }

  sharding_def.clear();
  while (!text_def.empty()) {
    std::string_view options;
    std::string_view name;
    size_t shard_cnt = 1;
    uint32_t l_bound = 0;
    uint32_t h_bound = std::numeric_limits<uint32_t>::max();

    std::string_view column_def;
    size_t spos = text_def.find(' ');
    if (spos == std::string_view::npos) {
      column_def = text_def;
      text_def = std::string_view(text_def.end(), 0);
    } else {
      column_def = text_def.substr(0, spos);
      text_def = text_def.substr(spos + 1);
    }
    size_t eqpos = column_def.find('=');
    if (eqpos != std::string_view::npos) {
      options = column_def.substr(eqpos + 1);
      column_def = column_def.substr(0, eqpos);
    }

    std::string_view shards_def;
    size_t bpos = column_def.find('(');
    if (bpos != std::string_view::npos) {
      name = column_def.substr(0, bpos);
      const char* nptr = &column_def[bpos + 1];
      char* endptr;
      shard_cnt = strtol(nptr, &endptr, 10);
      if (nptr == endptr) {
	*error_position = nptr;
	*error_msg = "expecting integer";
	break;
      }
      nptr = endptr;
      if (*nptr == ',') {
	nptr++;
	l_bound = strtol(nptr, &endptr, 10);
	if (nptr == endptr) {
	  *error_position = nptr;
	  *error_msg = "expecting integer";
	  break;
	}
	nptr = endptr;
	if (*nptr != '-') {
	  *error_position = nptr;
	  *error_msg = "expecting '-'";
	  break;
	}
	nptr++;
	h_bound = strtol(nptr, &endptr, 10);
	if (nptr == endptr) {
	  h_bound = std::numeric_limits<uint32_t>::max();
	}
	nptr = endptr;
      }
      if (*nptr != ')') {
	*error_position = nptr;
	*error_msg = "expecting ')'";
	break;
      }
    } else {
      name = column_def;
    }
    sharding_def.emplace_back(std::string(name), shard_cnt, std::string(options), l_bound, h_bound);
  }
  return *error_position == nullptr;
}

void RocksDBStore::sharding_def_to_columns(const std::vector<ColumnFamily>& sharding_def,
					  std::vector<std::string>& columns)
{
  columns.clear();
  for (size_t i = 0; i < sharding_def.size(); i++) {
    if (sharding_def[i].shard_cnt == 1) {
	columns.push_back(sharding_def[i].name);
    } else {
      for (size_t j = 0; j < sharding_def[i].shard_cnt; j++) {
	columns.push_back(sharding_def[i].name + "-" + to_string(j));
      }
    }
  }
}

int RocksDBStore::create_shards(const rocksdb::Options& opt,
				const std::vector<ColumnFamily>& sharding_def)
{
  for (auto& p : sharding_def) {
    // copy default CF settings, block cache, merge operators as
    // the base for new CF
    rocksdb::ColumnFamilyOptions cf_opt(opt);
    // user input options will override the base options
    rocksdb::Status status;
    status = rocksdb::GetColumnFamilyOptionsFromString(
						       cf_opt, p.options, &cf_opt);
    if (!status.ok()) {
      derr << __func__ << " invalid db column family option string for CF: "
	   << p.name << dendl;
      return -EINVAL;
    }
    install_cf_mergeop(p.name, &cf_opt);
    for (size_t idx = 0; idx < p.shard_cnt; idx++) {
      std::string cf_name;
      if (p.shard_cnt == 1)
	cf_name = p.name;
      else
	cf_name = p.name + "-" + to_string(idx);
      rocksdb::ColumnFamilyHandle *cf;
      status = db->CreateColumnFamily(cf_opt, cf_name, &cf);
      if (!status.ok()) {
	derr << __func__ << " Failed to create rocksdb column family: "
	     << cf_name << dendl;
	return -EINVAL;
      }
      // store the new CF handle
      add_column_family(p.name, p.hash_l, p.hash_h, idx, cf);
    }
  }
  return 0;
}

int RocksDBStore::apply_sharding(const rocksdb::Options& opt,
				 const std::string& sharding_text)
{
  // create and open column families
  if (!sharding_text.empty()) {
    bool b;
    int r;
    rocksdb::Status status;
    std::vector<ColumnFamily> sharding_def;
    char const* error_position;
    std::string error_msg;
    b = parse_sharding_def(sharding_text, sharding_def, &error_position, &error_msg);
    if (!b) {
      dout(1) << __func__ << " bad sharding: " << dendl;
      dout(1) << __func__ << sharding_text << dendl;
      dout(1) << __func__ << std::string(error_position - &sharding_text[0], ' ') << "^" << error_msg << dendl;
      return -EINVAL;
    }
    r = create_shards(opt, sharding_def);
    if (r != 0 ) {
      return r;
    }
    opt.env->CreateDir(sharding_def_dir);
    status = rocksdb::WriteStringToFile(opt.env, sharding_text,
					sharding_def_file, true);
    if (!status.ok()) {
      derr << __func__ << " cannot write to " << sharding_def_file << dendl;
      return -EIO;
    }
  } else {
    opt.env->DeleteFile(sharding_def_file);
  }
  return 0;
}

int RocksDBStore::verify_sharding(const rocksdb::Options& opt,
				  const std::string& sharding_text,
				  std::vector<rocksdb::ColumnFamilyDescriptor>& existing_cfs,
				  std::vector<std::pair<size_t, RocksDBStore::ColumnFamily> >& existing_cfs_shard,
				  std::vector<rocksdb::ColumnFamilyDescriptor>& missing_cfs,
				  std::vector<std::pair<size_t, RocksDBStore::ColumnFamily> >& missing_cfs_shard)
{
  rocksdb::Status status;
  std::string stored_sharding_text;
  status = opt.env->FileExists(sharding_def_file);
  if (status.ok()) {
    status = rocksdb::ReadFileToString(opt.env,
				       sharding_def_file,
				       &stored_sharding_text);
    if(!status.ok()) {
      derr << __func__ << " cannot read from " << sharding_def_file << dendl;
      return -EIO;
    }
  } else {
    //no "sharding_def" present
  }
  //check if sharding_def matches stored_sharding_def
  std::vector<ColumnFamily> sharding_def;
  std::vector<ColumnFamily> stored_sharding_def;
  parse_sharding_def(sharding_text, sharding_def);
  parse_sharding_def(stored_sharding_text, stored_sharding_def);

  std::sort(sharding_def.begin(), sharding_def.end(),
	    [](ColumnFamily& a, ColumnFamily& b) { return a.name < b.name; } );
  std::sort(stored_sharding_def.begin(), stored_sharding_def.end(),
	    [](ColumnFamily& a, ColumnFamily& b) { return a.name < b.name; } );

  bool match = true;
  if (sharding_def.size() != stored_sharding_def.size()) {
    match = false;
  } else {
    for (size_t i = 0; i < sharding_def.size(); i++) {
      auto& a = sharding_def[i];
      auto& b = stored_sharding_def[i];
      if ( (a.name != b.name) ||
	   (a.shard_cnt != b.shard_cnt) ||
	   (a.hash_l != b.hash_l) ||
	   (a.hash_h != b.hash_h) ) {
	match = false;
	break;
      }
    }
  }
  if (!match) {
    derr << __func__ << " mismatch on sharding. requested = " << sharding_def
	 << " stored = " << stored_sharding_def << dendl;
    return -EIO;
  }
  std::vector<string> rocksdb_cfs;
  status = rocksdb::DB::ListColumnFamilies(rocksdb::DBOptions(opt),
					   path, &rocksdb_cfs);
  if (!status.ok()) {
    return -EIO;
  }
  dout(5) << __func__ << " column families from rocksdb: " << rocksdb_cfs << dendl;

  auto emplace_cf = [&] (const RocksDBStore::ColumnFamily& column,
			 int32_t shard_id,
			 const std::string& shard_name,
			 const rocksdb::ColumnFamilyOptions& opt) {
    if (std::find(rocksdb_cfs.begin(), rocksdb_cfs.end(), shard_name) != rocksdb_cfs.end()) {
      existing_cfs.emplace_back(shard_name, opt);
      existing_cfs_shard.emplace_back(shard_id, column);
    } else {
      missing_cfs.emplace_back(shard_name, opt);
      missing_cfs_shard.emplace_back(shard_id, column);
    }
  };

  for (auto& column : stored_sharding_def) {
    rocksdb::ColumnFamilyOptions cf_opt(opt);
    status = rocksdb::GetColumnFamilyOptionsFromString(
						       cf_opt, column.options, &cf_opt);
    if (!status.ok()) {
      derr << __func__ << " invalid db column family options for CF '"
	   << column.name << "': " << column.options << dendl;
      return -EINVAL;
    }
    install_cf_mergeop(column.name, &cf_opt);

    if (column.shard_cnt == 1) {
      emplace_cf(column, 0, column.name, cf_opt);
    } else {
      for (size_t i = 0; i < column.shard_cnt; i++) {
	std::string cf_name = column.name + "-" + to_string(i);
	emplace_cf(column, i, cf_name, cf_opt);
      }
    }
  }
  existing_cfs.emplace_back("default", opt);

 if (existing_cfs.size() != rocksdb_cfs.size()) {
   std::vector<std::string> columns_from_stored;
   sharding_def_to_columns(stored_sharding_def, columns_from_stored);
   derr << __func__ << " extra columns in rocksdb. rocksdb columns = " << rocksdb_cfs
	<< " target columns = " << columns_from_stored << dendl;
   return -EIO;
 }
  return 0;
}

std::ostream& operator<<(std::ostream& out, const RocksDBStore::ColumnFamily& cf)
{
  out << "(";
  out << cf.name;
  out << ",";
  out << cf.shard_cnt;
  out << ",";
  out << cf.hash_l;
  out << "-";
  if (cf.hash_h != std::numeric_limits<uint32_t>::max()) {
    out << cf.hash_h;
  }
  out << ",";
  out << cf.options;
  out << ")";
  return out;
}

int RocksDBStore::do_open(ostream &out,
			  bool create_if_missing,
			  bool open_readonly,
			  const std::string& sharding_text)
{
  ceph_assert(!(create_if_missing && open_readonly));
  rocksdb::Options opt;
  int r = load_rocksdb_options(create_if_missing, opt);
  if (r) {
    dout(1) << __func__ << " load rocksdb options failed" << dendl;
    return r;
  }
  rocksdb::Status status;
  if (create_if_missing) {
    status = rocksdb::DB::Open(opt, path, &db);
    if (!status.ok()) {
      derr << status.ToString() << dendl;
      return -EINVAL;
    }
    r = apply_sharding(opt, sharding_text);
    if (r < 0) {
      return r;
    }
    default_cf = db->DefaultColumnFamily();
  } else {
    std::vector<rocksdb::ColumnFamilyDescriptor> existing_cfs;
    std::vector<std::pair<size_t, RocksDBStore::ColumnFamily> > existing_cfs_shard;
    std::vector<rocksdb::ColumnFamilyDescriptor> missing_cfs;
    std::vector<std::pair<size_t, RocksDBStore::ColumnFamily> > missing_cfs_shard;

    r = verify_sharding(opt, sharding_text,
			existing_cfs, existing_cfs_shard,
			missing_cfs, missing_cfs_shard);
    if (r < 0) {
      return r;
    }
    std::string sharding_recreate_text;
    status = rocksdb::ReadFileToString(opt.env,
				       sharding_recreate,
				       &sharding_recreate_text);
    bool recreate_mode = status.ok() && sharding_recreate_text == "1";

    if (recreate_mode == false && missing_cfs.size() != 0) {
      derr << __func__ << " missing column families: " << missing_cfs_shard << dendl;
      return -EIO;
    }

    if (existing_cfs.empty()) {
      // no column families
      if (open_readonly) {
	status = rocksdb::DB::Open(opt, path, &db);
      } else {
	status = rocksdb::DB::OpenForReadOnly(opt, path, &db);
      }
      if (!status.ok()) {
	derr << status.ToString() << dendl;
	return -EINVAL;
      }
      default_cf = db->DefaultColumnFamily();
    } else {
      std::vector<rocksdb::ColumnFamilyHandle*> handles;
      if (open_readonly) {
        status = rocksdb::DB::OpenForReadOnly(rocksdb::DBOptions(opt),
				              path, existing_cfs,
					      &handles, &db);
      } else {
        status = rocksdb::DB::Open(rocksdb::DBOptions(opt),
				   path, existing_cfs, &handles, &db);
      }
      if (!status.ok()) {
	derr << status.ToString() << dendl;
	return -EINVAL;
      }
      ceph_assert(existing_cfs.size() == existing_cfs_shard.size() + 1);
      ceph_assert(handles.size() == existing_cfs.size());
      dout(10) << __func__ << " existing_cfs=" << existing_cfs.size() << dendl;
      for (size_t i = 0; i < existing_cfs_shard.size(); i++) {
	add_column_family(existing_cfs_shard[i].second.name,
			  existing_cfs_shard[i].second.hash_l,
			  existing_cfs_shard[i].second.hash_h,
			  existing_cfs_shard[i].first,
			  handles[i]);
      }
      default_cf = handles[handles.size() - 1];
      must_close_default_cf = true;

      if (missing_cfs.size() > 0) {
	dout(10) << __func__ << " missing_cfs=" << missing_cfs.size() << dendl;
	ceph_assert(recreate_mode);
	ceph_assert(missing_cfs.size() == missing_cfs_shard.size());
	for (size_t i = 0; i < missing_cfs.size(); i++) {
	  rocksdb::ColumnFamilyHandle *cf;
	  status = db->CreateColumnFamily(missing_cfs[i].options, missing_cfs[i].name, &cf);
	  if (!status.ok()) {
	    derr << __func__ << " Failed to create rocksdb column family: "
		 << missing_cfs[i].name << dendl;
	    return -EINVAL;
	  }
	  add_column_family(missing_cfs_shard[i].second.name,
			    missing_cfs_shard[i].second.hash_l,
			    missing_cfs_shard[i].second.hash_h,
			    missing_cfs_shard[i].first,
			    cf);
	}
	opt.env->DeleteFile(sharding_recreate);
      }
    }
  }
  ceph_assert(default_cf != nullptr);
  
  PerfCountersBuilder plb(cct, "rocksdb", l_rocksdb_first, l_rocksdb_last);
  plb.add_u64_counter(l_rocksdb_gets, "get", "Gets");
  plb.add_u64_counter(l_rocksdb_txns, "submit_transaction", "Submit transactions");
  plb.add_u64_counter(l_rocksdb_txns_sync, "submit_transaction_sync", "Submit transactions sync");
  plb.add_time_avg(l_rocksdb_get_latency, "get_latency", "Get latency");
  plb.add_time_avg(l_rocksdb_submit_latency, "submit_latency", "Submit Latency");
  plb.add_time_avg(l_rocksdb_submit_sync_latency, "submit_sync_latency", "Submit Sync Latency");
  plb.add_u64_counter(l_rocksdb_compact, "compact", "Compactions");
  plb.add_u64_counter(l_rocksdb_compact_range, "compact_range", "Compactions by range");
  plb.add_u64_counter(l_rocksdb_compact_queue_merge, "compact_queue_merge", "Mergings of ranges in compaction queue");
  plb.add_u64(l_rocksdb_compact_queue_len, "compact_queue_len", "Length of compaction queue");
  plb.add_time_avg(l_rocksdb_write_wal_time, "rocksdb_write_wal_time", "Rocksdb write wal time");
  plb.add_time_avg(l_rocksdb_write_memtable_time, "rocksdb_write_memtable_time", "Rocksdb write memtable time");
  plb.add_time_avg(l_rocksdb_write_delay_time, "rocksdb_write_delay_time", "Rocksdb write delay time");
  plb.add_time_avg(l_rocksdb_write_pre_and_post_process_time, 
      "rocksdb_write_pre_and_post_time", "total time spent on writing a record, excluding write process");
  logger = plb.create_perf_counters();
  cct->get_perfcounters_collection()->add(logger);

  if (compact_on_mount) {
    derr << "Compacting rocksdb store..." << dendl;
    compact();
    derr << "Finished compacting rocksdb store" << dendl;
  }
  return 0;
}

int RocksDBStore::_test_init(const string& dir)
{
  rocksdb::Options options;
  options.create_if_missing = true;
  rocksdb::DB *db;
  rocksdb::Status status = rocksdb::DB::Open(options, dir, &db);
  delete db;
  db = nullptr;
  return status.ok() ? 0 : -EIO;
}

RocksDBStore::~RocksDBStore()
{
  close();

  if (priv) {
    delete static_cast<rocksdb::Env*>(priv);
  }
}

void RocksDBStore::close()
{
  // stop compaction thread
  compact_queue_lock.lock();
  if (compact_thread.is_started()) {
    dout(1) << __func__ << " waiting for compaction thread to stop" << dendl;
    compact_queue_stop = true;
    compact_queue_cond.notify_all();
    compact_queue_lock.unlock();
    compact_thread.join();
    dout(1) << __func__ << " compaction thread to stopped" << dendl;
  } else {
    compact_queue_lock.unlock();
  }

  if (logger) {
    cct->get_perfcounters_collection()->remove(logger);
    delete logger;
    logger = nullptr;
  }

  // Ensure db is destroyed before dependent db_cache and filterpolicy
  for (auto& p : cf_handles) {
    for (size_t i = 0; i < p.second.handles.size(); i++) {
      db->DestroyColumnFamilyHandle(p.second.handles[i]);
    }
  }
  cf_handles.clear();
  if (must_close_default_cf) {
    db->DestroyColumnFamilyHandle(default_cf);
    must_close_default_cf = false;
  }
  default_cf = nullptr;
  delete db;
  db = nullptr;
}

int RocksDBStore::repair(std::ostream &out)
{
  rocksdb::Status status;
  rocksdb::Options opt;
  int r = load_rocksdb_options(false, opt);
  if (r) {
    dout(1) << __func__ << " load rocksdb options failed" << dendl;
    out << "load rocksdb options failed" << std::endl;
    return r;
  }
  //need to save sharding definition, repairDB will delete files it does not know
  std::string stored_sharding_text;
  status = opt.env->FileExists(sharding_def_file);
  if (status.ok()) {
    status = rocksdb::ReadFileToString(opt.env,
				       sharding_def_file,
				       &stored_sharding_text);
    if (!status.ok()) {
      stored_sharding_text.clear();
    }
  }
  dout(10) << __func__ << " stored_sharding: " << stored_sharding_text << dendl;
  status = rocksdb::RepairDB(path, opt);
  bool repaired = status.ok();
  if (!stored_sharding_text.empty()) {
    //recreate markers even if repair failed
    opt.env->CreateDir(sharding_def_dir);
    status = rocksdb::WriteStringToFile(opt.env, stored_sharding_text,
					sharding_def_file, true);
    if (!status.ok()) {
      derr << __func__ << " cannot write to " << sharding_def_file << dendl;
      return -1;
    }
    status = rocksdb::WriteStringToFile(opt.env, "1",
					  sharding_recreate, true);
    if (!status.ok()) {
      derr << __func__ << " cannot write to " << sharding_recreate << dendl;
      return -1;
    }
  }

  if (repaired && status.ok()) {
    return 0;
  } else {
    out << "repair rocksdb failed : " << status.ToString() << std::endl;
    return -1;
  }
}

void RocksDBStore::split_stats(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
}

bool RocksDBStore::get_property(
  const std::string &property,
  uint64_t *out)
{
  return db->GetIntProperty(property, out);
}

int64_t RocksDBStore::estimate_prefix_size(const string& prefix,
					   const string& key_prefix)
{
  uint64_t size = 0;
  uint8_t flags =
    //rocksdb::DB::INCLUDE_MEMTABLES |  // do not include memtables...
    rocksdb::DB::INCLUDE_FILES;
  auto p_iter = cf_handles.find(prefix);
  if (p_iter != cf_handles.end()) {
    for (auto cf : p_iter->second.handles) {
      uint64_t s = 0;
      string start = key_prefix + string(1, '\x00');
      string limit = key_prefix + string("\xff\xff\xff\xff");
      rocksdb::Range r(start, limit);
      db->GetApproximateSizes(cf, &r, 1, &s, flags);
      size += s;
    }
  } else {
    string start = combine_strings(prefix , key_prefix);
    string limit = combine_strings(prefix , key_prefix + "\xff\xff\xff\xff");
    rocksdb::Range r(start, limit);
    db->GetApproximateSizes(default_cf, &r, 1, &size, flags);
  }
  return size;
}

void RocksDBStore::get_statistics(Formatter *f)
{
  if (!cct->_conf->rocksdb_perf)  {
    dout(20) << __func__ << " RocksDB perf is disabled, can't probe for stats"
	     << dendl;
    return;
  }

  if (cct->_conf->rocksdb_collect_compaction_stats) {
    std::string stat_str;
    bool status = db->GetProperty("rocksdb.stats", &stat_str);
    if (status) {
      f->open_object_section("rocksdb_statistics");
      f->dump_string("rocksdb_compaction_statistics", "");
      vector<string> stats;
      split_stats(stat_str, '\n', stats);
      for (auto st :stats) {
        f->dump_string("", st);
      }
      f->close_section();
    }
  }
  if (cct->_conf->rocksdb_collect_extended_stats) {
    if (dbstats) {
      f->open_object_section("rocksdb_extended_statistics");
      string stat_str = dbstats->ToString();
      vector<string> stats;
      split_stats(stat_str, '\n', stats);
      f->dump_string("rocksdb_extended_statistics", "");
      for (auto st :stats) {
        f->dump_string(".", st);
      }
      f->close_section();
    }
    f->open_object_section("rocksdbstore_perf_counters");
    logger->dump_formatted(f,0);
    f->close_section();
  }
  if (cct->_conf->rocksdb_collect_memory_stats) {
    f->open_object_section("rocksdb_memtable_statistics");
    std::string str;
    if (!bbt_opts.no_block_cache) {
      str.append(stringify(bbt_opts.block_cache->GetUsage()));
      f->dump_string("block_cache_usage", str.data());
      str.clear();
      str.append(stringify(bbt_opts.block_cache->GetPinnedUsage()));
      f->dump_string("block_cache_pinned_blocks_usage", str);
      str.clear();
    }
    db->GetProperty("rocksdb.cur-size-all-mem-tables", &str);
    f->dump_string("rocksdb_memtable_usage", str);
    str.clear();
    db->GetProperty("rocksdb.estimate-table-readers-mem", &str);
    f->dump_string("rocksdb_index_filter_blocks_usage", str);
    f->close_section();
  }
}

int RocksDBStore::submit_common(rocksdb::WriteOptions& woptions, KeyValueDB::Transaction t) 
{
  // enable rocksdb breakdown
  // considering performance overhead, default is disabled
  if (cct->_conf->rocksdb_perf) {
    rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTimeExceptForMutex);
    rocksdb::get_perf_context()->Reset();
  }

  RocksDBTransactionImpl * _t =
    static_cast<RocksDBTransactionImpl *>(t.get());
  woptions.disableWAL = disableWAL;
  lgeneric_subdout(cct, rocksdb, 30) << __func__;
  RocksWBHandler bat_txc;
  _t->bat.Iterate(&bat_txc);
  *_dout << " Rocksdb transaction: " << bat_txc.seen << dendl;
  
  rocksdb::Status s = db->Write(woptions, &_t->bat);
  if (!s.ok()) {
    RocksWBHandler rocks_txc;
    _t->bat.Iterate(&rocks_txc);
    derr << __func__ << " error: " << s.ToString() << " code = " << s.code()
         << " Rocksdb transaction: " << rocks_txc.seen << dendl;
  }

  if (cct->_conf->rocksdb_perf) {
    utime_t write_memtable_time;
    utime_t write_delay_time;
    utime_t write_wal_time;
    utime_t write_pre_and_post_process_time;
    write_wal_time.set_from_double(
	static_cast<double>(rocksdb::get_perf_context()->write_wal_time)/1000000000);
    write_memtable_time.set_from_double(
	static_cast<double>(rocksdb::get_perf_context()->write_memtable_time)/1000000000);
    write_delay_time.set_from_double(
	static_cast<double>(rocksdb::get_perf_context()->write_delay_time)/1000000000);
    write_pre_and_post_process_time.set_from_double(
	static_cast<double>(rocksdb::get_perf_context()->write_pre_and_post_process_time)/1000000000);
    logger->tinc(l_rocksdb_write_memtable_time, write_memtable_time);
    logger->tinc(l_rocksdb_write_delay_time, write_delay_time);
    logger->tinc(l_rocksdb_write_wal_time, write_wal_time);
    logger->tinc(l_rocksdb_write_pre_and_post_process_time, write_pre_and_post_process_time);
  }

  return s.ok() ? 0 : -1;
}

int RocksDBStore::submit_transaction(KeyValueDB::Transaction t) 
{
  utime_t start = ceph_clock_now();
  rocksdb::WriteOptions woptions;
  woptions.sync = false;

  int result = submit_common(woptions, t);

  utime_t lat = ceph_clock_now() - start;
  logger->inc(l_rocksdb_txns);
  logger->tinc(l_rocksdb_submit_latency, lat);
  
  return result;
}

int RocksDBStore::submit_transaction_sync(KeyValueDB::Transaction t)
{
  utime_t start = ceph_clock_now();
  rocksdb::WriteOptions woptions;
  // if disableWAL, sync can't set
  woptions.sync = !disableWAL;
  
  int result = submit_common(woptions, t);
  
  utime_t lat = ceph_clock_now() - start;
  logger->inc(l_rocksdb_txns_sync);
  logger->tinc(l_rocksdb_submit_sync_latency, lat);

  return result;
}

RocksDBStore::RocksDBTransactionImpl::RocksDBTransactionImpl(RocksDBStore *_db)
{
  db = _db;
}

void RocksDBStore::RocksDBTransactionImpl::put_bat(
  rocksdb::WriteBatch& bat,
  rocksdb::ColumnFamilyHandle *cf,
  const string &key,
  const bufferlist &to_set_bl)
{
  // bufferlist::c_str() is non-constant, so we can't call c_str()
  if (to_set_bl.is_contiguous() && to_set_bl.length() > 0) {
    bat.Put(cf,
	    rocksdb::Slice(key),
	    rocksdb::Slice(to_set_bl.buffers().front().c_str(),
			   to_set_bl.length()));
  } else {
    rocksdb::Slice key_slice(key);
    vector<rocksdb::Slice> value_slices(to_set_bl.get_num_buffers());
    bat.Put(cf,
	    rocksdb::SliceParts(&key_slice, 1),
            prepare_sliceparts(to_set_bl, &value_slices));
  }
}

void RocksDBStore::RocksDBTransactionImpl::set(
  const string &prefix,
  const string &k,
  const bufferlist &to_set_bl)
{
  auto cf = db->get_cf_handle(prefix, k);
  if (cf) {
    put_bat(bat, cf, k, to_set_bl);
  } else {
    string key = combine_strings(prefix, k);
    put_bat(bat, db->default_cf, key, to_set_bl);
  }
}

void RocksDBStore::RocksDBTransactionImpl::set(
  const string &prefix,
  const char *k, size_t keylen,
  const bufferlist &to_set_bl)
{
  auto cf = db->get_cf_handle(prefix, k, keylen);
  if (cf) {
    string key(k, keylen);  // fixme?
    put_bat(bat, cf, key, to_set_bl);
  } else {
    string key;
    combine_strings(prefix, k, keylen, &key);
    put_bat(bat, cf, key, to_set_bl);
  }
}

void RocksDBStore::RocksDBTransactionImpl::rmkey(const string &prefix,
					         const string &k)
{
  auto cf = db->get_cf_handle(prefix, k);
  if (cf) {
    bat.Delete(cf, rocksdb::Slice(k));
  } else {
    bat.Delete(db->default_cf, combine_strings(prefix, k));
  }
}

void RocksDBStore::RocksDBTransactionImpl::rmkey(const string &prefix,
					         const char *k,
						 size_t keylen)
{
  auto cf = db->get_cf_handle(prefix, k, keylen);
  if (cf) {
    bat.Delete(cf, rocksdb::Slice(k, keylen));
  } else {
    string key;
    combine_strings(prefix, k, keylen, &key);
    bat.Delete(db->default_cf, rocksdb::Slice(key));
  }
}

void RocksDBStore::RocksDBTransactionImpl::rm_single_key(const string &prefix,
					                 const string &k)
{
  auto cf = db->get_cf_handle(prefix, k);
  if (cf) {
    bat.SingleDelete(cf, k);
  } else {
    bat.SingleDelete(db->default_cf, combine_strings(prefix, k));
  }
}

void RocksDBStore::RocksDBTransactionImpl::rmkeys_by_prefix(const string &prefix)
{
  auto p_iter = db->cf_handles.find(prefix);
  if (p_iter == db->cf_handles.end()) {
    uint64_t cnt = db->delete_range_threshold;
    bat.SetSavePoint();
    auto it = db->get_iterator(prefix);
    for (it->seek_to_first(); it->valid() && (--cnt) != 0; it->next()) {
      bat.Delete(db->default_cf, combine_strings(prefix, it->key()));
    }
    if (cnt == 0) {
	bat.RollbackToSavePoint();
	string endprefix = prefix;
        endprefix.push_back('\x01');
	bat.DeleteRange(db->default_cf,
                        combine_strings(prefix, string()),
                        combine_strings(endprefix, string()));
    } else {
      bat.PopSavePoint();
    }
  } else {
    ceph_assert(p_iter->second.handles.size() >= 1);
    for (auto cf : p_iter->second.handles) {
      uint64_t cnt = db->delete_range_threshold;
      bat.SetSavePoint();
      auto it = db->new_shard_iterator(cf);
      for (it->SeekToFirst(); it->Valid() && (--cnt) != 0; it->Next()) {
	bat.Delete(cf, it->key());
      }
      if (cnt == 0) {
	bat.RollbackToSavePoint();
	string endprefix = "\xff\xff\xff\xff";  // FIXME: this is cheating...
	bat.DeleteRange(cf, string(), endprefix);
      } else {
	bat.PopSavePoint();
      }
    }
  }
}

void RocksDBStore::RocksDBTransactionImpl::rm_range_keys(const string &prefix,
                                                         const string &start,
                                                         const string &end)
{
  auto p_iter = db->cf_handles.find(prefix);
  if (p_iter == db->cf_handles.end()) {
    uint64_t cnt = db->delete_range_threshold;
    bat.SetSavePoint();
    auto it = db->get_iterator(prefix);
    for (it->lower_bound(start);
	 it->valid() && db->comparator->Compare(it->key(), end) < 0 && (--cnt) != 0;
	 it->next()) {
      bat.Delete(db->default_cf, combine_strings(prefix, it->key()));
    }
    if (cnt == 0) {
      bat.RollbackToSavePoint();
      bat.DeleteRange(db->default_cf,
		      rocksdb::Slice(combine_strings(prefix, start)),
		      rocksdb::Slice(combine_strings(prefix, end)));
    } else {
      bat.PopSavePoint();
    }
  } else {
    ceph_assert(p_iter->second.handles.size() >= 1);
    for (auto cf : p_iter->second.handles) {
      uint64_t cnt = db->delete_range_threshold;
      bat.SetSavePoint();
      rocksdb::Iterator* it = db->new_shard_iterator(cf);
      ceph_assert(it != nullptr);
      for (it->Seek(start);
	   it->Valid() && db->comparator->Compare(it->key(), end) < 0 && (--cnt) != 0;
	   it->Next()) {
	bat.Delete(cf, it->key());
      }
      if (cnt == 0) {
	bat.RollbackToSavePoint();
	bat.DeleteRange(cf, rocksdb::Slice(start), rocksdb::Slice(end));
      } else {
	bat.PopSavePoint();
      }
      delete it;
    }
  }
}

void RocksDBStore::RocksDBTransactionImpl::merge(
  const string &prefix,
  const string &k,
  const bufferlist &to_set_bl)
{
  auto cf = db->get_cf_handle(prefix, k);
  if (cf) {
    // bufferlist::c_str() is non-constant, so we can't call c_str()
    if (to_set_bl.is_contiguous() && to_set_bl.length() > 0) {
      bat.Merge(
	cf,
	rocksdb::Slice(k),
	rocksdb::Slice(to_set_bl.buffers().front().c_str(), to_set_bl.length()));
    } else {
      // make a copy
      rocksdb::Slice key_slice(k);
      vector<rocksdb::Slice> value_slices(to_set_bl.get_num_buffers());
      bat.Merge(cf, rocksdb::SliceParts(&key_slice, 1),
		prepare_sliceparts(to_set_bl, &value_slices));
    }
  } else {
    string key = combine_strings(prefix, k);
    // bufferlist::c_str() is non-constant, so we can't call c_str()
    if (to_set_bl.is_contiguous() && to_set_bl.length() > 0) {
      bat.Merge(
	db->default_cf,
	rocksdb::Slice(key),
	rocksdb::Slice(to_set_bl.buffers().front().c_str(), to_set_bl.length()));
    } else {
      // make a copy
      rocksdb::Slice key_slice(key);
      vector<rocksdb::Slice> value_slices(to_set_bl.get_num_buffers());
      bat.Merge(
	db->default_cf,
	rocksdb::SliceParts(&key_slice, 1),
	prepare_sliceparts(to_set_bl, &value_slices));
    }
  }
}

int RocksDBStore::get(
    const string &prefix,
    const std::set<string> &keys,
    std::map<string, bufferlist> *out)
{
  rocksdb::PinnableSlice value;
  utime_t start = ceph_clock_now();
  if (cf_handles.count(prefix) > 0) {
    for (auto& key : keys) {
      auto cf_handle = get_cf_handle(prefix, key);
      auto status = db->Get(rocksdb::ReadOptions(),
			    cf_handle,
			    rocksdb::Slice(key),
			    &value);
      if (status.ok()) {
	(*out)[key].append(value.data(), value.size());
      } else if (status.IsIOError()) {
	ceph_abort_msg(status.getState());
      }
      value.Reset();
    }
  } else {
    for (auto& key : keys) {
      string k = combine_strings(prefix, key);
      auto status = db->Get(rocksdb::ReadOptions(),
			    default_cf,
			    rocksdb::Slice(k),
			    &value);
      if (status.ok()) {
	(*out)[key].append(value.data(), value.size());
      } else if (status.IsIOError()) {
	ceph_abort_msg(status.getState());
      }
      value.Reset();
    }
  }
  utime_t lat = ceph_clock_now() - start;
  logger->inc(l_rocksdb_gets);
  logger->tinc(l_rocksdb_get_latency, lat);
  return 0;
}

int RocksDBStore::get(
    const string &prefix,
    const string &key,
    bufferlist *out)
{
  ceph_assert(out && (out->length() == 0));
  utime_t start = ceph_clock_now();
  int r = 0;
  rocksdb::PinnableSlice value;
  rocksdb::Status s;
  auto cf = get_cf_handle(prefix, key);
  if (cf) {
    s = db->Get(rocksdb::ReadOptions(),
		cf,
		rocksdb::Slice(key),
		&value);
  } else {
    string k = combine_strings(prefix, key);
    s = db->Get(rocksdb::ReadOptions(),
		default_cf,
		rocksdb::Slice(k),
		&value);
  }
  if (s.ok()) {
    out->append(value.data(), value.size());
  } else if (s.IsNotFound()) {
    r = -ENOENT;
  } else {
    ceph_abort_msg(s.getState());
  }
  utime_t lat = ceph_clock_now() - start;
  logger->inc(l_rocksdb_gets);
  logger->tinc(l_rocksdb_get_latency, lat);
  return r;
}

int RocksDBStore::get(
  const string& prefix,
  const char *key,
  size_t keylen,
  bufferlist *out)
{
  ceph_assert(out && (out->length() == 0));
  utime_t start = ceph_clock_now();
  int r = 0;
  rocksdb::PinnableSlice value;
  rocksdb::Status s;
  auto cf = get_cf_handle(prefix, key, keylen);
  if (cf) {
    s = db->Get(rocksdb::ReadOptions(),
		cf,
		rocksdb::Slice(key, keylen),
		&value);
  } else {
    string k;
    combine_strings(prefix, key, keylen, &k);
    s = db->Get(rocksdb::ReadOptions(),
		default_cf,
		rocksdb::Slice(k),
		&value);
  }
  if (s.ok()) {
    out->append(value.data(), value.size());
  } else if (s.IsNotFound()) {
    r = -ENOENT;
  } else {
    ceph_abort_msg(s.getState());
  }
  utime_t lat = ceph_clock_now() - start;
  logger->inc(l_rocksdb_gets);
  logger->tinc(l_rocksdb_get_latency, lat);
  return r;
}

int RocksDBStore::split_key(rocksdb::Slice in, string *prefix, string *key)
{
  size_t prefix_len = 0;

  // Find separator inside Slice
  char* separator = (char*) memchr(in.data(), 0, in.size());
  if (separator == NULL)
     return -EINVAL;
  prefix_len = size_t(separator - in.data());
  if (prefix_len >= in.size())
    return -EINVAL;

  // Fetch prefix and/or key directly from Slice
  if (prefix)
    *prefix = string(in.data(), prefix_len);
  if (key)
    *key = string(separator+1, in.size()-prefix_len-1);
  return 0;
}

void RocksDBStore::compact()
{
  logger->inc(l_rocksdb_compact);
  rocksdb::CompactRangeOptions options;
  db->CompactRange(options, default_cf, nullptr, nullptr);
  for (auto cf : cf_handles) {
    for (auto shard_cf : cf.second.handles) {
      db->CompactRange(
	options,
	shard_cf,
	nullptr, nullptr);
    }
  }
}

void RocksDBStore::compact_thread_entry()
{
  std::unique_lock l{compact_queue_lock};
  dout(10) << __func__ << " enter" << dendl;
  while (!compact_queue_stop) {
    if (!compact_queue.empty()) {
      auto range = compact_queue.front();
      compact_queue.pop_front();
      logger->set(l_rocksdb_compact_queue_len, compact_queue.size());
      l.unlock();
      logger->inc(l_rocksdb_compact_range);
      if (range.first.empty() && range.second.empty()) {
        compact();
      } else {
        compact_range(range.first, range.second);
      }
      l.lock();
      continue;
    }
    dout(10) << __func__ << " waiting" << dendl;
    compact_queue_cond.wait(l);
  }
  dout(10) << __func__ << " exit" << dendl;
}

void RocksDBStore::compact_range_async(const string& start, const string& end)
{
  std::lock_guard l(compact_queue_lock);

  // try to merge adjacent ranges.  this is O(n), but the queue should
  // be short.  note that we do not cover all overlap cases and merge
  // opportunities here, but we capture the ones we currently need.
  list< pair<string,string> >::iterator p = compact_queue.begin();
  while (p != compact_queue.end()) {
    if (p->first == start && p->second == end) {
      // dup; no-op
      return;
    }
    if (start <= p->first && p->first <= end) {
      // new region crosses start of existing range
      // select right bound that is bigger
      compact_queue.push_back(make_pair(start, end > p->second ? end : p->second));
      compact_queue.erase(p);
      logger->inc(l_rocksdb_compact_queue_merge);
      break;
    }
    if (start <= p->second && p->second <= end) {
      // new region crosses end of existing range
      //p->first < p->second and p->second <= end, so p->first <= end.
      //But we break if previous condition, so start > p->first.
      compact_queue.push_back(make_pair(p->first, end));
      compact_queue.erase(p);
      logger->inc(l_rocksdb_compact_queue_merge);
      break;
    }
    ++p;
  }
  if (p == compact_queue.end()) {
    // no merge, new entry.
    compact_queue.push_back(make_pair(start, end));
    logger->set(l_rocksdb_compact_queue_len, compact_queue.size());
  }
  compact_queue_cond.notify_all();
  if (!compact_thread.is_started()) {
    compact_thread.create("rstore_compact");
  }
}
bool RocksDBStore::check_omap_dir(string &omap_dir)
{
  rocksdb::Options options;
  options.create_if_missing = true;
  rocksdb::DB *db;
  rocksdb::Status status = rocksdb::DB::Open(options, omap_dir, &db);
  delete db;
  db = nullptr;
  return status.ok();
}
void RocksDBStore::compact_range(const string& start, const string& end)
{
  rocksdb::CompactRangeOptions options;
  rocksdb::Slice cstart(start);
  rocksdb::Slice cend(end);
  db->CompactRange(options, &cstart, &cend);
}

RocksDBStore::RocksDBWholeSpaceIteratorImpl::~RocksDBWholeSpaceIteratorImpl()
{
  delete dbiter;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::seek_to_first()
{
  dbiter->SeekToFirst();
  ceph_assert(!dbiter->status().IsIOError());
  return dbiter->status().ok() ? 0 : -1;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::seek_to_first(const string &prefix)
{
  rocksdb::Slice slice_prefix(prefix);
  dbiter->Seek(slice_prefix);
  ceph_assert(!dbiter->status().IsIOError());
  return dbiter->status().ok() ? 0 : -1;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::seek_to_last()
{
  dbiter->SeekToLast();
  ceph_assert(!dbiter->status().IsIOError());
  return dbiter->status().ok() ? 0 : -1;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::seek_to_last(const string &prefix)
{
  string limit = past_prefix(prefix);
  rocksdb::Slice slice_limit(limit);
  dbiter->Seek(slice_limit);

  if (!dbiter->Valid()) {
    dbiter->SeekToLast();
  } else {
    dbiter->Prev();
  }
  return dbiter->status().ok() ? 0 : -1;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::upper_bound(const string &prefix, const string &after)
{
  lower_bound(prefix, after);
  if (valid()) {
  pair<string,string> key = raw_key();
    if (key.first == prefix && key.second == after)
      next();
  }
  return dbiter->status().ok() ? 0 : -1;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::lower_bound(const string &prefix, const string &to)
{
  string bound = combine_strings(prefix, to);
  rocksdb::Slice slice_bound(bound);
  dbiter->Seek(slice_bound);
  return dbiter->status().ok() ? 0 : -1;
}
bool RocksDBStore::RocksDBWholeSpaceIteratorImpl::valid()
{
  return dbiter->Valid();
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::next()
{
  if (valid()) {
    dbiter->Next();
  }
  ceph_assert(!dbiter->status().IsIOError());
  return dbiter->status().ok() ? 0 : -1;
}
int RocksDBStore::RocksDBWholeSpaceIteratorImpl::prev()
{
  if (valid()) {
    dbiter->Prev();
  }
  ceph_assert(!dbiter->status().IsIOError());
  return dbiter->status().ok() ? 0 : -1;
}
string RocksDBStore::RocksDBWholeSpaceIteratorImpl::key()
{
  string out_key;
  split_key(dbiter->key(), 0, &out_key);
  return out_key;
}
pair<string,string> RocksDBStore::RocksDBWholeSpaceIteratorImpl::raw_key()
{
  string prefix, key;
  split_key(dbiter->key(), &prefix, &key);
  return make_pair(prefix, key);
}

bool RocksDBStore::RocksDBWholeSpaceIteratorImpl::raw_key_is_prefixed(const string &prefix) {
  // Look for "prefix\0" right in rocksb::Slice
  rocksdb::Slice key = dbiter->key();
  if ((key.size() > prefix.length()) && (key[prefix.length()] == '\0')) {
    return memcmp(key.data(), prefix.c_str(), prefix.length()) == 0;
  } else {
    return false;
  }
}

bufferlist RocksDBStore::RocksDBWholeSpaceIteratorImpl::value()
{
  return to_bufferlist(dbiter->value());
}

size_t RocksDBStore::RocksDBWholeSpaceIteratorImpl::key_size()
{
  return dbiter->key().size();
}

size_t RocksDBStore::RocksDBWholeSpaceIteratorImpl::value_size()
{
  return dbiter->value().size();
}

bufferptr RocksDBStore::RocksDBWholeSpaceIteratorImpl::value_as_ptr()
{
  rocksdb::Slice val = dbiter->value();
  return bufferptr(val.data(), val.size());
}

int RocksDBStore::RocksDBWholeSpaceIteratorImpl::status()
{
  return dbiter->status().ok() ? 0 : -1;
}

string RocksDBStore::past_prefix(const string &prefix)
{
  string limit = prefix;
  limit.push_back(1);
  return limit;
}

RocksDBStore::WholeSpaceIterator RocksDBStore::get_wholespace_iterator()
{
  return std::make_shared<RocksDBWholeSpaceIteratorImpl>(
    db->NewIterator(rocksdb::ReadOptions(), default_cf));
}

class CFIteratorImpl : public KeyValueDB::IteratorImpl {
protected:
  string prefix;
  rocksdb::Iterator *dbiter;
public:
  explicit CFIteratorImpl(const std::string& p,
				 rocksdb::Iterator *iter)
    : prefix(p), dbiter(iter) { }
  ~CFIteratorImpl() {
    delete dbiter;
  }

  int seek_to_first() override {
    dbiter->SeekToFirst();
    return dbiter->status().ok() ? 0 : -1;
  }
  int seek_to_last() override {
    dbiter->SeekToLast();
    return dbiter->status().ok() ? 0 : -1;
  }
  int upper_bound(const string &after) override {
    lower_bound(after);
    if (valid() && (key() == after)) {
      next();
    }
    return dbiter->status().ok() ? 0 : -1;
  }
  int lower_bound(const string &to) override {
    rocksdb::Slice slice_bound(to);
    dbiter->Seek(slice_bound);
    return dbiter->status().ok() ? 0 : -1;
  }
  int next() override {
    if (valid()) {
      dbiter->Next();
    }
    return dbiter->status().ok() ? 0 : -1;
  }
  int prev() override {
    if (valid()) {
      dbiter->Prev();
    }
    return dbiter->status().ok() ? 0 : -1;
  }
  bool valid() override {
    return dbiter->Valid();
  }
  string key() override {
    return dbiter->key().ToString();
  }
  std::pair<std::string, std::string> raw_key() override {
    return make_pair(prefix, key());
  }
  bufferlist value() override {
    return to_bufferlist(dbiter->value());
  }
  bufferptr value_as_ptr() override {
    rocksdb::Slice val = dbiter->value();
    return bufferptr(val.data(), val.size());
  }
  int status() override {
    return dbiter->status().ok() ? 0 : -1;
  }
};

class ShardMergeIteratorImpl : public KeyValueDB::IteratorImpl {
private:
  struct KeyLess {
  private:
    const rocksdb::Comparator* comparator;
  public:
    KeyLess(const rocksdb::Comparator* comparator) : comparator(comparator) { };

    bool operator()(rocksdb::Iterator* a, rocksdb::Iterator* b) const
    {
      if (a->Valid()) {
	if (b->Valid()) {
	  return comparator->Compare(a->key(), b->key()) < 0;
	} else {
	  return true;
	}
      } else {
	if (b->Valid()) {
	  return false;
	} else {
	  return false;
	}
      }
    }
  };

  const RocksDBStore* db;
  KeyLess keyless;
  string prefix;
  std::vector<rocksdb::Iterator*> iters;
public:
  explicit ShardMergeIteratorImpl(const RocksDBStore* db,
				  const std::string& prefix,
				  const std::vector<rocksdb::ColumnFamilyHandle*>& shards)
    : db(db), keyless(db->comparator), prefix(prefix)
  {
    iters.reserve(shards.size());
    for (auto& s : shards) {
      iters.push_back(db->db->NewIterator(rocksdb::ReadOptions(), s));
    }
  }
  ~ShardMergeIteratorImpl() {
    for (auto& it : iters) {
      delete it;
    }
  }
  int seek_to_first() override {
    for (auto& it : iters) {
      it->SeekToFirst();
      if (!it->status().ok()) {
	return -1;
      }
    }
    //all iterators seeked, sort
    std::sort(iters.begin(), iters.end(), keyless);
    return 0;
  }
  int seek_to_last() override {
    for (auto& it : iters) {
      it->SeekToLast();
      if (!it->status().ok()) {
	return -1;
      }
    }
    for (size_t i = 1; i < iters.size(); i++) {
      if (iters[0]->Valid()) {
	if (iters[i]->Valid()) {
	  if (keyless(iters[0], iters[i])) {
	    swap(iters[0], iters[i]);
	  }
	} else {
	  //iters[i] empty
	}
      } else {
	if (iters[i]->Valid()) {
	  swap(iters[0], iters[i]);
	}
      }
      //it might happen that cf was empty
      if (iters[i]->Valid()) {
	iters[i]->Next();
      }
    }
    //no need to sort, as at most 1 iterator is valid now
    return 0;
  }
  int upper_bound(const string &after) override {
    rocksdb::Slice slice_bound(after);
    for (auto& it : iters) {
      it->Seek(slice_bound);
      if (it->Valid() && it->key() == after) {
	it->Next();
      }
      if (!it->status().ok()) {
	return -1;
      }
    }
    std::sort(iters.begin(), iters.end(), keyless);
    return 0;
  }
  int lower_bound(const string &to) override {
    rocksdb::Slice slice_bound(to);
    for (auto& it : iters) {
      it->Seek(slice_bound);
      if (!it->status().ok()) {
	return -1;
      }
    }
    std::sort(iters.begin(), iters.end(), keyless);
    return 0;
  }
  int next() override {
    int r = -1;
    if (iters[0]->Valid()) {
      iters[0]->Next();
      if (iters[0]->status().ok()) {
	r = 0;
	//bubble up
	for (size_t i = 0; i < iters.size() - 1; i++) {
	  if (keyless(iters[i], iters[i + 1])) {
	    //matches, fixed
	    break;
	  }
	  std::swap(iters[i], iters[i + 1]);
	}
      }
    }
    return r;
  }
  // iters are sorted, so
  // a[0] < b[0] < c[0] < d[0]
  // a[0] > a[-1], a[0] > b[-1], a[0] > c[-1], a[0] > d[-1]
  // so, prev() will be one of:
  // a[-1], b[-1], c[-1], d[-1]
  // prev() will be the one that is *largest* of them
  //
  // alg:
  // 1. go prev() on each iterator we can
  // 2. select largest key from those iterators
  // 3. go next() on all iterators except (2)
  // 4. sort
  int prev() override {
    std::vector<rocksdb::Iterator*> prev_done;
    //1
    for (auto it: iters) {
      if (it->Valid()) {
	it->Prev();
	if (it->Valid()) {
	  prev_done.push_back(it);
	} else {
	  it->SeekToFirst();
	}
      } else {
	it->SeekToLast();
	if (it->Valid()) {
	  prev_done.push_back(it);
	}
      }
    }
    if (prev_done.size() == 0) {
      /* there is no previous element */
      if (iters[0]->Valid()) {
	iters[0]->Prev();
	ceph_assert(!iters[0]->Valid());
      }
      return 0;
    }
    //2,3
    rocksdb::Iterator* highest = prev_done[0];
    for (size_t i = 1; i < prev_done.size(); i++) {
      if (keyless(highest, prev_done[i])) {
	highest->Next();
	highest = prev_done[i];
      } else {
	prev_done[i]->Next();
      }
    }
    //4
    //insert highest in the beginning, and shift values until we pick highest
    //untouched rest is sorted - we just prev()/next() them
    rocksdb::Iterator* hold = highest;
    for (size_t i = 0; i < iters.size(); i++) {
      std::swap(hold, iters[i]);
      if (hold == highest) break;
    }
    ceph_assert(hold == highest);
    return 0;
  }
  bool valid() override {
    return iters[0]->Valid();
  }
  string key() override {
    return iters[0]->key().ToString();
  }
  std::pair<std::string, std::string> raw_key() override {
    return make_pair(prefix, key());
  }
  bufferlist value() override {
    return to_bufferlist(iters[0]->value());
  }
  bufferptr value_as_ptr() override {
    rocksdb::Slice val = iters[0]->value();
    return bufferptr(val.data(), val.size());
  }
  int status() override {
    return iters[0]->status().ok() ? 0 : -1;
  }
};

KeyValueDB::Iterator RocksDBStore::get_iterator(const std::string& prefix)
{
  auto cf_it = cf_handles.find(prefix);
  if (cf_it != cf_handles.end()) {
    if (cf_it->second.handles.size() == 1) {
      return std::make_shared<CFIteratorImpl>(
        prefix,
        db->NewIterator(rocksdb::ReadOptions(), cf_it->second.handles[0]));
    } else {
      return std::make_shared<ShardMergeIteratorImpl>(
        this,
        prefix,
        cf_it->second.handles);
    }
  } else {
    return KeyValueDB::get_iterator(prefix);
  }
}

rocksdb::Iterator* RocksDBStore::new_shard_iterator(rocksdb::ColumnFamilyHandle* cf)
{
  return db->NewIterator(rocksdb::ReadOptions(), cf);
}
