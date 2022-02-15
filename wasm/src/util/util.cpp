#include <fstream>
#include <sstream>
#include <string>

std::string slurp(const char *filename) {
  std::ifstream in;
  in.open(filename, std::ifstream::in | std::ifstream::binary);
  std::stringstream sstr;
  sstr << in.rdbuf();
  in.close();
  return sstr.str();
}
