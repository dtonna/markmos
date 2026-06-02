// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#if defined(__ANDROID__)
#    include <android/log.h>
#    ifndef MM_LOG_TAG
#        define MM_LOG_TAG "Markmos"
#    endif
#    define MM_LOG(...) __android_log_print(ANDROID_LOG_INFO, MM_LOG_TAG, __VA_ARGS__)
#    define MM_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, MM_LOG_TAG, __VA_ARGS__)
#else
#    include <cstdio>
#    define MM_LOG(...) do { printf("[Markmos] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#    define MM_ERROR(...) do { fprintf(stderr, "[Markmos ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#endif
