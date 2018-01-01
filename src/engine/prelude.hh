// -*- mode: c++ -*-
#ifndef __IMP_PRELUDE__56107413
#define __IMP_PRELUDE__56107413

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <cstdint>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <chrono>

// Include standard utilities
#include "utility/types.hh"
#include "utility/array_view.hh"
#include "utility/string_view.hh"
#include "utility/guard.hh"
#include "utility/optional.hh"
#include "imp/detail/Memory.hh"

#define STUB(msg) fmt::print("STUB {}: {} ({}:{})\n", __FUNCTION__, msg, __LINE__, __FILE__)

extern void (*__imp_error)(const std::string&);
namespace fmt {
  inline void println(CStringRef fmt, ArgList args)
  {
      print(fmt, args);
      print("\n");
  }

  inline void println(std::FILE* fh, CStringRef fmt, ArgList args)
  {
      print(fh, fmt, args);
      print(fh, "\n");
  }

  inline void fatal(CStringRef fmt, ArgList args)
  {
      if (__imp_error) {
          __imp_error(format(fmt, args));
      } else {
          println(stderr, fmt, args);
      }
      std::exit(1);
  }

  FMT_VARIADIC(void, println, CStringRef)
  FMT_VARIADIC(void, println, std::FILE*, CStringRef)
  FMT_VARIADIC(void, fatal, CStringRef)

  template <class T>
  inline void print(const T &x)
  {
      print("{}", x);
  }

  template <class T>
  inline void println(const T &x)
  {
      println("{}", x);
  }
}

namespace imp {
  struct dummy_t {
      template <class T>
      operator T() const
      { return {}; }
  };

  struct UserData {
      virtual ~UserData() {}
  };

  /*! Pad an integer so it's divisible by N */
  template <size_t N>
  constexpr size_t pad(size_t x)
  { return x + ((N - (x & (N - 1))) & (N - 1)); }

  using String = std::string;
  template <class T, class Deleter = std::default_delete<T>>
  using Vector = std::vector<T>;
  template <class T>
  using InitList = std::initializer_list<T>;
  template <class T, size_t N>
  using Array = std::array<T, N>;

  inline std::ostream& operator<<(std::ostream& s, StringView sv)
  {
      return s.write(sv.data(), sv.length());
  }

  class Stopwatch {
      using clock = std::chrono::steady_clock;
      std::string message_;
      clock::time_point start_;

  public:
      Stopwatch(const std::string &message):
          message_(message),
          start_(std::chrono::steady_clock::now()) {}

      ~Stopwatch()
      {
          using namespace std::chrono_literals;
          auto timediff = (clock::now() - start_) / 1ms;
          fmt::println("{}: {}ms", message_, timediff);
      }
  };

#define STOPWATCH Stopwatch _stopwatch_##__COUNTER__(__PRETTY_FUNCTION__);
}

template <class T, class Deleter>
inline std::ostream& operator<<(std::ostream& s, const std::unique_ptr<T, Deleter>& ptr)
{
    return s << static_cast<void*>(ptr.get());
}

template <class T>
inline std::ostream& operator<<(std::ostream& s, const std::shared_ptr<T>& ptr)
{
    return s << static_cast<void*>(ptr.get());
}

template <class T>
inline std::ostream& operator<<(std::ostream& s, const std::weak_ptr<T>& ptr)
{
    if (ptr.expired()) {
        return s << "(expired)";
    } else {
        auto shared = ptr.lock();
        return s << shared;
    }
}

#ifndef IMP_DONT_POLLUTE_GLOBAL_NAMESPACE
using namespace std::string_literals;
using namespace std::chrono_literals;
using namespace imp;

// clang complains about ambiguity with stuff like sprintf and fprintf, since there exist functions with those names
// in both the std and fmt namespaces, so we only forward the few functions we actually need, instead of
// using namespace imp;

template <class... Args>
inline auto print(Args&&... args)
{ return fmt::print(std::forward<Args>(args)...); }

template <class... Args>
inline auto println(Args&&... args)
{ return fmt::println(std::forward<Args>(args)...); }

template <class... Args>
inline auto format(Args&&... args)
{ return fmt::format(std::forward<Args>(args)...); }

template <class... Args>
inline auto fatal(Args&&... args)
{ return fmt::fatal(std::forward<Args>(args)...); }

#endif

#endif //__IMP_PRELUDE__56107413