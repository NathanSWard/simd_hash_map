#ifndef SIMD_HASH_MAP_HPP
#define SIMD_HASH_MAP_HPP

#include <utility>
#include <functional>
#include <optional>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <memory>
#include <cstdint>
#include <tuple>

#include <iostream>


#include <immintrin.h>

#include "BitMaskIter.hpp"

/*NOTES:
 *perhaps have std::optional<Iterator> returned?
 *   - if so, have Iterator inherit from std::reference_wrapper?
 *
 * */

#include "simd_metadata.hpp"  

template<typename T>
struct mdkv_group 
{
  mdkv_group()
    :md_{*EmptyGroup<metadata>()}
  {
  }

  constexpr bool hasSentry() const noexcept {
    return isSentry(md_[0]); 
  }

  constexpr void reset_metadata() noexcept {
    md_ = EmptyGroup<metadata>();
  }

  metadata md_[simd_metadata::size];
  T kv_[simd_metadata::size];
};

template<typename Key, 
         typename T, 
         typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<std::pair<const Key, T>>
> class simd_hash_table
{
public: /* Member Types */

  using key_type = Key;
  using mapped_type = T;
  using value_type = std::pair<const Key, T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using allocator_byte = typename std::allocator<char>;
  using allocator_type = Allocator;
  using allocator_traits = std::allocator_traits<allocator_byte>;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = typename allocator_traits::pointer;
  using const_pointer = typename allocator_traits::const_pointer;
  
  using ref_wrap = std::reference_wrapper<value_type>;
  using opt_ref = std::optional<ref_wrap>;
  using const_ref_wrap = std::reference_wrapper<const value_type>;
  using const_opt_ref = std::optional<const_ref_wrap>;
  using group_t = mdkv_group<value_type>;
  using group_pointer = group_t*;

public: /* Public Internal Classes and Structs*/
  template<typename U> 
  class Iterator 
  {
  public: /* Iterator Member Types */
    using iterator_category = std::forward_iterator_tag;
    using value_type = U;
    using difference_type = std::ptrdiff_t;
    using pointer = U*;
    using reference = U&;

  public: /* Iterator Member Functions */
    explicit constexpr Iterator(group_pointer group, size_type index) noexcept
      :index_{index}
      ,group_{group}
    {
    } 
    constexpr Iterator operator++() noexcept {
      do {
        if (index_ % simd_metadata::size == 0)
          --group_; 
        if (index_-- == 0)
          break; 
      } 
      while (isEmptyOrDeleted(group_->md_[index_]));
      return *this;
    }
    constexpr bool operator!=(const Iterator& other) const noexcept {
      return ((index_ != other.index_)&&(group_ != other.group_));
    }
    constexpr bool operator==(const Iterator& other) const noexcept {
      return ((index_ == other.index_)&&(group_ == other.group_));
    }
    constexpr value_type& operator*() noexcept {
      return group_->kv_[index_];
    }
    constexpr const value_type& operator*() const noexcept {
      return group_->kv_[index_];
    }
    constexpr explicit operator bool() const noexcept {
      return !isEmptyOrDeleted(group_->md_[index_]);
    } 
  
  private: /* Iterator Member Variables */
    friend class Iterator;
    size_type index_{0};
    group_pointer group_;
  }; //Iterator

public: /* Member Types */
  
  using iterator = Iterator<value_type>;
  using const_iterator = Iterator<const value_type>;

public: /* Public Member Functions */
  
  constexpr const hasher& hash_function() const noexcept {return std::get<0>(settings_);}
  constexpr const key_equal& key_eq() const noexcept {return std::get<1>(settings_);}
  constexpr const allocator_type& get_allocator() const noexcept {return std::get<2>(settings_);}

  explicit constexpr simd_hash_table() noexcept
    :cap_minus_one_{4 * simd_metadata::size}
  {
    char* memory = allocator_traits::allocate(get_alloc(), calc_total_memory(cap_minus_one_--));
    table_ = reinterpret_cast<group_pointer>(++memory);
  }

  explicit constexpr simd_hash_table(size_type capacity) noexcept
    :cap_minus_one_{((capacity + (simd_metadata::size - 1)) & ~(simd_metadata::size - 1))}
  { 
    char* memory = allocator_traits::allocate(get_alloc(), calc_total_memory(cap_minus_one_--));
    table_ = reinterpret_cast<group_pointer>(++memory);
  }

private:
  constexpr allocator_byte& get_alloc() noexcept {return std::get<2>(settings_);}

  constexpr size_type calc_total_memory(size_type num_groups) {
    return num_groups * sizeof(group_t) + sizeof(void*);
  }
public:
/*
  //template<typename K, typename ...Args>
  template<typename Args>
  opt_ref emplace(Args&& args)  {
    std::cout << "emplace_impl(Args&&... args)\n";
    if (is_full()) {
      grow();
      return emplace(std::forward<Args>(args));
    } 
    auto& key = args.first;

    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group].md_.data());
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      table_[group].md_[group_index] = partial_hash;
      table_[group].kv_[group_index] = std::forward<value_type>(args); 
      ++size_;
      return{std::ref(table_[group].kv_[group_index])};
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i].first == key) {
        table_[group].kv_[i].second = std::forward<mapped_type>(args.second); 
        return{std::ref(table_[group].kv_[group_index])}; 
      }
    }

    auto empty_index = bit_mask.getFirstUnsetBit();
    if (empty_index != -1) {
      table_[group].md_[group_index] = partial_hash;
      table_[group].kv_[group_index] = std::forward<value_type>(args);
      ++size_;
      return{std::ref(table_[group].kv_[group_index])}; 
    }
    grow();
    return emplace(std::forward<Args>(args)); 
  }
  
  template<typename K, typename ...Args>
  opt_ref try_emplace(K&& key, Args&& ...args)  { 
    if (is_full()) {
      grow();
      return try_emplace(std::forward<Args>(args)...);
    }
    auto new_value = value_type{std::forward<K>(key), std::forward<Args>(args)...};

    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group].md_.data());
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      table_[group].md_[group_index] = partial_hash;
      table_[group].kv_[group_index] = std::forward<value_type>(new_value);
      ++size_;
      return{std::ref(table_[group].kv_[group_index])};
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i].first == key) {
        return {std::ref(table_[group].kv_[i])}; 
      }
    }

    auto empty_index = bit_mask.getFirstUnsetBit();
    if (empty_index != -1) {
      table_[group].md_[group_index] = partial_hash;
      table_[group].kv_[group_index] = std::forward<value_type>(new_value);
      ++size_;
      return{std::ref(table_[group].kv_[group_index])}; 
    }
    grow();
    return emplace(std::forward<Args>(args)...); 
  }
 
  template<typename P, std::enable_if<std::is_constructible<P&&, value_type>::value>>
  opt_ref insert(P&& value) noexcept {
    std::cout << "insert(P&& value)\n";
    return emplace(std::forward<P>(value));
  } 
*/
  opt_ref insert(value_type&& value) noexcept { 
    std::cout << "insert(value_type&& value)\n"; 
    if (is_full()) {
      grow();
      return insert(std::move(value));
    } 
    auto& key = value.first;

    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group].md_);
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      table_[group].md_[group_index] = partial_hash;
      allocator_traits::construct (
          get_alloc(), std::addressof(table_[group].kv_[group_index]), std::move(value)
        ); 
      ++size_;
      return{std::ref(table_[group].kv_[group_index])};
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i].first == key) {
        allocator_traits::destroy(get_alloc(), std::addressof(table_[group].kv_[i]));
        allocator_traits::construct (
          get_alloc(), std::addressof(table_[group].kv_[i]), std::move(value)
        );  
        return{std::ref(table_[group].kv_[group_index])}; 
      }
    }

    auto empty_index = bit_mask.getFirstUnsetBit();
    if (empty_index != -1) {
      allocator_traits::construct (
        get_alloc(), std::addressof(table_[group].kv_[group_index]), std::move(value)
      ); 
      ++size_;
      return{std::ref(table_[group].kv_[group_index])}; 
    }
    grow();
    return insert(std::move(value)); 
  }

  opt_ref insert(const value_type& value) noexcept {
    std::cout << "insert(const value_type& value)\n";  
    return insert(std::move(value_type{value}));
  }

  void insert(std::initializer_list<value_type> ilist) noexcept {
    std::cout << "insert(std::initializer_list<value_type> ilist)\n";
    for (auto&& value : ilist) {
      insert(std::forward<value_type>(value));
    } 
  }

/*
  template<typename K, typename M>
  opt_ref insert_or_assign(K&& key, M&& mapped) noexcept { 
    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group]);
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      table_[group].md_[group_index] = partial_hash;
      table_[group].kv_[group_index] = std::forward<value_type>({std::forward<K>(key), std::forward<M>(mapped)});
      ++size_;
      return{std::ref({table_[group].kv_[group_index]})};
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i] == key) {
        table_[group].kv_[i].second = std::forward<M>(mapped);
        return {std::ref(table_[group].kv_[i])}; 
      }
    }

    auto empty_index = bit_mask.getFirstUnsetBit();
    if (empty_index != -1) {
      table_[group].md_[group_index] = partial_hash;
      table_[group].kv_[group_index] = std::forward<value_type>({std::forward<K>(key), std::forward<M>(mapped)});
      ++size_;
      return{std::ref(table_[group].kv_[group_index])}; 
    }
    grow();
    return emplace(std::forward<K>(key), std::forward<M>(mapped));
  }
  

  template<typename M> 
  opt_ref insert_or_assign(const key_type& key, M&& mapped) noexcept { 
    return insert_or_assing(key, std::forward<M>(mapped)); 
  } 
  
  template<typename M>
  opt_ref insert_or_assign(key_type&& key, M&& mapped) noexcept { 
    return insert_or_assign(std::move(key), std::forward<M>(mapped)); 
  }
*/
/* 
  [[nodiscard]] opt_ref find(const key_type& key) noexcept { 
    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group]);
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      return std::nullopt;
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i].first == key) {
        return {std::ref(table_[group].kv_[i])}; 
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] constexpr const_opt_ref find(const key_type& key) const noexcept {
    return static_cast<const_opt_ref>(find(key));
  }

  [[nodiscard]] constexpr T& operator[](const key_type& key) noexcept;

  [[nodiscard]] constexpr T& operator[](key_type&& key) noexcept; {
    return try_emplace(std::move(key))->get().second;
  }

   Hash::transparent_key_equal
  template<typename K, 
    typename std::enable_if<std::is_convertible<K, key_type>::value>>
  [[nodiscard]] opt_ref find(K&& key) noexcept {
    return find(std::forward<key_type>(static_cast<key_type>(key)));
  }
  */
/*
  [[nodiscard]] bool contains(const key_type& key) const noexcept { 
    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group]);
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      return false;
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i].first == key) {
        return true; 
      }
    }
    return false;
  }

  
  bool erase(const key_type& key) noexcept { 
    auto hash{calc_hash(key)};
    auto index{calc_index(hash)};
    auto group{calc_group(index)};
    auto group_index{calc_group_index(group, index)};
    auto partial_hash{calc_partial_hash(hash)};
    //auto group_ref = table_[group]; //necessary?
    simd_metadata simd(table_[group]);
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      return false;
    }
    //check for same keys
    for (const auto& i : bit_mask) {
      if (table_[group].kv_[i].first == key) {
        table_[group].md_[i] = md_states::mdDeleted;
        allocator_traits::destroy(allocator_type{}, std::addressof(table_[group].kv_[i]));
        return true; 
      }
    }
    return false;
  }

  constexpr bool erase(const opt_ref& value) noexcept {
    if (value) {
      return erase(value->get().first);
    }
    return false;
  }

  constexpr bool erase(const const_opt_ref& value) noexcept {
    if (value) { 
      return erase(value->get().first);
    }
    return false;
  }

*/
/*
  constexpr void clear() noexcept {
    for (auto i{0}, num_groups{(cap_minus_one_ + 1) / simd_metadata::size}; i < num_groups; ++i) {
      for (auto j{0}; j < simd_metadata::size; ++j) {
        if (isFull(table_[i].md_[j])) {
          allocator_traits::destroy(get_alloc(), std::addressof(table_[i].kv_[i])); 
        }
      } 
      table_ = EmptyGroup<group_t>();
    }
    size_ = 0;
  }
*/
  [[nodiscard]] constexpr bool empty() const noexcept {
    return size_ == 0;
  }

  [[nodiscard]] constexpr size_type size() const noexcept {
    return size_;
  }

  [[nodiscard]] constexpr size_type max_size() const noexcept {
    return cap_minus_one_ + 1;
  }

  [[nodiscard]] constexpr float load_factor() const noexcept {
    return size_ ? ((cap_minus_one_ + 1) / (float)size_) : 0;
  }
  
  [[nodiscard]] constexpr iterator begin() noexcept {
    size_type cap = cap_minus_one_ ? cap_minus_one_ + 1 : 0;
    return ++iterator{&(table_[cap/simd_metadata::size]), cap};
  }

  [[nodiscard]] constexpr iterator begin() const noexcept { 
    size_type cap = cap_minus_one_ ? cap_minus_one_ + 1 : 0;
    return ++iterator{&(table_[cap/simd_metadata::size]), cap};
  }

  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    return begin();
  }

  [[nodiscard]] constexpr iterator end() noexcept {
    return {table_ - 1, std::numeric_limits<size_type>::max()}; 
  }

  [[nodiscard]] constexpr iterator end() const noexcept {
    return {table_ - 1, std::numeric_limits<size_type>::max()}; 
    //maybe (char*)table_ - 1;
  }

  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    return end();
  }

private: /* Private Member Functions */
  
  constexpr void grow() noexcept {
    auto old_cap{(++cap_minus_one_) << 1};
    auto temp = allocator_traits::allocate(get_alloc(), calc_total_memory(old_cap));
    temp = (char*)temp + 1;
    group_pointer old_table = reinterpret_cast<group_pointer>(temp);
    std::swap(table_, old_table);
    std::swap(cap_minus_one_, old_cap); 
    for (size_type i{0}, group_size{old_cap / 64}; i < group_size; ++i) {
      for (auto j{0}; j < simd_metadata::size; ++j) {
        if (isFull(old_table[i].md_[j])) {
          insert(std::move(old_table[i].kv_[i]));
         //////////////REPLACE WITH EMPLACE 
        }
      }
    }
    --cap_minus_one_;
   /////////////////////////////////CALL DESTROY/DEALLOCATE FOR OLD_TABLE!!!!! MEM LEAK!!!! 
  }
 
  [[nodiscard]] constexpr bool is_full() const noexcept {
    return (size_ == (cap_minus_one_ + 1));
  }

  constexpr size_type calc_hash(const key_type& key) const noexcept 
    {return hasher{}(key);}
  constexpr metadata calc_partial_hash(size_type hash) const noexcept
    {return (metadata)(hash & 0x7F);}
  constexpr size_type calc_index(size_type hashed_key) const noexcept 
    {return hashed_key & cap_minus_one_;}
  constexpr size_type calc_group(size_type index) const noexcept 
    {return index / simd_metadata::size;};
  constexpr int8_t calc_group_index(const size_type& group, const size_type& index) const noexcept 
    {return (int8_t)(index - (group * simd_metadata::size));}

  constexpr hasher& hash_function() noexcept {return std::get<0>(settings_);}
  constexpr key_equal& key_eq() noexcept {return std::get<1>(settings_);}
  constexpr allocator_type& get_allocator() noexcept {return std::get<2>(settings_);}

private: /* Private Member Variables */
  
  size_type size_{0};
  size_type cap_minus_one_{simd_metadata::size * 4 - 1};
  group_pointer table_{EmptyGroup<group_t>()};

  std::tuple<hasher, key_equal, allocator_byte> settings_{hasher{}, key_equal{}, allocator_byte{allocator_type{}}};
}; //simd_hash_table

#endif
