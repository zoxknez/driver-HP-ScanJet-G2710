// Minimalni Result<T> - std::expected je C++23, a mi smo na C++20.
//
// Namerno bez izuzetaka: transport greske (timeout, STALL, disconnect) su
// ocekivani tok, ne izuzetne situacije, i moraju se obraditi na svakom mestu.

#pragma once

#include "Error.h"

#include <new>
#include <type_traits>
#include <utility>

namespace g2710 {

template <typename T>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result ne drzi reference");

public:
    using value_type = T;

    Result(T value) : ok_(true) { new (&storage_.value) T(std::move(value)); }
    Result(Error error) : ok_(false), error_(error) {}

    Result(const Result& other) : ok_(other.ok_), error_(other.error_) {
        if (ok_) {
            new (&storage_.value) T(other.storage_.value);
        }
    }

    Result(Result&& other) noexcept : ok_(other.ok_), error_(other.error_) {
        if (ok_) {
            new (&storage_.value) T(std::move(other.storage_.value));
        }
    }

    Result& operator=(Result other) noexcept {
        swapWith(other);
        return *this;
    }

    ~Result() {
        if (ok_) {
            storage_.value.~T();
        }
    }

    bool hasValue() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // Pozivati samo kada hasValue(); u suprotnom je ponasanje nedefinisano.
    T& value() & noexcept { return storage_.value; }
    const T& value() const& noexcept { return storage_.value; }
    T&& value() && noexcept { return std::move(storage_.value); }

    T valueOr(T fallback) const {
        return ok_ ? storage_.value : std::move(fallback);
    }

    const Error& error() const noexcept { return error_; }
    ErrorCode code() const noexcept { return ok_ ? ErrorCode::Ok : error_.code; }

private:
    void swapWith(Result& other) noexcept {
        Result tmp(std::move(other));
        other.~Result();
        new (&other) Result(std::move(*this));
        this->~Result();
        new (this) Result(std::move(tmp));
    }

    union Storage {
        Storage() {}
        ~Storage() {}
        T value;
        char none;
    } storage_;

    bool ok_;
    Error error_{};
};

// Specijalizacija za operacije bez povratne vrednosti.
template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;

    Result() : ok_(true) {}
    Result(Error error) : ok_(false), error_(error) {}

    bool hasValue() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Error& error() const noexcept { return error_; }
    ErrorCode code() const noexcept { return ok_ ? ErrorCode::Ok : error_.code; }

private:
    bool ok_;
    Error error_{};
};

using Status = Result<void>;

inline Status ok() noexcept { return Status{}; }

inline Error fail(ErrorCode code, const char* context, std::uint32_t win32 = 0) noexcept {
    return Error{code, win32, context};
}

}  // namespace g2710
