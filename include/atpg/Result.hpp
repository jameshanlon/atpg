#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace atpg {

/// An error message produced by a failed operation.
class Error {
public:
  explicit Error(std::string message) : message_(std::move(message)) {}

  const std::string& message() const { return message_; }

private:
  std::string message_;
};

/// The outcome of an operation that either succeeds with a value of type T
/// or fails with an Error. Used throughout atpg instead of exceptions.
template <typename T>
class [[nodiscard]] Result {
public:
  Result(T value) : value_(std::move(value)) {}
  Result(Error error) : value_(std::move(error)) {}

  bool ok() const { return std::holds_alternative<T>(value_); }
  explicit operator bool() const { return ok(); }

  /// Only valid to call when ok() is true.
  const T& value() const { return std::get<T>(value_); }
  /// Only valid to call when ok() is true.
  T& value() { return std::get<T>(value_); }

  /// Only valid to call when ok() is false.
  const std::string& error() const { return std::get<Error>(value_).message(); }

private:
  std::variant<T, Error> value_;
};

/// The outcome of an operation that either succeeds with no value or fails
/// with an Error.
class [[nodiscard]] Status {
public:
  Status() = default;
  Status(Error error) : error_(std::move(error)) {}

  bool ok() const { return !error_.has_value(); }
  explicit operator bool() const { return ok(); }

  /// Only valid to call when ok() is false.
  const std::string& error() const { return error_->message(); }

private:
  std::optional<Error> error_;
};

} // namespace atpg

#define ATPG_CONCAT_INNER(a, b) a##b
#define ATPG_CONCAT(a, b) ATPG_CONCAT_INNER(a, b)

/// Evaluates a Status/Result-returning expression and, if it failed,
/// propagates the error by returning from the current function (whose
/// return type must itself be Status- or Result-constructible from Error).
#define ATPG_RETURN_IF_ERROR(expr)                                                               \
  do {                                                                                            \
    auto ATPG_CONCAT(atpgStatus, __LINE__) = (expr);                                              \
    if (!ATPG_CONCAT(atpgStatus, __LINE__).ok()) {                                                \
      return ::atpg::Error(ATPG_CONCAT(atpgStatus, __LINE__).error());                            \
    }                                                                                             \
  } while (false)

/// Evaluates a Result<T>-returning expression, propagating its error (as
/// with ATPG_RETURN_IF_ERROR) or otherwise initializing `decl` from its
/// value, e.g. `ATPG_ASSIGN_OR_RETURN(auto width, getWidth());`.
#define ATPG_ASSIGN_OR_RETURN(decl, expr)                                                         \
  auto ATPG_CONCAT(atpgResult, __LINE__) = (expr);                                                \
  if (!ATPG_CONCAT(atpgResult, __LINE__).ok()) {                                                  \
    return ::atpg::Error(ATPG_CONCAT(atpgResult, __LINE__).error());                              \
  }                                                                                                \
  decl = std::move(ATPG_CONCAT(atpgResult, __LINE__).value())
