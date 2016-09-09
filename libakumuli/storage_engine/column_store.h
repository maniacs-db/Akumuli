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

/* ColumnStore + reshape functionality
 * selct cpu where host=XXXX group by tag order by time from 0 to 100;
 * TS  Series name Value
 *  0  cpu tag=Foo    10
 *  0  cpu tag=Bar    20
 *  1  cpu tag=Foo    10
 *  2  cpu tag=Foo    12
 *  2  cpu tag=Bar    30
 *  ...
 *
 * selct cpu where host=XXXX group by tag order by series from 0 to 100;
 * TS  Series name Value
 *  0  cpu tag=Foo    21
 *  1  cpu tag=Foo    20
 * ...
 * 99  cpu tag=Foo    19
 *  0  cpu tag=Bar    20
 *  1  cpu tag=Bar    11
 * ...
 * 99  cpu tag=Bar    14
 *  ...
 *
 * It is possible to add processing steps via IQueryProcessor.
 */

//! Set of ids returned by the query (defined by select and where clauses)
struct Selection {
    std::vector<aku_ParamId> ids;
    aku_Timestamp begin;
    aku_Timestamp end;
};

//! Mapping from persistent series names to transient series names
struct GroupBy {
    bool enabled;
    std::unordered_map<aku_ParamId, aku_ParamId> transient_map;
};

//! Output order
enum class OrderBy {
    SERIES,
    TIME,
};

//! Reshape request defines what should be sent to query processor
struct ReshapeRequest {
    Selection select;
    GroupBy group_by;
    OrderBy order_by;
};

/** Columns store.
  * Serve as a central data repository for series metadata and all individual columns.
  * Each column is addressed by the series name. Data can be written in through WriteSession
  * and read back via IQueryProcessor interface. ColumnStore can reshape data (group, merge or join
  * different columns together).
  * Columns are built from NB+tree instances.
  * Instances of this class is thread-safe.
  */
class ColumnStore : public std::enable_shared_from_this<ColumnStore> {
    std::shared_ptr<StorageEngine::BlockStore> blockstore_;
    std::unique_ptr<MetadataStorage> metadata_;
    std::unordered_map<aku_ParamId, std::shared_ptr<NBTreeExtentsList>> columns_;
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
    ColumnStore(std::shared_ptr<StorageEngine::BlockStore> bstore, std::unique_ptr<MetadataStorage>&& meta);

    // No value semantics allowed.
    ColumnStore(ColumnStore const&) = delete;
    ColumnStore(ColumnStore &&) = delete;
    ColumnStore& operator = (ColumnStore const&) = delete;

    /** Create new column.
      * @return completion status
      */
    aku_Status create_new_column(aku_ParamId id);

    /** Write sample to data-store.
      * @param sample to write
      * @param cache_or_null is a pointer to external cache, tree ref will be added there on success
      */
    NBTreeAppendResult write(aku_Sample const& sample, std::vector<LogicAddr> *rescue_points,
                     std::unordered_map<aku_ParamId, std::shared_ptr<NBTreeExtentsList> > *cache_or_null=nullptr);

    //! Slice and dice data according to request and feed it to query processor
    void query(ReshapeRequest const& req, QP::IQueryProcessor& qproc);
};


/** Dispatches incoming messages to corresponding NBTreeExtentsList instances.
  * Should be created per writer thread. Stores series matcher cache and tree
  * cache. ColumnStore can work without WriteSession.
  */
class CStoreSession : public std::enable_shared_from_this<CStoreSession>
{
    //! Link to global column store.
    std::shared_ptr<ColumnStore> cstore_;
    //! Tree cache
    std::unordered_map<aku_ParamId, std::shared_ptr<NBTreeExtentsList>> cache_;
public:
    //! C-tor. Shouldn't be called directly.
    CStoreSession(std::shared_ptr<ColumnStore> registry);

    CStoreSession(CStoreSession const&) = delete;
    CStoreSession(CStoreSession &&) = delete;
    CStoreSession& operator = (CStoreSession const&) = delete;

    //! Write sample
    NBTreeAppendResult write(const aku_Sample &sample, std::vector<LogicAddr>* rescue_points);

    void query(const ReshapeRequest &req, QP::IQueryProcessor& qproc);
};

}}  // namespace
