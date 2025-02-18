/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#ifndef META_SCHEMAMANAGER_H_
#define META_SCHEMAMANAGER_H_

#include "base/Base.h"
#include "base/StatusOr.h"
#include <folly/RWSpinLock.h>
#include "meta/NebulaSchemaProvider.h"

namespace nebula {
namespace meta {

class MetaClient;

using TagSchemas =
    std::unordered_map<TagID, std::vector<std::shared_ptr<const NebulaSchemaProvider>>>;

using EdgeSchemas =
    std::unordered_map<EdgeType, std::vector<std::shared_ptr<const NebulaSchemaProvider>>>;

class SchemaManager {
public:
    virtual ~SchemaManager() = default;

    static std::unique_ptr<SchemaManager> create(MetaClient *client);

    virtual StatusOr<int32_t> getSpaceVidLen(GraphSpaceID space) = 0;

    virtual std::shared_ptr<const NebulaSchemaProvider>
    getTagSchema(GraphSpaceID space, TagID tag, SchemaVer ver = -1) = 0;

    // Returns a negative number when the schema does not exist
    virtual StatusOr<SchemaVer> getLatestTagSchemaVersion(GraphSpaceID space, TagID tag) = 0;

    virtual std::shared_ptr<const NebulaSchemaProvider>
    getEdgeSchema(GraphSpaceID space, EdgeType edge, SchemaVer ver = -1) = 0;

    // Returns a negative number when the schema does not exist
    virtual StatusOr<SchemaVer> getLatestEdgeSchemaVersion(GraphSpaceID space, EdgeType edge) = 0;

    virtual StatusOr<GraphSpaceID> toGraphSpaceID(folly::StringPiece spaceName) = 0;

    virtual StatusOr<TagID> toTagID(GraphSpaceID space, folly::StringPiece tagName) = 0;

    virtual StatusOr<std::string> toTagName(GraphSpaceID space, TagID tagId) = 0;

    virtual StatusOr<EdgeType> toEdgeType(GraphSpaceID space, folly::StringPiece typeName) = 0;

    virtual StatusOr<std::string> toEdgeName(GraphSpaceID space, EdgeType edgeType) = 0;

    virtual StatusOr<std::vector<std::string>> getAllEdge(GraphSpaceID space) = 0;

    // get all version of all tag schema
    virtual StatusOr<TagSchemas> getAllVerTagSchema(GraphSpaceID space) = 0;

    // get all version of all edge schema
    virtual StatusOr<EdgeSchemas> getAllVerEdgeSchema(GraphSpaceID space) = 0;


protected:
    SchemaManager() = default;
};

}  // namespace meta
}  // namespace nebula
#endif  // META_SCHEMAMANAGER_H_
