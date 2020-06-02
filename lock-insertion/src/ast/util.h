#include <string>
#include <vector>

// When trying to add a new type of lock,
// one should manually appending lock and
// the corresponding string here.
#define X(attr, str) str
#define LOCK_LIST              \
  X(ttas, "TTAS"),             \
  X(pthreadmtx, "PTHREADMTX"),           \
  X(spinlock, "SPINLOCK")

std::string getLockPattern() {
  std::vector<std::string> locks { LOCK_LIST };
  std::string pattern("[^\\w]*(");
  for (auto l: locks) {
    pattern += l;
    pattern += "|";
  }
  pattern.erase(pattern.end() - 1);
  pattern += ")";
  return pattern;
}

