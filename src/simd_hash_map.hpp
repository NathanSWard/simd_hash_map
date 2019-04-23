#include "simd_metadata.hpp"

#include <utility>
#include <functional>
#include <iterator>
#include <limits>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <memory>

//REMOVE THESE
#include <bitset>
#include <iostream>

static constexpr std::size_t get_next_cap(std::size_t cap_minus_one) noexcept
{
  if (cap_minus_one)
    return (++cap_minus_one << 1); 
  return 128;
}

template<typename T, std::size_t Size>
struct bucket_group
{
  alignas(Size) metadata md_[Size];
  T kv_[Size];
  
  static auto empty_group() noexcept
  {
    static /*constexpr*/ metadata empty_md[] { 
      mdSentry, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, 
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty,
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty,
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty,
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty,
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty,
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty,
      mdEmpty,  mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty, mdEmpty
    };
    return reinterpret_cast<bucket_group*>(&empty_md);
  }

  constexpr void reset_metadata() noexcept 
  {
    //*md_ = md_states::mdSentry;
    for (auto i{0}; i < Size; ++i)
      md_[i] = md_states::mdEmpty;
  } 
};

template<typename Key,
         typename T,
         typename Hash,
         typename KeyEqual, 
         std::size_t GroupSize,
         std::size_t InitNumGroups
> class simd_hash_base : private Hash, private KeyEqual/*, private Allocator*/
{
public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = std::pair<const Key, T>;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  //using allocator_type = Allocator;
  //using allocator_traits = std::allocator_traits<allocator_type>;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;

protected:
  using group_t = bucket_group<value_type, GroupSize>;
  using group_pointer = group_t*; 
  //using group_allocator = typename allocator_traits::template rebind_alloc<group_t>;
  //using group_pointer = typename group_allocator::pointer;
protected:
  class Iterator
  {
  public:
    using iterator_category = std::forward_iterator_tag;
    
    explicit constexpr Iterator(group_pointer group, size_type index) noexcept
      :index_{index}
      ,group_{group}
    {
    }

    constexpr Iterator operator++() noexcept 
    {
      do {
        if (index_ % GroupSize == 0)
         --group_;
        if (index_-- == 0) 
          break;
      } while (!isFull(group_->md_[index_ % GroupSize])); 
      return *this;
    }
    constexpr bool operator!=(const Iterator& rhs) const noexcept 
    {
      return (index_ != rhs.index_);
    } 
    constexpr bool operator==(const Iterator& rhs) const noexcept 
    {
      return (index_ != rhs.index_);
    }
    constexpr const value_type& operator*() const noexcept 
    {
      return group_->kv_[index_ % GroupSize];
    }
    constexpr value_type& operator*() noexcept 
    {
      return group_->kv_[index_ % GroupSize];
    }
    constexpr value_type* operator->() noexcept 
    {
      return group_->kv_ + index_ % GroupSize;
    }
    constexpr explicit operator bool() const noexcept 
    {
      return isFull(group_->md_[index_ % GroupSize]);
    } 
  
  protected:
    friend class Iterator;
    group_pointer group_{nullptr};
    size_type index_{std::numeric_limits<size_type>::max()};
  };

public:
  using iterator = Iterator;
  using const_iterator = const Iterator; //not correct, fix

  [[nodiscard]] constexpr iterator begin() noexcept
  {
    size_type cap = cap_minus_one_ ? cap_minus_one_ + 1 : 0;
    if(cap)
      return ++iterator{std::addressof(table_[(cap/GroupSize)]), cap};
    return end();
  }
  [[nodiscard]] constexpr iterator begin() const noexcept
  { 
    size_type cap = cap_minus_one_ ? cap_minus_one_ + 1 : 0;
    if(cap)
      return ++iterator{std::addressof(table_[(cap/GroupSize)]), cap};
    return end();
  }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept
  {
    return begin();
  }
  [[nodiscard]] constexpr iterator end() noexcept
  {
    return iterator{nullptr, std::numeric_limits<size_type>::max()};
  } 
  [[nodiscard]] constexpr iterator end() const noexcept
  {
    return iterator{nullptr, std::numeric_limits<size_type>::max()};
  }
  [[nodiscard]] constexpr const_iterator cend() noexcept
  {
    return end();
  }


  [[nodiscard]] constexpr bool empty() const noexcept 
  {
    return size_ == 0;
  }
  [[nodiscard]] constexpr size_type size() const noexcept 
  {
    return size_;
  }
  [[nodiscard]] constexpr size_type max_size() const noexcept 
  {
    return cap_minus_one_ + 1;
  }
  [[nodiscard]] constexpr float load_factor() const noexcept 
  {
    return size_ ? (cap_minus_one_ + 1)/(float)size_ : 0;
  }
  //constexpr const allocator_type& get_allocator() const noexcept 
  //{    
  //  return static_cast<const allocator_type&>(my_alloc());
  //}
  constexpr const key_equal& key_eq() const noexcept 
  {
    return static_cast<const key_equal&>(my_key_eq());
  } 
  constexpr const hasher& hash_function() const noexcept 
  {
    return static_cast<const hasher&>(my_hasher());
  }
 
  iterator insert(value_type value) noexcept
  {
    if (is_full()) {
      grow();
      return insert(value);
    } 
    
    auto& key = value.first;
    size_type hash{hash_key(key)};
    size_type table_index{calc_table_index(hash)};
    size_type group{calc_group(table_index)};
    size_type group_index{calc_group_index(table_index, group)};
    metadata partial_hash{calc_partial_hash(hash)};
    
    group_t& group_ref = table_[group];
    simd_metadata simd(group_ref.md_);
    auto bit_mask = simd.Match(partial_hash);
    if (!bit_mask) {
      group_ref.md_[group_index] = partial_hash;
      new(std::addressof(group_ref.kv_[group_index])) value_type(value);
      ++size_;
      
      
      std::cout << "================INSERT===========\n"; 
      std::cout << "hash: " << std::bitset<64>(hash) << '\n';
      std::cout << "table index: " << table_index << '\n';
      std::cout << "group: " << group << '\n';
      std::cout << "group index: " << group_index << '\n';      
      std::cout << "metadata: " << std::bitset<8>(group_ref.md_[group_index]) << '\n'; 
      std::cout << "key: " << group_ref.kv_[group_index].first << '\n';
      std::cout << "value: " << group_ref.kv_[group_index].second << '\n';  
      std::cout << "=================================\n"; 


      return iterator{&group_ref, table_index};
    }
    for (const auto& i : bit_mask) {
      if (compare_keys(group_ref.kv_[i].first, key)) {
        group_ref.kv_[i].~value_type();
        new(std::addressof(group_ref.kv_[i])) value_type(std::move(value));
        
        std::cout << "===============INSERT============\n"; 
        std::cout << "hash: " << std::bitset<64>(hash) << '\n';
        std::cout << "table index: " << (group * GroupSize + i) << '\n';      
        std::cout << "group: " << group << '\n';
        std::cout << "group index: " << i << '\n';      
        std::cout << "metadata: " << std::bitset<8>(group_ref.md_[i]) << '\n'; 
        std::cout << "key: " << group_ref.kv_[i].first << '\n';
        std::cout << "value: " << group_ref.kv_[i].second << '\n';  
        std::cout << "=================================\n"; 

        return iterator{std::addressof(group_ref), group * GroupSize + i};
      }
    }

    group_index = bit_mask.getFirstUnsetBit();
    if (group_index != -1) { 
      group_ref.md_[group_index] = partial_hash;
      new(std::addressof(group_ref.kv_[group_index])) value_type(std::move(value));
      ++size_;

      std::cout << "===============INSERT============\n"; 
      std::cout << "hash: " << std::bitset<64>(hash) << '\n';
      std::cout << "table index: " << (group * GroupSize + group_index) << '\n';      
      std::cout << "group: " << group << '\n';
      std::cout << "group index: " << group_index << '\n';      
      std::cout << "metadata: " << std::bitset<8>(group_ref.md_[group_index]) << '\n'; 
      std::cout << "key: " << group_ref.kv_[group_index].first << '\n';
      std::cout << "value: " << group_ref.kv_[group_index].second << '\n';  
      std::cout << "=================================\n"; 

      return iterator{std::addressof(group_ref), group * GroupSize + group_index};
    }

    grow();
    return insert(value);  
  }

  bool erase(const key_type& key) noexcept
  {
    if (empty()) {
      std::cout << "===========ERASE=FAILED==========\n";
      std::cout << "empty() == true\n";
      std::cout << "key: " << key << '\n';
      std::cout << "=================================\n"; 
      return false;
    }
         
    size_type hash{hash_key(key)};
    size_type table_index{calc_table_index(hash)};
    size_type group{calc_group(table_index)};
    metadata partial_hash{calc_partial_hash(hash)};

    group_t& group_ref = table_[group];
    simd_metadata simd(group_ref.md_);
    auto bit_mask = simd.Match(partial_hash);
     
    std::cout << "\nhash: " << std::bitset<64>(hash) << '\n';
    std::cout << "table index: " << table_index << '\n';      
    std::cout << "group: " << group << '\n';
    std::cout << "partial hash: " << std::bitset<8>(partial_hash) << '\n';
    
    if (!bit_mask) { 
      std::cout << "===========ERASE=FAILED==========\n";
      std::cout << "no matched partial hashes\n";
      std::cout << "key: " << key << '\n';
      std::cout << "=================================\n"; 
      return false;
    }
    
    for (const auto& i : bit_mask) {
        std::cout << "group index: " << i << ", key: " << group_ref.kv_[i].first << ", value: " << group_ref.kv_[i].second << '\n';
      if (compare_keys(group_ref.kv_[i].first, key)) {

        std::cout << "===========ERASE=================\n"; 
        std::cout << "metadata: " << std::bitset<8>(group_ref.md_[i]) << '\n'; 
        std::cout << "key: " << group_ref.kv_[i].first << '\n';
        std::cout << "value: " << group_ref.kv_[i].second << '\n';  
        std::cout << "=================================\n"; 

        group_ref.md_[i] = md_states::mdEmpty; //or just empty?
        group_ref.kv_[i].~value_type();
        --size_;
        return true;
      }
    }
    std::cout << "===========ERASE=FAILED==========\n";
    std::cout << "matched partial hashes, but no equal key\n";
    std::cout << "key: " << key << '\n';
    std::cout << "=================================\n"; 
    return false;
  }

  std::optional<iterator> find(const key_type& key) const noexcept
  { 
    if (empty()) {
      std::cout << "============FIND=FAILED==========\n";
      std::cout << "empty() == true\n";
      std::cout << "key: " << key << '\n';
      std::cout << "=================================\n"; 
      return std::nullopt;
    }
         
    size_type hash{hash_key(key)};
    size_type table_index{calc_table_index(hash)};
    size_type group{calc_group(table_index)};
    metadata partial_hash{calc_partial_hash(hash)};

    group_t& group_ref = table_[group];
    simd_metadata simd(group_ref.md_);
    auto bit_mask = simd.Match(partial_hash);
     
    std::cout << "\nhash: " << std::bitset<64>(hash) << '\n';
    std::cout << "table index: " << table_index << '\n';      
    std::cout << "group: " << group << '\n';
    std::cout << "partial hash: " << std::bitset<8>(partial_hash) << '\n';
    
    if (!bit_mask) { 
      std::cout << "===========FIND=FAILED==========\n";
      std::cout << "no matched partial hashes\n";
      std::cout << "key: " << key << '\n';
      std::cout << "=================================\n"; 
      return std::nullopt;
    }
    
    for (const auto& i : bit_mask) {
      if (compare_keys(group_ref.kv_[i].first, key)) {

        std::cout << "==========FIND=SUCCESS===========\n"; 
        std::cout << "metadata: " << std::bitset<8>(group_ref.md_[i]) << '\n'; 
        std::cout << "key: " << group_ref.kv_[i].first << '\n';
        std::cout << "value: " << group_ref.kv_[i].second << '\n';  
        std::cout << "=================================\n"; 

        return {iterator{std::addressof(group_ref), group * GroupSize + i}};
      }
    }
    std::cout << "============FIND=FAILED==========\n";
    std::cout << "matched partial hashes, but no equal key\n";
    std::cout << "key: " << key << '\n';
    std::cout << "=================================\n"; 
    return std::nullopt;
  }

  void clear() noexcept 
  {
    if (table_ == group_t::empty_group())
      return;
    auto num_groups{(cap_minus_one_ + 1) / GroupSize};
    for (auto ptr{table_}, end{table_ + num_groups}; ptr != end; ++ptr) {
      for (auto i{0}; i < GroupSize; ++i) {
        if (isFull(ptr->md_[i]))
          ptr->kv_[i].~value_type();
      }
    }
    deallocate_groups(table_);
    size_ = 0;
    cap_minus_one_ = 0;
    table_ = group_t::empty_group();
  }

protected:
  void grow() noexcept 
  {
    auto old_cap{get_next_cap(cap_minus_one_++)};
    auto num_groups{old_cap/GroupSize};
    auto memory{std::malloc(sizeof(group_t) * num_groups)};
    auto table_temp{reinterpret_cast<group_pointer>(memory)};
    for (auto ptr{table_temp}, end{table_temp + num_groups}; ptr != end; ++ptr)
      ptr->reset_metadata();

    std::swap(table_, table_temp);
    std::swap(cap_minus_one_, old_cap);
    --cap_minus_one_;
    size_ = 0;

    if (table_temp != group_t::empty_group()) {
      //old/cap/Groupsize = pointer beyond the last group since = 2 but only allowed table[0] & table[1]
      for (auto ptr{table_temp}, end{table_temp + old_cap/GroupSize}; ptr != end; ++ptr) {
        for (int i{0}; i < GroupSize; ++i) {
          if (isFull(ptr->md_[i])) {
            insert(std::move(ptr->kv_[i]));
            ptr->kv_[i].~value_type();
          }
        } 
      } 
      deallocate_groups(table_temp);
    }
  }

  constexpr void deallocate_groups(group_pointer ptr)
  {
    if (ptr == group_t::empty_group())
      return;
    std::free(ptr);       
  }

  //below should be [[nodiscard]], though they're internal 
  bool is_full() const noexcept
  {
    if (cap_minus_one_)
      return size_ == cap_minus_one_ + 1; 
    return true;
  }  
  template<typename K1, typename K2>
  constexpr bool compare_keys(const K1& key1, const K2& key2) const noexcept
  {
    return my_key_eq()(key1, key2);
  }
  template<typename K>
  constexpr size_type hash_key(const K& key) const noexcept
  {
    return my_hasher()(key);
  }
  constexpr metadata calc_partial_hash(size_type hash) const noexcept 
  {
    return (metadata)(hash & 0x7F);
  }
  constexpr size_type calc_group(size_type index) const noexcept
  {
    return index / GroupSize; 
  }
  constexpr size_type calc_table_index(size_type hash) const noexcept
  {
    return hash & cap_minus_one_;
  }    
  constexpr size_type calc_group_index(const size_type& index, const size_type& group) const noexcept
  {
    return (index - group*GroupSize);
  } 

  constexpr const hasher& my_hasher() const noexcept 
  {
    return std::get<0>(settings_);
  }
  constexpr const key_equal& my_key_eq() const noexcept
  {
    return std::get<1>(settings_);
  }
  //constexpr allocator_type& my_alloc() noexcept
  //{
  //  return std::get<2>(settings_);
  //}
public:
  ~simd_hash_base() noexcept
  {
    clear();
  }
protected:
  size_type size_{0};
  size_type cap_minus_one_{0}; 
  group_pointer table_{group_t::empty_group()};
  std::tuple<hasher, key_equal> settings_{hasher{}, key_equal{}};
};

template<typename Key,
         typename T,
         typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>
> class simd_hash_map : public simd_hash_base<Key, T, Hash, KeyEqual, simd_metadata::size, 4>
{
}; 








