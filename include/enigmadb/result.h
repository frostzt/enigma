/*
 * result.h -- Result type
 *
 * Author: frostzt
 * Date: 2026-03-20
 */

#ifndef ENIGMA_DB_RESULT_H
#define ENIGMA_DB_RESULT_H

#include <stdexcept>
#include <variant>

namespace enigmadb {

template <typename T, typename E>
class [[nodiscard]] ExpectResult {
   private:
    std::variant<T, E> data_;

    ExpectResult(T data) : data_(std::move(data)) {};
    ExpectResult(E error) : data_(std::move(error)) {};

   public:
    bool has_value() const { return std::holds_alternative<T>(data_); };

    T& value() {
        if (auto* val = std::get_if<T>(&data_)) {
            return *val;
        }
        throw std::runtime_error("invalid access to data");
    }

    const T& value() const {
        if (auto* val = std::get_if<T>(&data_)) {
            return *val;
        }
        throw std::runtime_error("invalid access to data");
    }

    E& error() {
        if (auto* val = std::get_if<E>(&data_)) {
            return *val;
        }
        throw std::runtime_error("invalid access to error");
    }

    const E& error() const {
        if (auto* val = std::get_if<E>(&data_)) {
            return *val;
        }
        throw std::runtime_error("invalid access to error");
    }

    static ExpectResult ok(T value) {
        return ExpectResult<T, E>{std::move(value)};
    }

    static ExpectResult err(E error) {
        return ExpectResult<T, E>{std::move(error)};
    }
};

template <typename E>
class ExpectResult<void, E> {
   private:
    std::variant<std::monostate, E> data_;

    ExpectResult() : data_(std::monostate{}) {}
    ExpectResult(E err) : data_(std::move(err)) {}

   public:
    bool has_value() const {
        return std::holds_alternative<std::monostate>(data_);
    };

    void value() const {
        if (!has_value()) {
            throw std::runtime_error("invalid access to data");
        }
    }

    E& error() { return std::get<E>(data_); }

    const E& error() const { return std::get<E>(data_); }

    static ExpectResult ok() { return ExpectResult(); }

    static ExpectResult err(E error) { return ExpectResult(std::move(error)); }
};

}  // namespace enigmadb

#endif  // ENIGMA_DB_RESULT_H
