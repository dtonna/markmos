// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <atomic>
#include "core/mm_types.h"
#include <cstddef>
#include <cstdint>
// std::thread / std::mutex / std::condition_variable throughout. See the note on
// Worker below for why the worker array had to stop being malloc'd.
#include <condition_variable>
#include <mutex>
#include <thread>

// ─── Forward Declarations ───────────────────────────────────────
struct JobCounter;

// ─── Job ─────────────────────────────────────────────────────────
struct JobDesc {
    void (*fn)(void *) = nullptr;
    void *data         = nullptr;
};

struct Job {
    void (*fn)(void *);
    void       *data;
    JobCounter *counter; // decremented after execution; if 0, enqueue counter->pending
};

// ─── Job Counter (for dependencies) ─────────────────────────────
struct JobCounter {
    std::atomic<u32> value{0};
    JobDesc               pending; // executed when value reaches 0
};

// ─── Job Queue (lock-free bounded ring, spinlock) ───────────────
static constexpr u32 kMaxQueuedJobs = 4096;

class JobQueue {
  public:
    bool push(const Job &job) noexcept;
    bool pop(Job &out) noexcept;
    bool empty() const noexcept;

  private:
    Job              jobs_[kMaxQueuedJobs];
    u32         head_ = 0;
    u32         tail_ = 0;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;

    void             acquire() noexcept {
        while (lock_.test_and_set(std::memory_order_acquire)) {
        }
    }
    void release() noexcept { lock_.clear(std::memory_order_release); }
};

// ─── Job System ──────────────────────────────────────────────────
class JobSystem {
  public:
    void     init(u32 worker_count = 0) noexcept;
    void     shutdown() noexcept;

    // Joining the workers belongs to the object that owns them. Without this the
    // global's destruction ran while workers were still looping over wake_mutex_,
    // and a destroyed std::mutex throws on lock() — which, built -fno-exceptions,
    // is std::terminate. The pthread version had the same bug and was invisible:
    // pthread_mutex_lock on a destroyed mutex returns EINVAL, and nobody checked.
    ~JobSystem() { shutdown(); }

    void     run(const Job &job) noexcept;
    void     run_fn(void (*fn)(void *), void *data) noexcept;
    void     run_dep(void (*fn)(void *), void *data, JobCounter *counter) noexcept;

    // Wait until counter reaches 0 — helps process jobs while waiting
    void     wait(JobCounter *counter) noexcept;

    u32 worker_count() const noexcept { return worker_count_; }
    bool     is_initialized() const noexcept { return running_.load(std::memory_order_acquire); }

  private:
    struct Worker {
        std::thread thread{};
        u32         id     = 0;
        JobSystem  *system = nullptr;
    };

    // A FIXED array, not a malloc'd one.
    //
    // The workers were `Worker *workers_ = malloc(...)`, freed in shutdown(). That
    // was legal for pthread_t — a bare handle with a trivial destructor — and is
    // NOT for std::thread: its destructor is what joins, so free() tears the
    // storage out from under a live thread and no destructor ever runs.
    //
    // Migrating the member type WITHOUT fixing the allocation is how the emulator
    // started aborting with "Abort message: terminating" 0.4 s after
    // android_main(), before the renderer had finished initialising. The count
    // was already capped at 8 in init(), so this array is the same memory the
    // malloc asked for, minus the malloc.
    static constexpr u32 kMaxWorkers = 8;

    JobQueue          queue_;
    Worker            workers_[kMaxWorkers]{};
    u32               worker_count_ = 0;
    std::atomic<bool> running_{false};

    std::mutex             wake_mutex_;
    std::condition_variable wake_cond_;

    static void      *worker_entry(void *arg) noexcept;
    void              worker_loop(u32 id) noexcept;
    void              execute(const Job &job) noexcept;
};

// ─── Global Singleton ────────────────────────────────────────────
extern JobSystem g_job_system;

void             JobSystemInit(u32 num_workers = 0) noexcept;
void             JobSystemShutdown() noexcept;
