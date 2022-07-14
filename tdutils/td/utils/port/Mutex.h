#include <mutex>

namespace td {
class Mutex {
 public:
  struct Guard {
    std::unique_lock<std::mutex> guard;
    void reset() {
      guard.unlock();
    }
  };

  Guard lock() {
    return {std::unique_lock<std::mutex>(mutex_)};
  }

 private:
  std::mutex mutex_;
};
}  // namespace td
