# simd_hash_map

## REQUIREMENTS TO BUILD: ##
* c++17
* Intel Processor with SSE2, SSE3, AVX2, or AVX512 instruction support
* #include "path-to/simd_hash_table.hpp"
* Example Build:
  * clang++ my_file.cpp -std=c++17 -mavx2
* See the following for optional x86 compiler flags
  * <https://gcc.gnu.org/onlinedocs/gcc/Option-Summary.html>
  * <https://clang.llvm.org/docs/ClangCommandLineReference.html#x86>

## API as of 22 April 2019 ##
* Template Parameter
 * 1)key_type 2)mapped_type 3)hash_function 4)key_equal_function
 * Note:
   * only key_type and mapped_type are necessary
   * value_type is always `std::pair<const key_type, mapped_type>`
* Member Functions
 * `insert(value_type value)`
   * return : iterator to inserted value
   * insert value into the map
   * will replace a value if key is already present
 * `erase(const key_type& key)`
   * return : bool if successfully deleted
   * erase key/value pair with given value
 * `find(const key_type& key)`
   * return : `std::optional<iterator>`
   * find key/value pair with given key
 * clear()
   * return : void
   * clear and reset the map

## Tests##
To run tests, compile as follow inside the tests/ director

gcc tests-main.cpp <insert test file here>


