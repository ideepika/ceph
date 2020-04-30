// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_LIBRBD_IO_IMAGE_REQUEST_WQ_H
#define CEPH_LIBRBD_IO_IMAGE_REQUEST_WQ_H

#include "include/Context.h"
#include "common/ceph_mutex.h"
#include "common/WorkQueue.h"
#include "librbd/io/Types.h"
#include "include/interval_set.h"
#include <list>
#include <atomic>
#include <vector>

namespace librbd {

class ImageCtx;

namespace io {

class AioCompletion;
template <typename> class ImageDispatchSpec;
class ReadResult;

template <typename ImageCtxT = librbd::ImageCtx>
class ImageRequestWQ
  : public ThreadPool::PointerWQ<ImageDispatchSpec<ImageCtxT> > {
public:
  ImageRequestWQ(ImageCtxT *image_ctx, const string &name, time_t ti,
                 ThreadPool *tp);

  ssize_t read(uint64_t off, uint64_t len, ReadResult &&read_result,
               int op_flags);
  ssize_t write(uint64_t off, uint64_t len, bufferlist &&bl, int op_flags);
  ssize_t discard(uint64_t off, uint64_t len,
                  uint32_t discard_granularity_bytes);
  ssize_t writesame(uint64_t off, uint64_t len, bufferlist &&bl, int op_flags);
  ssize_t compare_and_write(uint64_t off, uint64_t len,
                            bufferlist &&cmp_bl, bufferlist &&bl,
                            uint64_t *mismatch_off, int op_flags);
  int flush();

  void aio_read(AioCompletion *c, uint64_t off, uint64_t len,
                ReadResult &&read_result, int op_flags, bool native_async=true);
  void aio_write(AioCompletion *c, uint64_t off, uint64_t len,
                 bufferlist &&bl, int op_flags, bool native_async=true);
  void aio_discard(AioCompletion *c, uint64_t off, uint64_t len,
                   uint32_t discard_granularity_bytes, bool native_async=true);
  void aio_flush(AioCompletion *c, bool native_async=true);
  void aio_writesame(AioCompletion *c, uint64_t off, uint64_t len,
                     bufferlist &&bl, int op_flags, bool native_async=true);
  void aio_compare_and_write(AioCompletion *c, uint64_t off,
                             uint64_t len, bufferlist &&cmp_bl,
                             bufferlist &&bl, uint64_t *mismatch_off,
                             int op_flags, bool native_async=true);

  using ThreadPool::PointerWQ<ImageDispatchSpec<ImageCtxT> >::drain;
  using ThreadPool::PointerWQ<ImageDispatchSpec<ImageCtxT> >::empty;

  void shut_down(Context *on_shutdown);

  inline bool writes_blocked() const {
    std::shared_lock locker{m_lock};
    return (m_write_blockers > 0);
  }

  void block_writes(Context *on_blocked);
  void unblock_writes();

  void wait_on_writes_unblocked(Context *on_unblocked);

  void set_require_lock(Direction direction, bool enabled);

protected:
  void *_void_dequeue() override;
  void process(ImageDispatchSpec<ImageCtxT> *req) override;

private:
  typedef std::list<Context *> Contexts;

  struct C_AcquireLock;
  struct C_BlockedWrites;
  struct C_RefreshFinish;

  ImageCtxT &m_image_ctx;
  mutable ceph::shared_mutex m_lock;
  Contexts m_write_blocker_contexts;
  uint32_t m_write_blockers = 0;
  Contexts m_unblocked_write_waiter_contexts;
  bool m_require_lock_on_read = false;
  bool m_require_lock_on_write = false;
  std::atomic<unsigned> m_queued_reads { 0 };
  std::atomic<unsigned> m_queued_writes { 0 };
  std::atomic<unsigned> m_in_flight_ios { 0 };
  std::atomic<unsigned> m_in_flight_writes { 0 };
  std::atomic<unsigned> m_io_blockers { 0 };

  typedef interval_set<uint64_t> ImageExtentIntervals;
  ImageExtentIntervals m_in_flight_extents;

  std::vector<ImageDispatchSpec<ImageCtxT>*> m_blocked_ios;
  std::atomic<unsigned> m_last_tid { 0 };
  std::set<uint64_t> m_queued_or_blocked_io_tids;
  std::map<uint64_t, ImageDispatchSpec<ImageCtxT>*> m_queued_flushes;

  bool m_shutdown = false;
  Context *m_on_shutdown = nullptr;

  bool is_lock_required(bool write_op) const;

  inline bool require_lock_on_read() const {
    std::shared_lock locker{m_lock};
    return m_require_lock_on_read;
  }
  inline bool writes_empty() const {
    std::shared_lock locker{m_lock};
    return (m_queued_writes == 0);
  }

  void finish_queued_io(bool write_op);
  void remove_in_flight_write_ios(uint64_t offset, uint64_t length,
                                  bool write_op, uint64_t tid);
  void finish_in_flight_write();

  void unblock_flushes();
  bool block_overlapping_io(ImageExtentIntervals* in_flight_image_extents,
                            uint64_t object_off, uint64_t object_len);
  void unblock_overlapping_io(uint64_t offset, uint64_t length, uint64_t tid);
  int start_in_flight_io(AioCompletion *c);
  void finish_in_flight_io();
  void fail_in_flight_io(int r, ImageDispatchSpec<ImageCtxT> *req);
  void process_io(ImageDispatchSpec<ImageCtxT> *req, bool non_blocking_io);

  void queue(ImageDispatchSpec<ImageCtxT> *req);
  void queue_unblocked_io(AioCompletion *comp,
                          ImageDispatchSpec<ImageCtxT> *req);

  void handle_acquire_lock(int r, ImageDispatchSpec<ImageCtxT> *req);
  void handle_refreshed(int r, ImageDispatchSpec<ImageCtxT> *req);
  void handle_blocked_writes(int r);
};

} // namespace io
} // namespace librbd

extern template class librbd::io::ImageRequestWQ<librbd::ImageCtx>;

#endif // CEPH_LIBRBD_IO_IMAGE_REQUEST_WQ_H
