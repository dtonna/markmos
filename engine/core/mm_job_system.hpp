// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <atomic>
#include "core/mm_types.h"
#include <cstddef>
#include <cstdint>
#include <pthread.h>

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

    void     run(const Job &job) noexcept;
    void     run_fn(void (*fn)(void *), void *data) noexcept;
    void     run_dep(void (*fn)(void *), void *data, JobCounter *counter) noexcept;

    // Wait until counter reaches 0 — helps process jobs while waiting
    void     wait(JobCounter *counter) noexcept;

    u32 worker_count() const noexcept { return worker_count_; }
    bool     is_initialized() const noexcept { return running_.load(std::memory_order_acquire); }

  private:
    struct Worker {
        pthread_t  thread;
        u32   id;
        JobSystem *system;
    };

    JobQueue          queue_;
    Worker           *workers_      = nullptr;
    u32          worker_count_ = 0;
    std::atomic<bool> running_{false};

    pthread_mutex_t   wake_mutex_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t    wake_cond_  = PTHREAD_COND_INITIALIZER;

    static void      *worker_entry(void *arg) noexcept;
    void              worker_loop(u32 id) noexcept;
    void              execute(const Job &job) noexcept;
};

// ─── Global Singleton ────────────────────────────────────────────
extern JobSystem g_job_system;

void             JobSystemInit(u32 num_workers = 0) noexcept;
void             JobSystemShutdown() noexcept;
