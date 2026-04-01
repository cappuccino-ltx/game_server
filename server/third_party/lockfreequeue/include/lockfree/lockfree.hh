// Implemented multiple lock-free queues for different scenarios,
// Project address:
// https://github.com/cappuccino-ltx
// The readme file of the project provides a detailed elaboration on the design approach
//
// Copyright 2026 cappuccino-ltx
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


#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <atomic>
#include <thread>
#include <vector>
#include <memory>

namespace lockfree{

enum queue_size {
    K003 = 1llu << 5, // 32
    K01 = 1llu << 7,  // 128
    K02 = 1llu << 8,  // 256
    K05 = 1llu << 9,  // 512
    K1 = 1llu << 10,  // 1024
    K2 = 1llu << 11,  // 2048
    K4 = 1llu << 12,  // 4096
    K8 = 1llu << 13,  // 8192
    K16 = 1llu << 14, // 16384
    K32 = 1llu << 15 // 32768
}; // enum queue_size
namespace util{
static inline size_t glign_to_2_index(long long num){
    if (num <= 1) {
        return 1;
    }
    num--;
    num |= num >> 1;
    num |= num >> 2;
    num |= num >> 4;
    num |= num >> 8;
    num |= num >> 16;
    num |= num >> 32;
    return num + 1;
}
static inline queue_size get_proper_size(std::size_t n){
    if (n <= queue_size::K003) return queue_size::K003;
    else if (n <= queue_size::K01) return queue_size::K01;
    else if (n <= queue_size::K02) return queue_size::K02;
    else if (n <= queue_size::K05) return queue_size::K05;
    else if (n <= queue_size::K1) return queue_size::K1;
    else if (n <= queue_size::K2) return queue_size::K2;
    else if (n <= queue_size::K4) return queue_size::K4;
    else if (n <= queue_size::K8) return queue_size::K8;
    else if (n <= queue_size::K16) return queue_size::K16;
    else return queue_size::K32;
}
template<typename T>
class optional {
private:
    alignas(T) unsigned char storage_[sizeof(T)];
    T* ptr() {
        return reinterpret_cast<T*>(storage_);
    }
    const T* ptr() const {
        return reinterpret_cast<const T*>(storage_);
    }

public:
    optional() noexcept = default;
    // reset
    void reset() {
        ptr()->~T();
    }
    void reset(const T& value) {
        new (storage_) T(value);
    }
    void reset(T&& value) {
        new (storage_) T(std::move(value));
    }
    T& value() {
        return *ptr();
    }
    const T& value() const {
        return *ptr();
    }
};
} // namespace util

#define LOCKFREE_QUEUE_SIZE_DEFAULT queue_size::K1
#define LOCKFREE_QUEUE_MPMC_DEFALUT_TASK_LIMIT 32

template<class T>
class lfree_queue {
private:
    struct slot{
        std::atomic_uint64_t seqence;
        util::optional<T> data;
    };
public:
    lfree_queue(lockfree::queue_size size = LOCKFREE_QUEUE_SIZE_DEFAULT)
        :queue_size_(util::get_proper_size(size))
        // ,seqence_(queue_size_)
        ,buffer_(queue_size_)
    {
        // init seqence
        for(int i = 0; i < queue_size_; i++) {
            buffer_[i].seqence = i;
        }
    }

    template<typename U>
    bool try_put(U&& data){
        size_t tail = tail_r();
        size_t head = head_r();
        int index = tail & (queue_size_ - 1);
        size_t seqence = seqence_a(index);
        if (seqence == tail) {
            if (tail_cas(tail)){
                buffer_[index].data.reset(std::forward<U>(data));
                ++seqence;
                seqence_store(index, (seqence));
                return true;
            }
        }
        return false;
    }
    
    bool try_get(T& data) {
        size_t tail = tail_r();
        size_t head = head_r();
        int index = head & (queue_size_ - 1);
        size_t seqence = seqence_a(index);
        if (seqence == head + 1) {
            if (head_cas(head)){
                data = std::move(buffer_[index].data.value());
                buffer_[index].data.reset();
                seqence = head + queue_size_;
                seqence_store(index, (seqence));
                return true;
            }
        }
        return false;
    }

    size_t size_approx(){
        size_t h = head_r();
        size_t t = tail_r();
        return t - h;
    }
    
    bool is_full(){
        size_t h = head_r();
        size_t t = tail_r();
        return t - h == queue_size_;
    }

private:
    inline size_t head_r(){
        return head_.load(std::memory_order_relaxed);
    }
    inline size_t head_a(){
        return head_.load(std::memory_order_acquire);
    }
    inline bool head_cas(size_t& h){
        return head_.compare_exchange_strong(h, h + 1, std::memory_order_acq_rel,std::memory_order_relaxed);
    }
    inline size_t tail_r(){
        return tail_.load(std::memory_order_relaxed);
    }
    inline size_t tail_a(){
        return tail_.load(std::memory_order_acquire);
    }
    inline bool tail_cas(size_t& t){
        return tail_.compare_exchange_strong(t, t + 1, std::memory_order_acq_rel,std::memory_order_relaxed);
    }
    inline uint64_t seqence_r(int index){
        return buffer_[index].seqence.load(std::memory_order_relaxed);
    }
    inline uint64_t seqence_a(int index){
        return buffer_[index].seqence.load(std::memory_order_acquire);
    }
    inline void seqence_store(int index, uint64_t value){
        buffer_[index].seqence.store(value, std::memory_order_release);
    }

private:
    alignas(uint64_t) std::atomic_uint64_t head_ { 0 };
    alignas(uint64_t) std::atomic_uint64_t tail_ { 0 };
    int queue_size_;
    std::vector<slot> buffer_;
}; // template class lfree_queue


template<class T>
class lfree_queue_spsc {
public:
    lfree_queue_spsc(lockfree::queue_size size = LOCKFREE_QUEUE_SIZE_DEFAULT)
        :queue_size_(util::get_proper_size(size)),
        buffer_(queue_size_)
    {}

    template<typename U>
    bool try_put(U&& data){
        if (producer_id_ == std::thread::id())
            producer_id_ = std::this_thread::get_id();
        assert(std::this_thread::get_id() == producer_id_);
        size_t h = head_r();
        size_t t = tail_a();
        if (t - h == queue_size_) {
            return false;
        }
        size_t producer_index = t & (queue_size_ - 1);
        buffer_[producer_index].reset(std::forward<U>(data));
        tail_add();
        return true;
    }
    
    bool try_get(T& data) {
        if (consumer_id_ == std::thread::id())
            consumer_id_ = std::this_thread::get_id();
        assert(std::this_thread::get_id() == consumer_id_);
        size_t h = head_r();
        size_t t = tail_a();
        if(h == t) {
            return false;
        }
        size_t consumer_index = h & (queue_size_ - 1);
        data = std::move(buffer_[consumer_index].value());
        buffer_[consumer_index].reset();
        head_add();
        return true;
    }

    size_t size_approx(){
        size_t h = head_r();
        size_t t = tail_r();
        return t - h;
    }

    bool is_full(){
        size_t h = head_r();
        size_t t = tail_r();
        return t - h == queue_size_;
    }

private:
    inline size_t head_r(){
        return head_.load(std::memory_order_relaxed);
    }
    inline size_t head_a(){
        return head_.load(std::memory_order_acquire);
    }
    inline void head_add(){
        head_.fetch_add(1,std::memory_order_release);
    }
    inline size_t tail_r(){
        return tail_.load(std::memory_order_relaxed);
    }
    inline size_t tail_a(){
        return tail_.load(std::memory_order_acquire);
    }
    inline void tail_add(){
        tail_.fetch_add(1,std::memory_order_release);
    }

private:
    alignas(64) std::atomic_uint64_t head_ { 0 };
    alignas(64) std::atomic_uint64_t tail_ { 0 };
    int queue_size_;
    std::vector<util::optional<T>> buffer_;
    std::thread::id producer_id_ = std::thread::id();
    std::thread::id consumer_id_ = std::thread::id();
}; // template class lfree_queue_spsc

template<class T>
class lfree_queue_spmc {
public:
    lfree_queue_spmc(int consume_n, lockfree::queue_size size = queue_size::K05)
        :queues_(consume_n, nullptr),queue_size_(consume_n)
    {
        for (int i = 0; i < consume_n; i++) {
            queues_[i] = std::make_shared<lfree_queue<T>>(util::get_proper_size(size / consume_n));
        }
    }

    template<typename U>
    bool try_put(U&& data){
        if (producer_id_ == std::thread::id())
            producer_id_ = std::this_thread::get_id();
        assert(std::this_thread::get_id() == producer_id_);
        for(int i = 0; i < queue_size_; i++) {
            size_t index = index_++ % queue_size_;
            bool ret = queues_[index]->try_put(std::forward<U>(data));
            if (ret) {
                return true;
            }
        }
        return false;
    }
    
    bool try_get(T& data) {
        thread_local int index = -1;
        thread_local int steal_index = -1;
        if (index == -1) {
            index = next_id();
            steal_index = (index + 1) % queue_size_;
        }
        bool ret = queues_[index]->try_get(data);
        if (ret) {
            return true;
        }
        ret = queues_[steal_index]->try_get(data);
        if (ret) {
            return true;
        }
        do{
            steal_index = (steal_index + 1) % queue_size_;
        }while(steal_index == index && queue_size_ != 1);
        return false;
    }

    size_t size_approx(){
        size_t ret = 0;
        for (auto& q : queues_) {
            ret += q->size_approx();
        }
        return ret;
    }

    bool is_full(){
        for (auto& q : queues_) {
            if(!q->is_full()) {
                return false;
            }
        }
        return true;
    }

private:
    int next_id(){
        assert(next_id_.load(std::memory_order_acquire) < queue_size_);
        int index = next_id_.fetch_add(1);
        assert(index < queue_size_);
        return index;
    }

private:
    size_t index_ { 0 };
    std::vector<std::shared_ptr<lfree_queue<T>>> queues_;
    std::atomic_int next_id_ { 0 };
    std::thread::id producer_id_ = std::thread::id();
    int queue_size_;
}; // template class lfree_queue_spmc

template<class T>
class lfree_queue_mpsc {
public:
    lfree_queue_mpsc(int producer_n, lockfree::queue_size size = queue_size::K05)
        :queues_(producer_n, nullptr),queue_size_(producer_n)
    {
        for (int i = 0; i < producer_n; i++) {
            queues_[i] = std::make_shared<lfree_queue_spsc<T>>(util::get_proper_size(size / producer_n));
        }
    }

    template<typename U>
    bool try_put(U&& data){
        thread_local int index = -1;
        if (index == -1) {
            index = next_id();
        }
        return queues_[index]->try_put(std::forward<U>(data));
    }
    
    bool try_get(T& data) {
        if (consumer_id_ == std::thread::id())
            consumer_id_ = std::this_thread::get_id();
        assert(std::this_thread::get_id() == consumer_id_);
        for(int i = 0; i < queue_size_; i++) {
            size_t index = index_++ % queue_size_;
            bool ret = queues_[index]->try_get(data);
            if (ret) {
                return true;
            }
        }
        return false;
    }

    size_t size_approx(){
        size_t ret = 0;
        for (auto& q : queues_) {
            ret += q->size_approx();
        }
        return ret;
    }
    bool is_full(){
        for (auto& q : queues_) {
            if(!q->is_full()) {
                return false;
            }
        }
        return true;
    }

private:
    int next_id(){
        assert(next_id_.load(std::memory_order_acquire) < queue_size_);
        int index = next_id_.fetch_add(1);
        assert(index < queue_size_);
        return index;
    }

private:
    size_t index_ { 0 };
    std::vector<std::shared_ptr<lfree_queue_spsc<T>>> queues_;
    std::atomic_int next_id_ { 0 };
    std::thread::id consumer_id_ = std::thread::id();
    int queue_size_;
}; // template class lfree_queue_mpsc

template<class T>
class lfree_queue_mpmc {
public:
    lfree_queue_mpmc(int producer_n, int consumer_n, lockfree::queue_size size = queue_size::K01)
        :queues_(producer_n, nullptr)
        ,producer_n_(producer_n)
        ,consumer_n_(consumer_n)
    {
        for (int i = 0; i < producer_n_; i++) {
            queues_[i] = std::make_shared<lfree_queue_spmc<T>>(consumer_n_, util::get_proper_size(size / consumer_n_));
        }
    }

    template<typename U>
    bool try_put(U&& data){
        thread_local int index = -1;
        if (index == -1) {
            index = next_id();
        }
        return queues_[index]->try_put(std::forward<U>(data));
    }
    
    bool try_get(T& data) {
        thread_local int current_task_limit = task_limit_;
        thread_local size_t index = --consumer_n_ % producer_n_;
        for(int i = 0; i < producer_n_; i++){
            int ret = queues_[index]->try_get(data);
            if (ret) {
                if(--current_task_limit == 0) {
                    index = (index + 1) % producer_n_;
                    current_task_limit = task_limit_;
                }
                return true;
            }
            index = (index + 1) % producer_n_;
            current_task_limit = task_limit_;
        }
        return false;
    }

    size_t size_approx(){
        size_t ret = 0;
        for (auto& q : queues_) {
            ret += q->size_approx();
        }
        return ret;
    }
    bool is_full(){
        for (auto& q : queues_) {
            if(!q->is_full()) {
                return false;
            }
        }
        return true;
    }
    void set_task_limit(int limit){
        task_limit_ = limit;
    }

private:
    int next_id(){
        assert(next_id_.load(std::memory_order_acquire) < producer_n_);
        int index = next_id_.fetch_add(1);
        assert(index < producer_n_);
        return index;
    }

private:
    std::vector<std::shared_ptr<lfree_queue_spmc<T>>> queues_;
    std::atomic_int next_id_ { 0 };
    std::atomic_int consumer_n_;
    int producer_n_;
    int task_limit_ = LOCKFREE_QUEUE_MPMC_DEFALUT_TASK_LIMIT;
}; // template class lfree_queue_mpmc

template<class T>
class concurrent_queue {
public:
    concurrent_queue(size_t thread_num = std::thread::hardware_concurrency(), lockfree::queue_size size = queue_size::K01)
        :queues_(thread_num, nullptr),queue_size_(thread_num)
    {
        for (int i = 0; i < thread_num; i++) {
            queues_[i] = std::make_shared<lfree_queue<T>>(util::get_proper_size(size / thread_num));
        }
    }

    template<typename U>
    bool try_put(U&& data){
        uint64_t tail = tail_a();
        while (tail_cas(tail)) {
            uint64_t index = tail % queue_size_;
            return queues_[index]->try_put(std::forward<U>(data));
        }
        return false;
    }
    
    bool try_get(T& data) {
        uint64_t head = head_a();
        while(head_cas(head)){
            uint64_t index = head % queue_size_;
            return queues_[index]->try_get(data);
        }
        return false;
    }

    size_t size_approx(){
        size_t ret = 0;
        for (auto& q : queues_) {
            ret += q->size_approx();
        }
        return ret;
    }
    bool is_full(){
        for (auto& q : queues_) {
            if(!q->is_full()) {
                return false;
            }
        }
        return true;
    }

private:
    inline size_t head_r(){
        return head_.load(std::memory_order_relaxed);
    }
    inline size_t head_a(){
        return head_.load(std::memory_order_acquire);
    }
    inline bool head_cas(size_t& h){
        return head_.compare_exchange_strong(h, h + 1, std::memory_order_acq_rel,std::memory_order_relaxed);
    }
    inline size_t tail_r(){
        return tail_.load(std::memory_order_relaxed);
    }
    inline size_t tail_a(){
        return tail_.load(std::memory_order_acquire);
    }
    inline bool tail_cas(size_t& t){
        return tail_.compare_exchange_strong(t, t + 1, std::memory_order_acq_rel,std::memory_order_relaxed);
    }

private:
    std::vector<std::shared_ptr<lfree_queue<T>>> queues_;
    alignas(64) std::atomic_uint64_t head_ { 0 };
    alignas(64) std::atomic_uint64_t tail_ { 0 };
    int queue_size_;
}; // template class concurrent_queue

} // namespace lockfree