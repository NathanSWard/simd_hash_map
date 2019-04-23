#include "simd_hash_map.hpp"
#include <iostream>
int main()
{
  simd_hash_map<int, int> table;
  using type = std::pair<int, int>;
  type p{3,4}; 
  table.insert(p);
  table.insert(std::pair<int, int>{5,6});
  

  auto found = table.find(5);
  std::cout << "key: " << found.value()->first << ", value: " << found.value()->second << '\n';
  found = table.find(10);
  if (!found)
   std::cout << "success!!!\n"; 
  return 0;
}
