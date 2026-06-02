// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once
#include <cstddef>
#include <cassert>
#include <type_traits>

// Span2D — lightweight 2D view over contiguous row-major storage
// @cache_reason Contiguous memory = single allocation, linear iteration
// @zero_virtual No inheritance, no virtual dispatch
// @fallback When std::mdspan not available on target toolchain

template<typename T>
struct Span2D {
    T*     data_;
    size_t rows_;
    size_t cols_;

    constexpr Span2D() noexcept : data_(nullptr), rows_(0), cols_(0) {}

    constexpr Span2D(T* data, size_t rows, size_t cols) noexcept
        : data_(data), rows_(rows), cols_(cols) {}

    constexpr T& operator()(size_t row, size_t col) noexcept {
        assert(row < rows_ && col < cols_);
        return data_[row * cols_ + col];
    }

    constexpr const T& operator()(size_t row, size_t col) const noexcept {
        assert(row < rows_ && col < cols_);
        return data_[row * cols_ + col];
    }

    constexpr T* row(size_t r) noexcept {
        assert(r < rows_);
        return data_ + r * cols_;
    }

    constexpr const T* row(size_t r) const noexcept {
        assert(r < rows_);
        return data_ + r * cols_;
    }

    constexpr size_t rows() const noexcept { return rows_; }
    constexpr size_t cols() const noexcept { return cols_; }
    constexpr size_t size() const noexcept { return rows_ * cols_; }

    constexpr T* data() noexcept { return data_; }
    constexpr const T* data() const noexcept { return data_; }
};

static_assert(std::is_trivially_copyable_v<Span2D<int>>);
