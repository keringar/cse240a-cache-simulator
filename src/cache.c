//========================================================//
//  cache.c                                               //
//  Source file for the Cache Simulator                   //
//                                                        //
//  Implement the I-cache, D-Cache and L2-cache as        //
//  described in the README                               //
//========================================================//

#include "cache.h"

#include <math.h>
#include <stdbool.h>

//
// TODO:Student Information
//
const char *studentName = "Warren Hu";
const char *studentID   = "A15154462";
const char *email       = "wsh003@ucsd.edu";

//------------------------------------//
//        Cache Configuration         //
//------------------------------------//

uint32_t icacheSets;     // Number of sets in the I$
uint32_t icacheAssoc;    // Associativity of the I$
uint32_t icacheHitTime;  // Hit Time of the I$

uint32_t dcacheSets;     // Number of sets in the D$
uint32_t dcacheAssoc;    // Associativity of the D$
uint32_t dcacheHitTime;  // Hit Time of the D$

uint32_t l2cacheSets;    // Number of sets in the L2$
uint32_t l2cacheAssoc;   // Associativity of the L2$
uint32_t l2cacheHitTime; // Hit Time of the L2$
uint32_t inclusive;      // Indicates if the L2 is inclusive

uint32_t blocksize;      // Block/Line size
uint32_t memspeed;       // Latency of Main Memory

//------------------------------------//
//          Cache Statistics          //
//------------------------------------//

uint64_t icacheRefs;       // I$ references
uint64_t icacheMisses;     // I$ misses
uint64_t icachePenalties;  // I$ penalties

uint64_t dcacheRefs;       // D$ references
uint64_t dcacheMisses;     // D$ misses
uint64_t dcachePenalties;  // D$ penalties

uint64_t l2cacheRefs;      // L2$ references
uint64_t l2cacheMisses;    // L2$ misses
uint64_t l2cachePenalties; // L2$ penalties

//------------------------------------//
//        Cache Data Structures       //
//------------------------------------//

// if l2 is inclusive and l2 had to evict something, then
// it must also be evicted from l1 to maintain inclusion
bool l2_did_evict;
uint32_t l2_evicted_addr;

uint32_t** l1i_tag_storage;
uint32_t** l1i_lru_storage;

uint32_t** l1d_tag_storage;
uint32_t** l1d_lru_storage;

uint32_t** l2_tag_storage;
uint32_t** l2_lru_storage;

//------------------------------------//
//          Cache Functions           //
//------------------------------------//

// Initialize the Cache Hierarchy
//
void
init_cache()
{
  // Initialize cache stats
  icacheRefs        = 0;
  icacheMisses      = 0;
  icachePenalties   = 0;
  dcacheRefs        = 0;
  dcacheMisses      = 0;
  dcachePenalties   = 0;
  l2cacheRefs       = 0;
  l2cacheMisses     = 0;
  l2cachePenalties  = 0;
  
  // Initialize Cache Simulator Data Structures

  // allocate l1i tag storage. no need for actual data storage
  l1i_tag_storage = malloc(sizeof(uint32_t*) * icacheSets);
  l1i_lru_storage = malloc(sizeof(uint32_t*) * icacheSets);
  for (size_t i = 0; i < icacheSets; i++) {
    l1i_tag_storage[i] = calloc(icacheAssoc, sizeof(uint32_t));
    l1i_lru_storage[i] = calloc(icacheAssoc, sizeof(uint32_t));
  }

  // allocate l1d tag storage. no need for actual data storage
  l1d_tag_storage = malloc(sizeof(uint32_t*) * dcacheSets);
  l1d_lru_storage = malloc(sizeof(uint32_t*) * dcacheSets);
  for (size_t i = 0; i < dcacheSets; i++) {
    l1d_tag_storage[i] = calloc(dcacheAssoc, sizeof(uint32_t));
    l1d_lru_storage[i] = calloc(dcacheAssoc, sizeof(uint32_t));
  }

  // allocate l1d tag storage. no need for actual data storage
  l2_tag_storage = malloc(sizeof(uint32_t*) * l2cacheSets);
  l2_lru_storage = malloc(sizeof(uint32_t*) * l2cacheSets);
  for (size_t i = 0; i < l2cacheSets; i++) {
    l2_tag_storage[i] = calloc(l2cacheAssoc, sizeof(uint32_t));
    l2_lru_storage[i] = calloc(l2cacheAssoc, sizeof(uint32_t));
  }

  l2_did_evict = false;
}

// Perform a memory access through the icache interface for the address 'addr'
// Return the access time for the memory operation
//
uint32_t
icache_access(uint32_t addr)
{
  if (icacheSets == 0) {
    return l2cache_access(addr);
  }

  icacheRefs++;

  // calculate index
  uint32_t blocksize_bitwidth = log2(blocksize);
  uint32_t index = (addr >> blocksize_bitwidth) & (icacheSets - 1);

  // calculate tag
  uint32_t index_bitwidth = log2(icacheSets);
  uint32_t tag = (addr >> blocksize_bitwidth) >> index_bitwidth;

  tag |= 0x80000000; // treat top most bit as valid bit

  // check the tags
  uint32_t* tags = l1i_tag_storage[index];
  for (size_t i = 0; i < icacheAssoc; i++) {
    if (tags[i] == tag) {
      // Bump up all the other LRU trackers
      for (size_t j = 0; j < icacheAssoc; j++) {
        if (l1i_lru_storage[index][j] != UINT32_MAX) {
          l1i_lru_storage[index][j]++;
        }
      }
      // Set the hit to the most recently used
      l1i_lru_storage[index][i] = 0;

      return icacheHitTime;
    }
  }

  // no tag found, need to evict the oldest entry and
  // load more data in
  icacheMisses++;

  // find the oldest entry and increment counters
  size_t index_to_evict = 0;
  uint32_t largest = 0;
  uint32_t* lru_storage = l1i_lru_storage[index];
  for (size_t i = 0; i < icacheAssoc; i++) {
    if (lru_storage[i] > largest) {
      largest = lru_storage[i];
      index_to_evict = i;
    }
    
    if (lru_storage[i] != UINT32_MAX) {
      lru_storage[i]++;
    }
  }

  // load into l1i
  tags[index_to_evict] = tag;
  lru_storage[index_to_evict] = 0;

  // load from l2
  uint32_t l2_time = l2cache_access(addr);

  // if inclusive and if l2 evicted something, need to evict same thing
  // from l1. note that for inclusion to work,
  // l2 associativity must be geq l1 associativity since
  // otherwise the evicted index wouldn't match up
  if (inclusive && l2_did_evict) {
    uint32_t evicted_index = (l2_evicted_addr >> blocksize_bitwidth) & (icacheSets - 1);
    uint32_t evicted_tag = (l2_evicted_addr >> blocksize_bitwidth) >> index_bitwidth;
    evicted_tag |= 0x80000000;

    uint32_t* set_to_check = l1i_tag_storage[evicted_index];
    for (size_t i = 0; i < icacheAssoc; i++) {
      if (set_to_check[i] == evicted_tag) {
        // evict
        set_to_check[i] = 0;
        l1i_lru_storage[evicted_index][i] = UINT32_MAX;
      }
    }
  }

  icachePenalties += l2_time;

  return icacheHitTime + l2_time;
}

// Perform a memory access through the dcache interface for the address 'addr'
// Return the access time for the memory operation
//
uint32_t
dcache_access(uint32_t addr)
{
  if (dcacheSets == 0) {
    return l2cache_access(addr);
  }

  dcacheRefs++;

  // calculate index
  uint32_t blocksize_bitwidth = log2(blocksize);
  uint32_t index = (addr >> blocksize_bitwidth) & (dcacheSets - 1);

  // calculate tag
  uint32_t index_bitwidth = log2(dcacheSets);
  uint32_t tag = (addr >> blocksize_bitwidth) >> index_bitwidth;

  tag |= 0x80000000; // treat top most bit as valid bit

  // check the tags
  uint32_t* tags = l1d_tag_storage[index];
  for (size_t i = 0; i < dcacheAssoc; i++) {
    if (tags[i] == tag) {
      // Bump up all the other LRU trackers
      for (size_t j = 0; j < dcacheAssoc; j++) {
        if (l1d_lru_storage[index][j] != UINT32_MAX) {
          l1d_lru_storage[index][j]++;    
         }
      }
      // Set the hit to the most recently used
      l1d_lru_storage[index][i] = 0;

      return dcacheHitTime;
    }
  }

  // no tag found, need to evict the oldest entry and
  // load more data in
  dcacheMisses++;

  // find the oldest entry and increment counters
  size_t index_to_evict = 0;
  uint32_t largest = 0;
  uint32_t* lru_storage = l1d_lru_storage[index];
  for (size_t i = 0; i < dcacheAssoc; i++) {
    if (lru_storage[i] > largest) {
      largest = lru_storage[i];
      index_to_evict = i;
    }
    
    if (lru_storage[i] != UINT32_MAX) {
      lru_storage[i]++;  
    }
  }

  // load into l1d
  tags[index_to_evict] = tag;
  lru_storage[index_to_evict] = 0;

  // load from l2
  uint32_t l2_time = l2cache_access(addr);

  // if inclusive and if l2 evicted something, need to evict same thing
  // from l1. note that for inclusion to work,
  // l2 associativity must be geq l1 associativity since
  // otherwise the evicted index wouldn't match up
  if (inclusive && l2_did_evict) {
    uint32_t evicted_index = (l2_evicted_addr >> blocksize_bitwidth) & (dcacheSets - 1);
    uint32_t evicted_tag = (l2_evicted_addr >> blocksize_bitwidth) >> index_bitwidth;
    evicted_tag |= 0x80000000;

    uint32_t* set_to_check = l1d_tag_storage[evicted_index];
    for (size_t i = 0; i < dcacheAssoc; i++) {
      if (set_to_check[i] == evicted_tag) {
        // evict
        set_to_check[i] = 0;
        l1d_lru_storage[evicted_index][i] = UINT32_MAX;
      }
    }
  }

  // calculate dcache miss penalty
  dcachePenalties += l2_time;

  return dcacheHitTime + l2_time;
}

// Perform a memory access to the l2cache for the address 'addr'
// Return the access time for the memory operation
//
uint32_t
l2cache_access(uint32_t addr)
{
  l2_did_evict = false;

  if (l2cacheSets == 0) {
    return memspeed;
  }

  l2cacheRefs++;

  // calculate index
  uint32_t blocksize_bitwidth = log2(blocksize);
  uint32_t index = (addr >> blocksize_bitwidth) & (l2cacheSets - 1);

  // calculate tag
  uint32_t index_bitwidth = log2(l2cacheSets);
  uint32_t tag = (addr >> blocksize_bitwidth) >> index_bitwidth;

  tag |= 0x80000000; // treat top most bit as valid bit

  // check the tags
  uint32_t* tags = l2_tag_storage[index];
  for (size_t i = 0; i < l2cacheAssoc; i++) {
    if (tags[i] == tag) {
      // Bump up all the other LRU trackers
      for (size_t j = 0; j < l2cacheAssoc; j++) {
        l2_lru_storage[index][j]++;
      }
      // Set the hit to the most recently used
      l2_lru_storage[index][i] = 0;

      return l2cacheHitTime;
    }
  }

  // no tag found, need to evict the oldest entry and
  // load more data in
  l2cacheMisses++;

  // find the oldest entry and increment counters
  size_t index_to_evict = 0;
  uint32_t largest = 0;
  uint32_t* lru_storage = l2_lru_storage[index];
  for (size_t i = 0; i < l2cacheAssoc; i++) {
    if (lru_storage[i] > largest) {
      largest = lru_storage[i];
      index_to_evict = i;
    }
    
    lru_storage[i]++;
  }

  // check if evicted entry was valid
  if (tags[index_to_evict] & 0x80000000) {
    l2_did_evict = true;
    l2_evicted_addr = ((tags[index_to_evict] << blocksize_bitwidth) << index_bitwidth) |
                      (index_to_evict << blocksize_bitwidth);
  }

  // load into l2
  tags[index_to_evict] = tag;
  lru_storage[index_to_evict] = 0;

  // miss penalty is always just memspeed as there is no higher cache
  l2cachePenalties += memspeed;

  return l2cacheHitTime + memspeed;
}
