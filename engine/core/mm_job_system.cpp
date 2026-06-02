// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_job_system.hpp"
#include "mm_tracy.hpp"
#include <cstdlib>
#include <ctime>
#include <unistd.h>

// ─── JobQueue ────────────────────────────────────────────────────

bool JobQueue::push(const Job& job) noexcept {
    acquire();
    uint32_t next = (tail_ + 1) & (kMaxQueuedJobs - 1);
    if (next == head_) {  // full
        release();
        return false;
    }
    jobs_[tail_] = job;
    tail_ = next;
    release();
    return true;
}

bool JobQueue::pop(Job& out) noexcept {
    acquire();
    if (head_ == tail_) {  // empty
        release();
        return false;
    }
    out = jobs_[head_];
    head_ = (head_ + 1) & (kMaxQueuedJobs - 1);
    release();
    return true;
}

bool JobQueue::empty() const noexcept {
    // Single-threaded check; only safe when no concurrent pushes/pops
    return head_ == tail_;
}

// ─── JobSystem ───────────────────────────────────────────────────

void JobSystem::init(uint32_t worker_count) noexcept {
    if (worker_count == 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        worker_count = (n > 1) ? static_cast<uint32_t>(n - 1) : 1;
        if (worker_count > 8) worker_count = 8;
    }
    worker_count_ = worker_count;
    workers_ = static_cast<Worker*>(std::malloc(sizeof(Worker) * worker_count_));
    if (!workers_) return;

    running_.store(true, std::memory_order_release);

    pthread_mutex_init(&wake_mutex_, nullptr);
    pthread_cond_init(&wake_cond_, nullptr);

    for (uint32_t i = 0; i < worker_count_; ++i) {
        workers_[i].id     = i;
        workers_[i].system = this;
        pthread_create(&workers_[i].thread, nullptr, worker_entry, &workers_[i]);
    }
}

void JobSystem::shutdown() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    pthread_mutex_lock(&wake_mutex_);
    pthread_cond_broadcast(&wake_cond_);
    pthread_mutex_unlock(&wake_mutex_);

    for (uint32_t i = 0; i < worker_count_; ++i) {
        pthread_join(workers_[i].thread, nullptr);
    }

    std::free(workers_);
    workers_ = nullptr;
    worker_count_ = 0;

    pthread_mutex_destroy(&wake_mutex_);
    pthread_cond_destroy(&wake_cond_);
}

void JobSystem::run(const Job& job) noexcept {
    while (!queue_.push(job)) {
        // Queue full — process a job to make room
        Job j;
        if (queue_.pop(j)) execute(j);
    }
    // Wake one worker
    pthread_mutex_lock(&wake_mutex_);
    pthread_cond_signal(&wake_cond_);
    pthread_mutex_unlock(&wake_mutex_);
}

void JobSystem::run_fn(void (*fn)(void*), void* data) noexcept {
    Job job{fn, data, nullptr};
    run(job);
}

void JobSystem::run_dep(void (*fn)(void*), void* data, JobCounter* counter) noexcept {
    Job job{fn, data, counter};
    run(job);
}

void JobSystem::wait(JobCounter* counter) noexcept {
    while (counter->value.load(std::memory_order_acquire) > 0) {
        // Help process jobs while waiting (prevents deadlock)
        Job job;
        if (queue_.pop(job)) {
            execute(job);
        }
    }
}

void* JobSystem::worker_entry(void* arg) noexcept {
    auto* worker = static_cast<Worker*>(arg);
    worker->system->worker_loop(worker->id);
    return nullptr;
}

void JobSystem::worker_loop(uint32_t id) noexcept {
    (void)id;
#ifdef TRACY_ENABLE
    tracy::SetThreadName("Worker");
#endif
    while (running_.load(std::memory_order_acquire)) {
        Job job;
        if (queue_.pop(job)) {
            execute(job);
        } else {
            // No work — wait on condition
            pthread_mutex_lock(&wake_mutex_);
            if (queue_.empty()) {
                struct timespec ts{};
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 1'000'000;  // 1ms timeout
                pthread_cond_timedwait(&wake_cond_, &wake_mutex_, &ts);
            }
            pthread_mutex_unlock(&wake_mutex_);
        }
    }
}

void JobSystem::execute(const Job& job) noexcept {
    ZoneScoped;
    if (job.fn) job.fn(job.data);

    if (job.counter) {
        uint32_t prev = job.counter->value.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1 && job.counter->pending.fn) {
            // This was the last dependency — enqueue the pending job
            Job pending{job.counter->pending.fn, job.counter->pending.data, nullptr};
            while (!queue_.push(pending)) {
                // Queue full — process a job to make room
                Job j;
                if (queue_.pop(j)) execute(j);
            }
            pthread_mutex_lock(&wake_mutex_);
            pthread_cond_signal(&wake_cond_);
            pthread_mutex_unlock(&wake_mutex_);
        }
    }
}

// ─── Global Singleton ────────────────────────────────────────────

JobSystem g_job_system;

void JobSystemInit(uint32_t num_workers) noexcept {
    g_job_system.init(num_workers);
}

void JobSystemShutdown() noexcept {
    g_job_system.shutdown();
}
