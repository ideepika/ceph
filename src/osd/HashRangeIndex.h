// HashRangeIndexCalculation

#include <algorithm>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/geometry/geometries/segment.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <boost/function_output_iterator.hpp>

#include "common/hobject.h"
#include "osd_types.h"

#ifndef CEPH_HASHRANGEINDEX_H
#define CEPH_HASHRANGEINDEX_H

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

typedef bg::model::point<uint32_t, 2, bg::cs::cartesian> point_t;
typedef bg::model::segment<point_t> segment_t;
typedef std::pair<segment_t, pg_shard_t> value_t;


class HashRangeIndex {
 private:
    bgi::rtree<value_t, bgi::quadratic<16> > skip_range_idx;
    point_t make_point(hobject_t hobj) {
        return point_t(hobj.get_hash(), hobj.get_hash());
    }

 public:
    std::vector<value_t> query(hobject_t hobj) {
        std::vector<value_t> result;
        skip_range_idx.query(
            bgi::contains(make_point(hobj)),
            std::back_inserter(result)
        );
        
        return result;
    }

    size_t count(hobject_t hobj) {
        return skip_range_idx.query(
            bgi::contains(make_point(hobj)),
            boost::make_function_output_iterator([](const value_t& v){}) //noop
        );
    }

    void clear() {
        skip_range_idx.clear();
    }

    void ignore(pg_shard_t from) {
        skip_range_idx.insert(
            value_t(
                segment_t(point_t(0, 0), point_t(0,0)),
                from
            )
        );
    }

    void fill(std::deque<std::pair<uint32_t, uint32_t> >& ranges, pg_shard_t from) {
        for (auto& it : ranges) {
            uint32_t hmin = it.first;
            uint32_t hmax = it.second;
            skip_range_idx.insert(
                value_t(
                    segment_t(
                        point_t(hmin, hmin),
                        point_t(hmax, hmax)
                    ),
                    from
                )
            );
        }
    }

    void remove(value_t value) {
        skip_range_idx.remove(value);
    }

    void remove(hobject_t hobj, pg_shard_t from) {
        for (auto& range_shard_pair : query(hobj)) {
            if (range_shard_pair.second == from) {
                remove(range_shard_pair);
                break; //we can't have overlapping ranges of the same pg_shard that include the hobject
            }
        }
    }

    void remove_all(hobject_t const& hobj) {
        skip_range_idx.remove(query(hobj));
    }

    auto get() {
      return skip_range_idx;
    }
};

#endif //CEPH_HASHRANGEINDEX_H
