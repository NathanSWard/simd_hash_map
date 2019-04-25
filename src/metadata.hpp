#ifndef METADATA_HPP
#define METADATA_HPP

using metadata = signed char;

enum md_states : metadata {
  mdEmpty = -128,  // 0b10000000
  mdDeleted = -2,  // 0b11111110
  mdSentry = -1,   // 0b11111111
};

static inline bool isFull(const metadata md) { return md >= 0; }
static inline bool isEmpty(const metadata md) {
  return md == md_states::mdEmpty;
}
static inline bool isDeleted(const metadata md) {
  return md == md_states::mdDeleted;
}
static inline bool isSentry(const metadata md) {
  return md == md_states::mdSentry;
}
static inline bool isEmptyOrDeleted(const metadata md) {
  return md < md_states::mdSentry;
}

#endif
