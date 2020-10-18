#include <string>
#include <vector>

#define LOCK_LIST "TTAS", "PTHREADMTX", "BACKOFF", "ADAPTIVEMTX", "MCS", "CBOMCS"

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

