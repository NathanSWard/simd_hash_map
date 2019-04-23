//Copyright Nathan Ward 2019..
//Distributed under the Boost Software License, Version 1.0.
//(See http://www.boost.org/LICENSE_1_0.txt)

#ifndef METADATA_HPP
#define METADATA_HPP

using metadata = signed char;

enum md_states : metadata
{
  mdEmpty = -128, //0b10000000
  mdDeleted = -2, //0b11111110
};

static inline bool isFull(const metadata md)           {return md >= 0;}
static inline bool isEmpty(const metadata md)          {return md == md_states::mdEmpty;}
static inline bool isDeleted(const metadata md)        {return md == md_states::mdDeleted;}

#endif
