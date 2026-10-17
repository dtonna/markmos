// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#include "mm_job_system.hpp"
#include "core/mm_pool.hpp"
#include "core/mm_types.h"
#include "mm_tracy.hpp"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

// ─── JobQueue ────────────────────────────────────────────────────

bool JobQueue::push(const Job &job) noexcept {
    acquire();
    u32 next = (tail_ + 1) & (kMaxQueuedJobs - 1);
    if (next == head_) { // full
        release();
        return false;
    }
    jobs_[tail_] = job;
    tail_        = next;
    release();
    return true;
}

bool JobQueue::pop(Job &out) noexcept {
    acquire();
    if (head_ == tail_) { // empty
        release();
        return false;
    }
    out   = jobs_[head_];
    head_ = (head_ + 1) & (kMaxQueuedJobs - 1);
    release();
    return true;
}

bool JobQueue::empty() const noexcept {
    // Single-threaded check; only safe when no concurrent pushes/pops
    return head_ == tail_;
}

// ─── JobSystem ───────────────────────────────────────────────────

void JobSystem::init(u32 worker_count) noexcept {
    if (running_.load(std::memory_order_acquire)) {
        return;
    }
    if (worker_count == 0) {
        long n       = sysconf(_SC_NPROCESSORS_ONLN);
        worker_count = (n > 1) ? static_cast<u32>(n - 1) : 1;
        if (worker_count > 8) {
            worker_count = 8;
        }
    }
    // kMaxWorkers, not worker_count: init() clamps below, but a clamp that only
    // exists in one place is a clamp that gets forgotten.
    if (worker_count > kMaxWorkers) {
        worker_count = kMaxWorkers;
    }
    worker_count_ = worker_count;

    running_.store(true, std::memory_order_release);


    for (u32 i = 0; i < worker_count_; ++i) {
        workers_[i].id     = i;
        workers_[i].system = this;
        // Fatal on failure, same reasoning as the save worker: the pthread version
        // ignored the return value and left running_ true with fewer workers than
        // init() was asked for, which on a device that cannot spare a thread looks
        // exactly like a slow load rather than a failure.
        workers_[i].thread = std::thread(worker_entry, &workers_[i]);
    }
}

void JobSystem::shutdown() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
        wake_cond_.notify_all();
    }

    for (u32 i = 0; i < worker_count_; ++i) {
        if (workers_[i].thread.joinable()) {
            workers_[i].thread.join();
        }
        workers_[i] = Worker{}; // runs ~std::thread on a joined handle
    }

    worker_count_ = 0;
    // No frees and no destroys: the array is inline, and the mutex and condvar
    // clean themselves up.
}

void JobSystem::run(const Job &job) noexcept {
    while (!queue_.push(job)) {
        // Queue full — process a job to make room
        Job j;
        if (queue_.pop(j)) {
            execute(j);
        }
    }
    // Wake one worker
        // Notified inside the lock: the 1 ms poll in worker_loop() makes a lost
        // wakeup cost latency rather than a stall, and holding the lock keeps that
        // true without having to reason about the window either way.
        std::lock_guard<std::mutex> lock(wake_mutex_);
        wake_cond_.notify_one();
}

void JobSystem::run_fn(void (*fn)(void *), void *data) noexcept {
    Job job{fn, data, nullptr};
    run(job);
}

void JobSystem::run_dep(void (*fn)(void *), void *data, JobCounter *counter) noexcept {
    Job job{fn, data, counter};
    run(job);
}

void JobSystem::wait(JobCounter *counter) noexcept {
    while (counter->value.load(std::memory_order_acquire) > 0) {
        // Help process jobs while waiting (prevents deadlock)
        Job job;
        if (queue_.pop(job)) {
            execute(job);
        }
    }
}

void *JobSystem::worker_entry(void *arg) noexcept {
    auto *worker = static_cast<Worker *>(arg);
    // rpmalloc is per-thread and jobs here touch the VFS, which allocates. A
    // worker with no heap of its own faults inside rpmalloc() on the first
    // allocation over 4096 bytes.
    pool_thread_initialize();
    worker->system->worker_loop(worker->id);
    pool_thread_finalize();
    return nullptr;
}

void JobSystem::worker_loop(u32 id) noexcept {
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
            // No work — wait on the condition, but only briefly. The 1 ms timeout
            // is a POLL, not a latency budget: it is how a worker notices running_
            // going false without needing shutdown() to wake every thread exactly
            // once, so join() can never outlive the flag it is waiting on.
            std::unique_lock<std::mutex> lock(wake_mutex_);
            if (queue_.empty()) {
                wake_cond_.wait_for(lock, std::chrono::milliseconds(1));
            }
        }
    }
}

void JobSystem::execute(const Job &job) noexcept {
    ZoneScoped;
    if (job.fn) {
        job.fn(job.data);
    }

    if (job.counter) {
        u32 prev = job.counter->value.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1 && job.counter->pending.fn) {
            // This was the last dependency — enqueue the pending job
            Job pending{job.counter->pending.fn, job.counter->pending.data, nullptr};
            while (!queue_.push(pending)) {
                // Queue full — process a job to make room
                Job j;
                if (queue_.pop(j)) {
                    execute(j);
                }
            }
            std::lock_guard<std::mutex> lock(wake_mutex_);
            wake_cond_.notify_one();
        }
    }
}

// ─── Global Singleton ────────────────────────────────────────────

JobSystem g_job_system;

void      JobSystemInit(u32 num_workers) noexcept {
    if (g_job_system.is_initialized()) {
        return;
    }
    g_job_system.init(num_workers);
}

void JobSystemShutdown() noexcept { g_job_system.shutdown(); }
