/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* This benchmark exists to ensure that the benchmark integration is
 * working */

#include <string.h>
#include <sstream>

#include <grpc++/support/channel_arguments.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

extern "C" {
#include "src/core/ext/client_channel/client_channel.h"
#include "src/core/ext/load_reporting/load_reporting_filter.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/compress_filter.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/deadline_filter.h"
#include "src/core/lib/channel/http_client_filter.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/channel/message_size_filter.h"
#include "src/core/lib/transport/transport_impl.h"
}

#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

auto &force_library_initialization = Library::get();

class BaseChannelFixture {
 public:
  BaseChannelFixture(grpc_channel *channel) : channel_(channel) {}
  ~BaseChannelFixture() { grpc_channel_destroy(channel_); }

  grpc_channel *channel() const { return channel_; }

 private:
  grpc_channel *const channel_;
};

class InsecureChannel : public BaseChannelFixture {
 public:
  InsecureChannel()
      : BaseChannelFixture(
            grpc_insecure_channel_create("localhost:1234", NULL, NULL)) {}
};

class LameChannel : public BaseChannelFixture {
 public:
  LameChannel()
      : BaseChannelFixture(grpc_lame_client_channel_create(
            "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah")) {}
};

template <class Fixture>
static void BM_CallCreateDestroy(benchmark::State &state) {
  TrackCounters track_counters;
  Fixture fixture;
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void *method_hdl =
      grpc_channel_register_call(fixture.channel(), "/foo/bar", NULL, NULL);
  while (state.KeepRunning()) {
    grpc_call_destroy(grpc_channel_create_registered_call(
        fixture.channel(), NULL, GRPC_PROPAGATE_DEFAULTS, cq, method_hdl,
        deadline, NULL));
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}

BENCHMARK_TEMPLATE(BM_CallCreateDestroy, InsecureChannel);
BENCHMARK_TEMPLATE(BM_CallCreateDestroy, LameChannel);

static void FilterDestroy(grpc_exec_ctx *exec_ctx, void *arg,
                          grpc_error *error) {
  gpr_free(arg);
}

static void DoNothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

class FakeClientChannelFactory : public grpc_client_channel_factory {
 public:
  FakeClientChannelFactory() { vtable = &vtable_; }

 private:
  static void NoRef(grpc_client_channel_factory *factory) {}
  static void NoUnref(grpc_exec_ctx *exec_ctx,
                      grpc_client_channel_factory *factory) {}
  static grpc_subchannel *CreateSubchannel(grpc_exec_ctx *exec_ctx,
                                           grpc_client_channel_factory *factory,
                                           const grpc_subchannel_args *args) {
    return nullptr;
  }
  static grpc_channel *CreateClientChannel(grpc_exec_ctx *exec_ctx,
                                           grpc_client_channel_factory *factory,
                                           const char *target,
                                           grpc_client_channel_type type,
                                           const grpc_channel_args *args) {
    return nullptr;
  }

  static const grpc_client_channel_factory_vtable vtable_;
};

const grpc_client_channel_factory_vtable FakeClientChannelFactory::vtable_ = {
    NoRef, NoUnref, CreateSubchannel, CreateClientChannel};

static grpc_arg StringArg(const char *key, const char *value) {
  grpc_arg a;
  a.type = GRPC_ARG_STRING;
  a.key = const_cast<char *>(key);
  a.value.string = const_cast<char *>(value);
  return a;
}

enum FixtureFlags : uint32_t {
  CHECKS_NOT_LAST = 1,
  REQUIRES_TRANSPORT = 2,
};

template <const grpc_channel_filter *kFilter, uint32_t kFlags>
struct Fixture {
  const grpc_channel_filter *filter = kFilter;
  const uint32_t flags = kFlags;
};

namespace dummy_filter {

static void StartTransportStreamOp(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_transport_stream_op *op) {}

static void StartTransportOp(grpc_exec_ctx *exec_ctx,
                             grpc_channel_element *elem,
                             grpc_transport_op *op) {}

static grpc_error *InitCallElem(grpc_exec_ctx *exec_ctx,
                                grpc_call_element *elem,
                                const grpc_call_element_args *args) {
  return GRPC_ERROR_NONE;
}

static void SetPollsetOrPollsetSet(grpc_exec_ctx *exec_ctx,
                                   grpc_call_element *elem,
                                   grpc_polling_entity *pollent) {}

static void DestroyCallElem(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                            const grpc_call_final_info *final_info,
                            void *and_free_memory) {}

grpc_error *InitChannelElem(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                            grpc_channel_element_args *args) {
  return GRPC_ERROR_NONE;
}

void DestroyChannelElem(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem) {}

char *GetPeer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  return gpr_strdup("peer");
}

void GetChannelInfo(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                    const grpc_channel_info *channel_info) {}

static const grpc_channel_filter dummy_filter = {StartTransportStreamOp,
                                                 StartTransportOp,
                                                 0,
                                                 InitCallElem,
                                                 SetPollsetOrPollsetSet,
                                                 DestroyCallElem,
                                                 0,
                                                 InitChannelElem,
                                                 DestroyChannelElem,
                                                 GetPeer,
                                                 GetChannelInfo,
                                                 "dummy_filter"};

}  // namespace dummy_filter

namespace dummy_transport {

/* Memory required for a single stream element - this is allocated by upper
   layers and initialized by the transport */
size_t sizeof_stream; /* = sizeof(transport stream) */

/* name of this transport implementation */
const char *name;

/* implementation of grpc_transport_init_stream */
int InitStream(grpc_exec_ctx *exec_ctx, grpc_transport *self,
               grpc_stream *stream, grpc_stream_refcount *refcount,
               const void *server_data) {
  return 0;
}

/* implementation of grpc_transport_set_pollset */
void SetPollset(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                grpc_stream *stream, grpc_pollset *pollset) {}

/* implementation of grpc_transport_set_pollset */
void SetPollsetSet(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                   grpc_stream *stream, grpc_pollset_set *pollset_set) {}

/* implementation of grpc_transport_perform_stream_op */
void PerformStreamOp(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                     grpc_stream *stream, grpc_transport_stream_op *op) {
  grpc_closure_sched(exec_ctx, op->on_complete, GRPC_ERROR_NONE);
}

/* implementation of grpc_transport_perform_op */
void PerformOp(grpc_exec_ctx *exec_ctx, grpc_transport *self,
               grpc_transport_op *op) {}

/* implementation of grpc_transport_destroy_stream */
void DestroyStream(grpc_exec_ctx *exec_ctx, grpc_transport *self,
                   grpc_stream *stream, void *and_free_memory) {}

/* implementation of grpc_transport_destroy */
void Destroy(grpc_exec_ctx *exec_ctx, grpc_transport *self) {}

/* implementation of grpc_transport_get_peer */
char *GetPeer(grpc_exec_ctx *exec_ctx, grpc_transport *self) {
  return gpr_strdup("transport_peer");
}

/* implementation of grpc_transport_get_endpoint */
grpc_endpoint *GetEndpoint(grpc_exec_ctx *exec_ctx, grpc_transport *self) {
  return nullptr;
}

static const grpc_transport_vtable dummy_transport_vtable = {
    0,          "dummy_http2", InitStream,
    SetPollset, SetPollsetSet, PerformStreamOp,
    PerformOp,  DestroyStream, Destroy,
    GetPeer,    GetEndpoint};

static grpc_transport dummy_transport = {&dummy_transport_vtable};

}  // namespace dummy_transport

class NoOp {
 public:
  class Op {
   public:
    Op(grpc_exec_ctx *exec_ctx, NoOp *p, grpc_call_stack *s) {}
    void Finish(grpc_exec_ctx *exec_ctx) {}
  };
};

class SendEmptyMetadata {
 public:
  SendEmptyMetadata() {
    memset(&op_, 0, sizeof(op_));
    op_.on_complete = grpc_closure_init(&closure_, DoNothing, nullptr,
                                        grpc_schedule_on_exec_ctx);
  }

  class Op {
   public:
    Op(grpc_exec_ctx *exec_ctx, SendEmptyMetadata *p, grpc_call_stack *s) {
      grpc_metadata_batch_init(&batch_);
      p->op_.send_initial_metadata = &batch_;
    }
    void Finish(grpc_exec_ctx *exec_ctx) {
      grpc_metadata_batch_destroy(exec_ctx, &batch_);
    }

   private:
    grpc_metadata_batch batch_;
  };

 private:
  const gpr_timespec deadline_ = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  const gpr_timespec start_time_ = gpr_now(GPR_CLOCK_MONOTONIC);
  const grpc_slice method_ = grpc_slice_from_static_string("/foo/bar");
  grpc_transport_stream_op op_;
  grpc_closure closure_;
};

// Test a filter in isolation. Fixture specifies the filter under test (use the
// Fixture<> template to specify this), and TestOp defines some unit of work to
// perform on said filter.
template <class Fixture, class TestOp>
static void BM_IsolatedFilter(benchmark::State &state) {
  TrackCounters track_counters;
  Fixture fixture;
  std::ostringstream label;

  std::vector<grpc_arg> args;
  FakeClientChannelFactory fake_client_channel_factory;
  args.push_back(grpc_client_channel_factory_create_channel_arg(
      &fake_client_channel_factory));
  args.push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));

  grpc_channel_args channel_args = {args.size(), &args[0]};

  std::vector<const grpc_channel_filter *> filters;
  if (fixture.filter != nullptr) {
    filters.push_back(fixture.filter);
  }
  if (fixture.flags & CHECKS_NOT_LAST) {
    filters.push_back(&dummy_filter::dummy_filter);
    label << " #has_dummy_filter";
  }

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  size_t channel_size = grpc_channel_stack_size(&filters[0], filters.size());
  grpc_channel_stack *channel_stack =
      static_cast<grpc_channel_stack *>(gpr_zalloc(channel_size));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "call_stack_init",
      grpc_channel_stack_init(&exec_ctx, 1, FilterDestroy, channel_stack,
                              &filters[0], filters.size(), &channel_args,
                              fixture.flags & REQUIRES_TRANSPORT
                                  ? &dummy_transport::dummy_transport
                                  : nullptr,
                              "CHANNEL", channel_stack)));
  grpc_exec_ctx_flush(&exec_ctx);
  grpc_call_stack *call_stack = static_cast<grpc_call_stack *>(
      gpr_zalloc(channel_stack->call_stack_size));
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);
  grpc_slice method = grpc_slice_from_static_string("/foo/bar");
  grpc_call_final_info final_info;
  TestOp test_op_data;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(grpc_call_stack_init(&exec_ctx, channel_stack, 1,
                                          DoNothing, NULL, NULL, NULL, method,
                                          start_time, deadline, call_stack));
    typename TestOp::Op op(&exec_ctx, &test_op_data, call_stack);
    grpc_call_stack_destroy(&exec_ctx, call_stack, &final_info, NULL);
    op.Finish(&exec_ctx);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_channel_stack_destroy(&exec_ctx, channel_stack);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(channel_stack);
  gpr_free(call_stack);

  state.SetLabel(label.str());
  track_counters.Finish(state);
}

typedef Fixture<nullptr, 0> NoFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, NoFilter, NoOp);
typedef Fixture<&dummy_filter::dummy_filter, 0> DummyFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, DummyFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, DummyFilter, SendEmptyMetadata);
typedef Fixture<&grpc_client_channel_filter, 0> ClientChannelFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientChannelFilter, NoOp);
typedef Fixture<&grpc_compress_filter, CHECKS_NOT_LAST> CompressFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, CompressFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, CompressFilter, SendEmptyMetadata);
typedef Fixture<&grpc_client_deadline_filter, CHECKS_NOT_LAST>
    ClientDeadlineFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientDeadlineFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ClientDeadlineFilter, SendEmptyMetadata);
typedef Fixture<&grpc_server_deadline_filter, CHECKS_NOT_LAST>
    ServerDeadlineFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ServerDeadlineFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, ServerDeadlineFilter, SendEmptyMetadata);
typedef Fixture<&grpc_http_client_filter, CHECKS_NOT_LAST | REQUIRES_TRANSPORT>
    HttpClientFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpClientFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpClientFilter, SendEmptyMetadata);
typedef Fixture<&grpc_http_server_filter, CHECKS_NOT_LAST> HttpServerFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpServerFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, HttpServerFilter, SendEmptyMetadata);
typedef Fixture<&grpc_message_size_filter, CHECKS_NOT_LAST> MessageSizeFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, MessageSizeFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, MessageSizeFilter, SendEmptyMetadata);
typedef Fixture<&grpc_load_reporting_filter, CHECKS_NOT_LAST>
    LoadReportingFilter;
BENCHMARK_TEMPLATE(BM_IsolatedFilter, LoadReportingFilter, NoOp);
BENCHMARK_TEMPLATE(BM_IsolatedFilter, LoadReportingFilter, SendEmptyMetadata);

BENCHMARK_MAIN();
