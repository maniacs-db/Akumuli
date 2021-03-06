/**
 * PRIVATE HEADER
 *
 * Utils
 *
 * Copyright (c) 2013 Eugene Lazin <4lazin@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */


#pragma once

#include "akumuli.h"
#include <apr_general.h>
#include <apr_mmap.h>
#include <atomic>
#include <boost/throw_exception.hpp>
#include <ostream>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Akumuli {
/** APR error converter */
std::string apr_error_message(apr_status_t status);

/** Set global panic handler */
void set_panic_handler(aku_panic_handler_t new_panic_handler);

/** APR error wrapper.
      * Code doesn't need to throw this exception directly, it must use AKU_APR_PANIC macro.
      */
struct AprException : public std::runtime_error {
    apr_status_t status;

    /** C-tor
          * @param status APR status
          * @param message APR error message
          */
    AprException(apr_status_t status, const char* message);
};

/** Akumuli exception type.
     *  Code doesn't need to throw it directly, it must use AKU_PANIC macro.
     */
struct Exception : public std::runtime_error {
    /** C-tor
      * @param message Error message
      */
    Exception(const char* message);

    /** C-tor
      * @param message Error message
      */
    Exception(std::string message);
};

std::ostream& operator<<(std::ostream& str, Exception const& except);


/** Memory mapped file
      * maps all file on construction
      */
class MemoryMappedFile {
    apr_pool_t*  mem_pool_;  //< local memory pool
    apr_mmap_t*  mmap_;
    apr_file_t*  fp_;
    apr_finfo_t  finfo_;
    apr_status_t status_;
    std::string  path_;
    const bool   enable_huge_tlb_;

public:
    MemoryMappedFile(const char* file_name, bool enable_huge_tlb);
    ~MemoryMappedFile();
    void move_file(const char* new_name);
    void   delete_file();
    void*  get_pointer() const;
    size_t get_size() const;
    //! Flush only part of the page
    apr_status_t flush(size_t from, size_t to);
    //! Flush full page
    apr_status_t flush();
    bool         is_bad() const;
    std::string  error_message() const;
    void         panic_if_bad();
    apr_status_t status_code() const;
    //! Remap file in a destructive way (all file content is lost)
    void remap_file_destructive();
    //! Protect page from writing
    aku_Status protect_all();
    //! Make page available for writing
    aku_Status unprotect_all();

private:
    //! Map file into virtual address space
    apr_status_t map_file();
    //! Free OS resources associated with object
    void free_resources(int cnt);
};

//! Fast integer logarithm
i64 log2(i64 value);

size_t get_page_size();

const void* align_to_page(const void* ptr, size_t get_page_size);

void* align_to_page(void* ptr, size_t get_page_size);

void prefetch_mem(const void* ptr, size_t mem_size);
    
class Rand {
    std::ranlux48_base rand_;

public:
    Rand();
    u32 operator()();
};

/** Reader writer lock
     *  mutex.
     */
class RWLock {
    // TODO: specializations for different platforms
    pthread_rwlock_t rwlock_;

public:
    RWLock();

    ~RWLock();

    void rdlock();

    bool try_rdlock();

    void wrlock();

    bool try_wrlock();

    void unlock();
};

//! Compare two double values and return true if they are equal at bit-level (needed to supress CLang analyzer warnings).
bool same_value(double a, double b);
}

/** Panic macro.
  * @param msg error message
  * @throws Exception.
  */
#define AKU_PANIC(msg) BOOST_THROW_EXCEPTION(Akumuli::Exception(std::move(msg)));

/** Panic macro that can use APR error code to panic more informative.
  * @param msg error message
  * @param status APR status
  * @throws AprException.
  */
#define AKU_APR_PANIC(status, msg) BOOST_THROW_EXCEPTION(Akumuli::AprException(status, msg));

//! Macro to supress `variable unused` warnings for variables that is unused for a reason.
#define AKU_UNUSED(x) (void)(x)

#ifdef __GNUC__
#define AKU_LIKELY(x) __builtin_expect((x), 1)
#define AKU_UNLIKELY(x) __builtin_expect((x), 0)
#else
#define AKU_LIKELY(x) (x)
#define AKU_UNLIKELY(x) (x)
#endif

