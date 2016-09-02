/**
 * Copyright (c) 2016 Eugene Lazin <4lazin@gmail.com>
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

/* In general this is a tree-roots collection combined with series name parser
 * and series registery (backed by sqlite). One TreeRegistery should be created
 * per database. This registery can be used to create sessions. Session instances
 * should be created per-connection for each connection to operate locally (without
 * synchronization). This code assumes that each connection works with its own
 * set of time-series. If this isn't the case - performance penalty will be introduced.
 */

// Stdlib
#include <unordered_map>
#include <mutex>

// Boost libraries
#include <boost/property_tree/ptree_fwd.hpp>


// Project
#include "akumuli_def.h"
#include "external_cursor.h"
#include "metadatastorage.h"
#include "seriesparser.h"
#include "storage_engine/nbtree.h"
#include <queryprocessor_framework.h>

namespace Akumuli {
namespace StorageEngine {


/** Global tree registery.
  * Serve as a central data repository for series metadata and NBTree roots.
  * Client code should create `StreamDispatcher` per connection, each dispatcher
  * should have link to `TreeRegistry`.
  * Instances of this class is thread-safe.
  */
class TreeRegistry : public std::enable_shared_from_this<TreeRegistry> {
    std::shared_ptr<StorageEngine::BlockStore> blockstore_;
    std::unique_ptr<MetadataStorage> metadata_;
    std::unordered_map<aku_ParamId, std::shared_ptr<NBTreeExtentsList>> table_;
    SeriesMatcher global_matcher_;
    //! List of metadata to update
    std::unordered_map<aku_ParamId, std::vector<StorageEngine::LogicAddr>> rescue_points_;

    //! Mutex for metadata storage and rescue points list
    std::mutex metadata_lock_;
    //! Mutex for table_ hashmap (shrink and resize)
    std::mutex table_lock_;

    //! Syncronization for watcher thread
    std::condition_variable cvar_;

public:
    TreeRegistry(std::shared_ptr<StorageEngine::BlockStore> bstore, std::unique_ptr<MetadataStorage>&& meta);

    // No value semantics allowed.
    TreeRegistry(TreeRegistry const&) = delete;
    TreeRegistry(TreeRegistry &&) = delete;
    TreeRegistry& operator = (TreeRegistry const&) = delete;

    //! Match series name. If series with such name doesn't exists - create it.
    aku_Status init_series_id(const char* begin, const char* end, aku_Sample *sample, SeriesMatcher *local_matcher);

    int get_series_name(aku_ParamId id, char* buffer, size_t buffer_size, SeriesMatcher *local_matcher);

    //! Update rescue points list for `id`.
    void update_rescue_points(aku_ParamId id, std::vector<StorageEngine::LogicAddr>&& addrlist);

    //! Write rescue points to persistent storage synchronously.
    void sync_with_metadata_storage();

    //! Waint until some data will be available.
    aku_Status wait_for_sync_request(int timeout_us);

    /** Write sample to data-store.
      * @param sample to write
      * @param cache_or_null is a pointer to external cache, tree ref will be added there on success
      */
    aku_Status write(aku_Sample const& sample,
                     std::unordered_map<aku_ParamId, std::shared_ptr<NBTreeExtentsList> > *cache_or_null=nullptr);

    //! Query data
    void query(QP::IQueryProcessor& qproc);
};


/** Dispatches incoming messages to corresponding NBTreeExtentsList instances.
  * Should be created per writer thread. Stores series matcher cache and tree
  * cache. TreeRegistry can work without Session.
  */
class Session : public std::enable_shared_from_this<Session>
{
    //! Link to global registry.
    std::shared_ptr<TreeRegistry> registry_;
    //! Local series matcher (with cached global data).
    SeriesMatcher local_matcher_;
    //! Tree cache
    std::unordered_map<aku_ParamId, std::shared_ptr<NBTreeExtentsList>> cache_;
public:
    //! C-tor. Shouldn't be called directly.
    Session(std::shared_ptr<TreeRegistry> registry);

    Session(Session const&) = delete;
    Session(Session &&) = delete;
    Session& operator = (Session const&) = delete;

    /** Match series name. If series with such name doesn't exists - create it.
      * This method should be called for each sample to init its `paramid` field.
      */
    aku_Status init_series_id(const char* begin, const char* end, aku_Sample *sample);

    int get_series_name(aku_ParamId id, char* buffer, size_t buffer_size);

    //! Write sample
    aku_Status write(const aku_Sample &sample);

    void query(QP::IQueryProcessor& qproc);
};

}}  // namespace
