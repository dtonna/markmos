#pragma once

#include <type_traits>
#include <utility>

// Expected<T, E> — minimal fallback when std::expected not available
// @cache_reason Small POD-like, no heap alloc, matches std::expected API
// @fallback Controlled by ENGINE_USE_STD_EXPECTED macro
// @zero_virtual No inheritance, no virtual dispatch

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && !defined(ENGINE_FORCE_LOCAL_EXPECTED)
#    define ENGINE_USE_STD_EXPECTED 1
#else
#    include <cstdint>
#    include <memory>
#    include <new>
#    define ENGINE_USE_STD_EXPECTED 0
#endif

#if ENGINE_USE_STD_EXPECTED
#    include <expected>
template <typename T, typename E> using Expected = std::expected<T, E>;

template <typename E> using Unexpected           = std::unexpected<E>;

template <typename E> constexpr Unexpected<std::decay_t<E>> make_unexpected(E &&e) noexcept { return Unexpected<std::decay_t<E>>(std::forward<E>(e)); }
#else
template <typename E> struct Unexpected {
    E error;
    constexpr explicit Unexpected(E &&e) noexcept : error(std::move(e)) {}
    constexpr explicit Unexpected(const E &e) noexcept : error(e) {}
};

template <typename E> constexpr Unexpected<std::decay_t<E>> make_unexpected(E &&e) noexcept { return Unexpected<std::decay_t<E>>(std::forward<E>(e)); }

template <typename T, typename E> class Expected {
    static_assert(!std::is_reference_v<T>, "Expected does not support references");
    static_assert(!std::is_reference_v<E>, "Expected does not support reference errors");

    union Storage {
        T value;
        E error;
        Storage() noexcept {}
        ~Storage() noexcept {}
    } storage_;
    bool           has_value_;

    constexpr void destroy() noexcept {
        if (has_value_) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                storage_.value.~T();
            }
        } else {
            if constexpr (!std::is_trivially_destructible_v<E>) {
                storage_.error.~E();
            }
        }
    }

  public:
    constexpr Expected(T &&val) noexcept(std::is_nothrow_move_constructible_v<T>) : has_value_(true) { std::construct_at(&storage_.value, std::move(val)); }

    constexpr Expected(const T &val) noexcept(std::is_nothrow_copy_constructible_v<T>) : has_value_(true) { std::construct_at(&storage_.value, val); }

    constexpr Expected(E &&err) noexcept(std::is_nothrow_move_constructible_v<E>) : has_value_(false) { std::construct_at(&storage_.error, std::move(err)); }

    constexpr Expected(const E &err) noexcept(std::is_nothrow_copy_constructible_v<E>) : has_value_(false) { std::construct_at(&storage_.error, err); }

    constexpr Expected(Unexpected<E> unexp) noexcept(std::is_nothrow_move_constructible_v<E>) : has_value_(false) {
        std::construct_at(&storage_.error, std::move(unexp.error));
    }

    constexpr Expected(Expected &&other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            std::construct_at(&storage_.value, std::move(other.storage_.value));
        } else {
            std::construct_at(&storage_.error, std::move(other.storage_.error));
        }
    }

    constexpr Expected(const Expected &other) noexcept : has_value_(other.has_value_) {
        if (has_value_) {
            std::construct_at(&storage_.value, other.storage_.value);
        } else {
            std::construct_at(&storage_.error, other.storage_.error);
        }
    }

    ~Expected() noexcept { destroy(); }

    constexpr bool                    has_value() const noexcept { return has_value_; }
    constexpr explicit                operator bool() const noexcept { return has_value_; }

    constexpr T                      &operator*() noexcept { return storage_.value; }
    constexpr const T                &operator*() const noexcept { return storage_.value; }

    constexpr T                      *operator->() noexcept { return &storage_.value; }
    constexpr const T                *operator->() const noexcept { return &storage_.value; }

    constexpr E                      &error() noexcept { return storage_.error; }
    constexpr const E                &error() const noexcept { return storage_.error; }

    template <typename U> constexpr T value_or(U &&default_val) const & { return has_value_ ? storage_.value : static_cast<T>(std::forward<U>(default_val)); }
};

// Void specialization
template <typename E> class Expected<void, E> {
    static_assert(!std::is_reference_v<E>, "Expected<void, E> does not support reference errors");

    union Storage {
        E error;
        Storage() noexcept : error{} {}
        ~Storage() noexcept {}
    } storage_;
    bool has_value_;

  public:
    constexpr Expected() noexcept : storage_{}, has_value_(true) {}

    constexpr Expected(E &&err) noexcept(std::is_nothrow_move_constructible_v<E>) : has_value_(false) { std::construct_at(&storage_.error, std::move(err)); }

    constexpr Expected(const E &err) noexcept(std::is_nothrow_copy_constructible_v<E>) : has_value_(false) { std::construct_at(&storage_.error, err); }

    constexpr Expected(Unexpected<E> unexp) noexcept(std::is_nothrow_move_constructible_v<E>) : has_value_(false) {
        std::construct_at(&storage_.error, std::move(unexp.error));
    }

    ~Expected() noexcept {
        if (!has_value_ && !std::is_trivially_destructible_v<E>) {
            storage_.error.~E();
        }
    }

    constexpr bool     has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }

    constexpr E       &error() noexcept { return storage_.error; }
    constexpr const E &error() const noexcept { return storage_.error; }
};
#endif

// LightweightErrorView concept
template <typename V, typename E>
concept LightweightErrorView = std::is_trivially_copyable_v<std::remove_cvref_t<V>> && requires(const std::remove_cvref_t<V> &v) {
    { static_cast<E>(v) } -> std::same_as<E>;
};

// ErrorReturnLike — error() can return E, const E&, or LightweightErrorView
template <typename ER, typename E>
concept ErrorReturnLike = std::same_as<std::remove_cvref_t<ER>, E> || std::same_as<std::remove_cvref_t<ER>, const E> ||
                          std::same_as<std::remove_cvref_t<ER>, const E &> || LightweightErrorView<ER, E>;

// ExpectedLike<R, T, E> — concept for expected types
// MUST NOT require R to be default-constructible
template <typename R, typename T, typename E>
concept ExpectedLike = requires(const R &cr) {
    { cr.has_value() } -> std::same_as<bool>;
    requires ErrorReturnLike<decltype(cr.error()), E>;
} && (std::is_void_v<T> || requires(R &r) {
                           { *r } -> std::same_as<T &>;
                       });
