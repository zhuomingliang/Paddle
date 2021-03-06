//   Copyright (c) 2018 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/fluid/framework/details/fetch_op_handle.h"

namespace paddle {
namespace framework {
namespace details {

FetchOpHandle::FetchOpHandle(FeedFetchList *data, size_t offset,
                             std::vector<Scope *> *local_scopes)
    : data_(data), offset_(offset), local_scopes_(local_scopes) {}

FetchOpHandle::~FetchOpHandle() {
  for (auto *input_var : inputs_) {
    input_var->pending_ops_.erase(this);
  }
}

void FetchOpHandle::Wait(platform::DeviceContext *waited_dev) {
  PADDLE_THROW("Nobody should wait FetchOp. Unexpceted Error");
}

void FetchOpHandle::WaitAndMergeCPUTensors() const {
  std::vector<const LoDTensor *> tensors_ptr;
  tensors_ptr.reserve(tensors_.size());
  for (auto &t : tensors_) {
    tensors_ptr.emplace_back(&t);
  }
  data_->at(offset_).MergeLoDTensor(tensors_ptr, platform::CPUPlace());
}

void FetchOpHandle::RunImpl() {
  auto cpu_ctx =
      platform::DeviceContextPool::Instance().Get(platform::CPUPlace());
  for (auto *input : inputs_) {
    auto *var = static_cast<VarHandle *>(input);
    var->generated_op_->Wait(cpu_ctx);
  }

  tensors_.resize(inputs_.size());
  auto *var = static_cast<VarHandle *>(inputs_[0]);
  auto &var_name = var->name_;
  platform::CPUPlace cpu;
  auto &scopes = *local_scopes_;

  for (size_t i = 0; i < scopes.size(); ++i) {
    auto &scope = scopes[i];
    auto &t = scope->FindVar(var_name)->Get<framework::LoDTensor>();
    if (platform::is_gpu_place(var->place_)) {
#ifdef PADDLE_WITH_CUDA
      TensorCopy(t, cpu, *dev_ctxes_[t.place()], &tensors_[i]);
      dev_ctxes_[t.place()]->Wait();
#endif
    } else {
      tensors_[i].ShareDataWith(t);
      tensors_[i].set_lod(t.lod());
    }
  }

  this->WaitAndMergeCPUTensors();
}

std::string FetchOpHandle::Name() const { return "Fetch"; }

}  // namespace details
}  // namespace framework
}  // namespace paddle
