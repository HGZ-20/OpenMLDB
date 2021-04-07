/*
 * Copyright 2021 4Paradigm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_VM_ENGINE_H_
#define INCLUDE_VM_ENGINE_H_

#include <map>
#include <memory>
#include <mutex>  //NOLINT
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "base/raw_buffer.h"
#include "base/spin_lock.h"
#include "codec/fe_row_codec.h"
#include "codec/list_iterator_codec.h"
#include "gflags/gflags.h"
#include "llvm-c/Target.h"
#include "proto/fe_common.pb.h"
#include "vm/catalog.h"
#include "vm/engine_context.h"
#include "vm/router.h"

namespace hybridse {
namespace vm {

using ::hybridse::codec::Row;

class Engine;
/// \brief An options class for controlling engine behaviour.
class EngineOptions {
 public:
    EngineOptions();

    /// Set if support to store ir results into SqlContext.
    inline void set_keep_ir(bool flag) { this->keep_ir_ = flag; }
    /// Return if support to store ir results into SqlContext.
    inline bool is_keep_ir() const { return this->keep_ir_; }

    /// Set if only support to compile SQL.
    ///
    /// If set `true`, the engine won't generate runner plan as well.
    inline void set_compile_only(bool flag) { this->compile_only_ = flag; }
    /// Return if only support to compile physical plan.
    inline bool is_compile_only() const { return compile_only_; }


    /// Set if the engine only generate physical plan.
    ///
    /// If set `true`, the engine won't build llvm jit.
    inline void set_plan_only(bool flag) { plan_only_ = flag; }
    ///
    inline bool is_plan_only() const { return plan_only_; }

    /// Return the maximum number of entries we can hold for compiling cache.
    inline uint32_t max_sql_cache_size() const { return max_sql_cache_size_; }

    /// Set the maxinum number of cache entries.
    inline void set_max_sql_cache_size(uint32_t size) {
        max_sql_cache_size_ = size;
    }

    /// Set if the engine is performance sensitive.
    ///
    /// Normally, the engine can support more abilities under performance un-sensitive mode.
    inline void set_performance_sensitive(bool flag) {
        performance_sensitive_ = flag;
    }
    /// Return if the engine is performance sensitive.
    inline bool is_performance_sensitive() const {
        return performance_sensitive_;
    }

    /// Set if the engine support cluster optimization.
    inline EngineOptions* set_cluster_optimized(bool flag) {
        cluster_optimized_ = flag;
        return this;
    }
    /// Return if the engine support cluster optimization.
    inline bool is_cluster_optimzied() const { return cluster_optimized_; }

    /// Set if the engine supoort batch request optimization.
    inline EngineOptions* set_batch_request_optimized(bool flag) {
        batch_request_optimized_ = flag;
        return this;
    }
    /// Return if the engine support batch request optimization.
    inline bool is_batch_request_optimized() const { return batch_request_optimized_; }


    /// Set if the engine can support expression optimization.
    inline EngineOptions* set_enable_expr_optimize(bool flag) {
        enable_expr_optimize_ = flag;
        return this;
    }
    /// Return if the engine support expression optimization
    inline bool is_enable_expr_optimize() const { return enable_expr_optimize_; }


    /// Set if the engine can support batch window parallelization.
    inline EngineOptions* set_enable_batch_window_parallelization(bool flag) {
        enable_batch_window_parallelization_ = flag;
        return this;
    }
    /// Return if the engine support batch window parallelization.
    inline bool is_enable_batch_window_parallelization() const {
        return enable_batch_window_parallelization_;
    }

    /// Set if the engine can support spark unsafe row format.
    inline EngineOptions* set_enable_spark_unsaferow_format(bool flag);
    /// Return if the engine can support can support spark unsafe row format.
    inline bool is_enable_spark_unsaferow_format() const {
        return enable_spark_unsaferow_format_;
    }

    /// Return JitOptions
    inline hybridse::vm::JitOptions& jit_options() { return jit_options_; }

 private:
    bool keep_ir_;
    bool compile_only_;
    bool plan_only_;
    bool performance_sensitive_;
    bool cluster_optimized_;
    bool batch_request_optimized_;
    bool enable_expr_optimize_;
    bool enable_batch_window_parallelization_;
    uint32_t max_sql_cache_size_;
    bool enable_spark_unsaferow_format_;
    JitOptions jit_options_;
};

/// \brief A RunSession maintain SQL running context, including compile information, procedure name.
///
class RunSession {
 public:
    explicit RunSession(EngineMode engine_mode);

    virtual ~RunSession();

    /// Return query result schema.
    virtual const Schema& GetSchema() const {
        return compile_info_->GetSchema();
    }

    /// Return query schema string.
    virtual const std::string& GetEncodedSchema() const {
        return compile_info_->GetEncodedSchema();
    }

    /// Return query related compile information.
    virtual std::shared_ptr<hybridse::vm::CompileInfo> GetCompileInfo() {
        return compile_info_;
    }

    /// Update query related compile information.
    bool SetCompileInfo(
        const std::shared_ptr<hybridse::vm::CompileInfo>& compile_info);

    /// Enable printing debug information while running a query.
    void EnableDebug() { is_debug_ = true; }
    /// Disable printing debug information while running a query.
    void DisableDebug() { is_debug_ = false; }
    /// Return if this run session support printing debug information.
    bool IsDebug() { return is_debug_; }

    /// Bind this run session with specific procedure
    void SetSpName(const std::string& sp_name) { sp_name_ = sp_name; }
    /// Return the engine mode of this run session
    EngineMode engine_mode() const { return engine_mode_; }

 protected:
    std::shared_ptr<hybridse::vm::CompileInfo> compile_info_;
    hybridse::vm::EngineMode engine_mode_;
    bool is_debug_;
    std::string sp_name_;
    friend Engine;
};

/// \brief BatchRunSession is a kind of RunSession designed for batch mode query.
class BatchRunSession : public RunSession {
 public:
    explicit BatchRunSession(bool mini_batch = false)
        : RunSession(kBatchMode), mini_batch_(mini_batch) {}
    ~BatchRunSession() {}
    /// \brief Query sql in batch mode.
    /// Query results will be returned as std::vector<Row> in output
    int32_t Run(std::vector<Row>& output,  // NOLINT
                uint64_t limit = 0);
    /// \brief Query sql in batch mode.
    /// Return query result as TableHandler pointer.
    std::shared_ptr<TableHandler> Run();

 private:
    const bool mini_batch_;
};
/// \brief RequestRunSession is a kind of RunSession designed for request mode query.
///
/// Request-mode query is widely used in OLAD database. It requires a request Row.
class RequestRunSession : public RunSession {
 public:
    RequestRunSession() : RunSession(kRequestMode) {}
    ~RequestRunSession() {}
    /// \brief Query sql in request mode.
    ///
    /// \param in_row request row
    /// \param output query result will be returned as Row in output
    /// \return `0` if run successfully else negative integer
    int32_t Run(const Row& in_row, Row* output);                    // NOLINT

    /// \brief Run a task specified by task_id in request mode.
    ///
    /// \param task_id: task id of task
    /// \param in_row: request row
    /// \param[out] output: result is written to this variable
    /// \return `0` if run successfully else negative integer
    int32_t Run(uint32_t task_id, const Row& in_row, Row* output);  // NOLINT


    /// \brief Return the schema of request row
    virtual const Schema& GetRequestSchema() const {
        return compile_info_->GetRequestSchema();
    }
    /// \brief Return the name of request row
    virtual const std::string& GetRequestName() const {
        return compile_info_->GetRequestName();
    }
};

/// \brief BatchRequestRunSession is a kind of RunSession designed for batch request mode query.
///
/// BatchRequest mode query is widely used in OLAD database. It requires a batch of request Rows.
class BatchRequestRunSession : public RunSession {
 public:
    BatchRequestRunSession() : RunSession(kBatchRequestMode) {}
    ~BatchRequestRunSession() {}

    /// \brief Return the schema of request row
    const Schema& GetRequestSchema() const {
        return compile_info_->GetRequestSchema();
    }
    /// \brief Return the name of request row
    const std::string& GetRequestName() const {
        return compile_info_->GetRequestName();
    }

    /// \brief Run query in batch request mode.
    /// \param request_batch: a batch of request rows
    /// \param output: query results will be returned as std::vector<Row> in output
    /// \return 0 if runs successfully else negative integer
    int32_t Run(const std::vector<Row>& request_batch,
                std::vector<Row>& output);  // NOLINT

    /// \brief Run a task specified by task_id in request mode.
    /// \param id: id of task
    /// \param request_batch: a batch of request rows
    /// \param output: query results will be returned as std::vector<Row> in output
    /// \return 0 if runs successfully else negative integer
    int32_t Run(const uint32_t id, const std::vector<Row>& request_batch,
                std::vector<Row>& output);  // NOLINT

    /// \brief Add common column idx
    void AddCommonColumnIdx(size_t idx) { common_column_indices_.insert(idx); }

    /// \brief Return a set of common column indices
    const std::set<size_t>& common_column_indices() const {
        return common_column_indices_;
    }

 private:
    std::set<size_t> common_column_indices_;
};

/// An options class for controlling runtime interpreter behavior.
struct ExplainOutput {
    vm::Schema input_schema;    ///< The schema of request row for request-mode query
    std::string request_name;   ///< The name of request for request-mode query
    std::string logical_plan;   ///< Logical plan string
    std::string physical_plan;  ///< Physical plan string
    std::string ir;             ///< Codegen IR String
    vm::Schema output_schema;   ///< The schema of query result
    vm::Router router;          ///< The Router for request-mode query
};


/// \brief An engine is responsible to compile SQL on the specific Catalog.
///
/// An engine can be used to `compile sql and explain the compiling result.
/// It maintains a LRU cache for compiling result.
///
/// **Example**
/// ```
/// // Assuming the catalog has been created and initialized before
/// base::Status status;
/// EngineOptions options;
/// Engine engine(catalog, options);
/// BatchRunSession session;
/// std::string db = "test_db";
/// std::string sql = "select col0, col1, col2, col1+col2 as col12 from t1;";
/// engine.Get(sql, db, session, status);
/// engine.Explain(sql, db, EngineMode::kBatchMode, &output, &status);
/// ```
class Engine {
 public:
    /// \brief Create an Engine with a specific Catalog object.
    explicit Engine(const std::shared_ptr<Catalog>& cl);

    /// \brief Create an Engine a specific Catalog object, configuring it with EngineOptions
    Engine(const std::shared_ptr<Catalog>& cl, const EngineOptions& options);

    /// \brief Initialize LLVM environments
    static void InitializeGlobalLLVM();

    ~Engine();

    /// \brief Compile sql in db and stored the results in the session
    bool Get(const std::string& sql, const std::string& db,
             RunSession& session,    // NOLINT
             base::Status& status);  // NOLINT

    /// \brief Search all tables related to the specific sql in db.
    ///
    /// The tables' names are returned in tables
    bool GetDependentTables(const std::string& sql, const std::string& db,
                            EngineMode engine_mode,
                            std::set<std::string>* tables,
                            base::Status& status);  // NOLINT

    /// \brief Explain sql compiling result.
    ///
    /// The results are returned as ExplainOutput in explain_output.
    /// The success or fail status message is returned as Status in status.
    /// TODO: base::Status* status -> base::Status& status
    bool Explain(const std::string& sql, const std::string& db,
                 EngineMode engine_mode, ExplainOutput* explain_output,
                 base::Status* status);

    /// \brief Same as above, but allowing compiling with configuring common column indices.
    ///
    /// The common column indices are used for common column optimization under EngineMode::kBatchRequestMode
    bool Explain(const std::string& sql, const std::string& db,
                 EngineMode engine_mode,
                 const std::set<size_t>& common_column_indices,
                 ExplainOutput* explain_output, base::Status* status);

    /// \brief Update engine's catalog
    inline void UpdateCatalog(std::shared_ptr<Catalog> cl) {
        std::atomic_store_explicit(&cl_, cl, std::memory_order_release);
    }

    /// \brief Clear engine's compiling result cache
    void ClearCacheLocked(const std::string& db);

 private:
    bool GetDependentTables(node::PlanNode* node, std::set<std::string>* tables,
                            base::Status& status);  // NOLINT
    std::shared_ptr<CompileInfo> GetCacheLocked(const std::string& db,
                                                const std::string& sql,
                                                EngineMode engine_mode);
    bool SetCacheLocked(const std::string& db, const std::string& sql,
                        EngineMode engine_mode,
                        std::shared_ptr<CompileInfo> info);

    bool IsCompatibleCache(RunSession& session,  // NOLINT
                           std::shared_ptr<CompileInfo> info,
                           base::Status& status);  // NOLINT

    std::shared_ptr<Catalog> cl_;
    EngineOptions options_;
    base::SpinMutex mu_;
    EngineLRUCache lru_cache_;
};

/// \brief Local tablet is responsible to run a task locally.
///
/// Local tablet won't invoke rpc to run a task remotely.
class LocalTablet : public Tablet {
 public:
    explicit LocalTablet(
        hybridse::vm::Engine* engine,
        std::shared_ptr<hybridse::vm::CompileInfoCache> sp_cache)
        : Tablet(),
          name_("LocalTablet"),
          engine_(engine),
          sp_cache_(sp_cache) {}
    ~LocalTablet() {}

    /// Run a task in request mode locally
    /// \param task_id: id of task
    /// \param db: name of database
    /// \param sql: represents a sql string if `is_procedure` is ture, or represents a procedure name
    /// \param row: request row
    /// \param is_procedure: whether sql is a procedure or not
    /// \param is_debug: whether printing debug information while running
    /// \return result row as RowHandler pointer
    std::shared_ptr<RowHandler> SubQuery(uint32_t task_id,
                                         const std::string& db,
                                         const std::string& sql, const Row& row,
                                         const bool is_procedure,
                                         const bool is_debug) override;

    /// Run a task in batch-request mode locally
    /// \param task_id: id of task
    /// \param db: name of database
    /// \param sql: represents a sql string if `is_procedure` is ture, or represents a procedure name
    /// \param common_column_indices: a set of common column indices
    /// \param in_rows: a batch of request rows
    /// \param request_is_common: whether request is common or not
    /// \param is_procedure: whether run procedure or not
    /// \param is_debug: whether printing debug information while running
    /// \return result rows as TableHandler pointer
    virtual std::shared_ptr<TableHandler> SubQuery(
        uint32_t task_id, const std::string& db, const std::string& sql,
        const std::set<size_t>& common_column_indices,
        const std::vector<Row>& in_rows, const bool request_is_common,
        const bool is_procedure, const bool is_debug);

    /// Return the name of tablet
    const std::string& GetName() const { return name_; }

 private:
    const std::string name_;
    vm::Engine* engine_;
    std::shared_ptr<hybridse::vm::CompileInfoCache> sp_cache_;
};

}  // namespace vm
}  // namespace hybridse
#endif  // INCLUDE_VM_ENGINE_H_
