/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "base/Base.h"
#include <folly/hash/Hash.h>
#include "clients/meta/MetaClient.h"
#include "network/NetworkUtils.h"
#include "meta/NebulaSchemaProvider.h"
#include "conf/Configuration.h"
#include "stats/StatsManager.h"
#include "clients/meta/FileBasedClusterIdMan.h"
#include <folly/ScopeGuard.h>


DEFINE_int32(heartbeat_interval_secs, 3, "Heartbeat interval");
DEFINE_int32(meta_client_retry_times, 3, "meta client retry times, 0 means no retry");
DEFINE_int32(meta_client_retry_interval_secs, 1,
             "meta client sleep interval between retry");
DEFINE_int32(meta_client_timeout_ms, 60 * 1000,
             "meta client timeout");
DEFINE_string(cluster_id_path, "cluster.id",
              "file path saved clusterId");
DECLARE_string(gflags_mode_json);


namespace nebula {
namespace meta {

MetaClient::MetaClient(std::shared_ptr<folly::IOThreadPoolExecutor> ioThreadPool,
                       std::vector<HostAddr> addrs,
                       const MetaClientOptions& options)
        : ioThreadPool_(ioThreadPool)
        , addrs_(std::move(addrs))
        , options_(options) {
    CHECK(ioThreadPool_ != nullptr) << "IOThreadPool is required";
    CHECK(!addrs_.empty())
        << "No meta server address is specified. Meta server is required";
    clientsMan_ = std::make_shared<
        thrift::ThriftClientManager<cpp2::MetaServiceAsyncClient>
    >();
    updateActive();
    updateLeader();
    bgThread_ = std::make_unique<thread::GenericWorker>();
    LOG(INFO) << "Create meta client to " << active_;
}


MetaClient::~MetaClient() {
    stop();
    VLOG(3) << "~MetaClient";
}


bool MetaClient::isMetadReady() {
    auto ret = heartbeat().get();
    if (!ret.ok() && ret.status() != Status::LeaderChanged()) {
        LOG(ERROR) << "Heartbeat failed, status:" << ret.status();
        ready_ = false;
        return ready_;
    }

    bool ldRet = loadData();
    bool lcRet = true;
    if (!options_.skipConfig_) {
        lcRet = loadCfg();
    }
    if (ldRet && lcRet) {
        localLastUpdateTime_ = metadLastUpdateTime_;
    }
    return ready_;
}


bool MetaClient::waitForMetadReady(int count, int retryIntervalSecs) {
    if (!options_.skipConfig_) {
        std::string gflagsJsonPath;
        GflagsManager::getGflagsModule(gflagsModule_);
        gflagsDeclared_ = GflagsManager::declareGflags(gflagsModule_);
    }
    isRunning_ = true;
    int tryCount = count;
    while (!isMetadReady() && ((count == -1) || (tryCount > 0)) && isRunning_) {
        LOG(INFO) << "Waiting for the metad to be ready!";
        --tryCount;
        ::sleep(retryIntervalSecs);
    }  // end while

    if (!isRunning_) {
        LOG(ERROR) << "Connect to the MetaServer Failed";
        return false;
    }

    CHECK(bgThread_->start());
    LOG(INFO) << "Register time task for heartbeat!";
    size_t delayMS = FLAGS_heartbeat_interval_secs * 1000 + folly::Random::rand32(900);
    bgThread_->addDelayTask(delayMS, &MetaClient::heartBeatThreadFunc, this);
    return ready_;
}


void MetaClient::stop() {
    if (bgThread_ != nullptr) {
        bgThread_->stop();
        bgThread_->wait();
        bgThread_.reset();
    }
    isRunning_ = false;
}


void MetaClient::heartBeatThreadFunc() {
    SCOPE_EXIT {
        bgThread_->addDelayTask(FLAGS_heartbeat_interval_secs * 1000,
                                &MetaClient::heartBeatThreadFunc,
                                this);
    };
    auto ret = heartbeat().get();
    if (!ret.ok()) {
        LOG(ERROR) << "Heartbeat failed, status:" << ret.status();
        return;
    }

    // if MetaServer has some changes, refesh the localCache_
    if (localLastUpdateTime_ < metadLastUpdateTime_) {
        bool ldRet = loadData();
        bool lcRet = true;
        if (!options_.skipConfig_) {
            lcRet = loadCfg();
        }
        if (ldRet && lcRet) {
            localLastUpdateTime_ = metadLastUpdateTime_;
        }
    }
}


bool MetaClient::loadUsersAndRoles() {
    auto userRoleRet = listUsers().get();
    if (!userRoleRet.ok()) {
        LOG(ERROR) << "List users failed, status:" << userRoleRet.status();
        return false;
    }
    decltype(userRolesMap_)       userRolesMap;
    decltype(userPasswordMap_)    userPasswordMap;
    for (auto& user : userRoleRet.value()) {
        auto rolesRet = getUserRoles(user.first).get();
        if (!rolesRet.ok()) {
            LOG(ERROR) << "List role by user failed, user : " << user.first;
            return false;
        }
        userRolesMap[user.first] = rolesRet.value();
        userPasswordMap[user.first] = user.second;
    }
    {
        folly::RWSpinLock::WriteHolder holder(localCacheLock_);
        userRolesMap_ = std::move(userRolesMap);
        userPasswordMap_ = std::move(userPasswordMap);
    }
    return true;
}


bool MetaClient::loadData() {
    if (ioThreadPool_->numThreads() <= 0) {
        LOG(ERROR) << "The threads number in ioThreadPool should be greater than 0";
        return false;
    }

    if (!loadUsersAndRoles()) {
        LOG(ERROR) << "Load roles Failed";
        return false;
    }

    auto ret = listSpaces().get();
    if (!ret.ok()) {
        LOG(ERROR) << "List space failed, status:" << ret.status();
        return false;
    }

    decltype(localCache_)               cache;
    decltype(spaceIndexByName_)         spaceIndexByName;
    decltype(spaceTagIndexByName_)      spaceTagIndexByName;
    decltype(spaceEdgeIndexByName_)     spaceEdgeIndexByName;
    decltype(spaceNewestTagVerMap_)     spaceNewestTagVerMap;
    decltype(spaceNewestEdgeVerMap_)    spaceNewestEdgeVerMap;
    decltype(spaceEdgeIndexByType_)     spaceEdgeIndexByType;
    decltype(spaceTagIndexById_)        spaceTagIndexById;
    decltype(spaceAllEdgeMap_)          spaceAllEdgeMap;

    for (auto space : ret.value()) {
        auto spaceId = space.first;
        auto r = getPartsAlloc(spaceId).get();
        if (!r.ok()) {
            LOG(ERROR) << "Get parts allocation failed for spaceId " << spaceId
                       << ", status " << r.status();
            return false;
        }

        auto spaceCache = std::make_shared<SpaceInfoCache>();
        auto partsAlloc = r.value();
        spaceCache->spaceName = space.second;
        spaceCache->partsOnHost_ = reverse(partsAlloc);
        spaceCache->partsAlloc_ = std::move(partsAlloc);
        VLOG(2) << "Load space " << spaceId
                << ", parts num:" << spaceCache->partsAlloc_.size();

        // loadSchemas
        if (!loadSchemas(spaceId,
                         spaceCache,
                         spaceTagIndexByName,
                         spaceTagIndexById,
                         spaceEdgeIndexByName,
                         spaceEdgeIndexByType,
                         spaceNewestTagVerMap,
                         spaceNewestEdgeVerMap,
                         spaceAllEdgeMap)) {
            LOG(ERROR) << "Load Schemas Failed";
            return false;
        }

        if (!loadIndexes(spaceId, spaceCache)) {
            LOG(ERROR) << "Load Indexes Failed";
            return false;
        }

        // get space properties
        auto resp = getSpace(spaceCache->spaceName).get();
        if (!resp.ok()) {
            LOG(ERROR) << "Get space properties failed for space " << spaceId;
            return false;
        }
        const auto& properties = resp.value().get_properties();
        spaceCache->vertexIdLen_ = properties.get_vid_size();

        cache.emplace(spaceId, spaceCache);
        spaceIndexByName.emplace(space.second, spaceId);
    }

    decltype(localCache_) oldCache;
    {
        folly::RWSpinLock::WriteHolder holder(localCacheLock_);
        oldCache                = std::move(localCache_);
        localCache_             = std::move(cache);
        spaceIndexByName_       = std::move(spaceIndexByName);
        spaceTagIndexByName_    = std::move(spaceTagIndexByName);
        spaceEdgeIndexByName_   = std::move(spaceEdgeIndexByName);
        spaceNewestTagVerMap_   = std::move(spaceNewestTagVerMap);
        spaceNewestEdgeVerMap_  = std::move(spaceNewestEdgeVerMap);
        spaceEdgeIndexByType_   = std::move(spaceEdgeIndexByType);
        spaceTagIndexById_      = std::move(spaceTagIndexById);
        spaceAllEdgeMap_        = std::move(spaceAllEdgeMap);
    }

    diff(oldCache, localCache_);
    ready_ = true;
    return true;
}


bool MetaClient::loadSchemas(GraphSpaceID spaceId,
                             std::shared_ptr<SpaceInfoCache> spaceInfoCache,
                             SpaceTagNameIdMap &tagNameIdMap,
                             SpaceTagIdNameMap &tagIdNameMap,
                             SpaceEdgeNameTypeMap &edgeNameTypeMap,
                             SpaceEdgeTypeNameMap &edgeTypeNameMap,
                             SpaceNewestTagVerMap &newestTagVerMap,
                             SpaceNewestEdgeVerMap &newestEdgeVerMap,
                             SpaceAllEdgeMap &allEdgeMap) {
    auto tagRet = listTagSchemas(spaceId).get();
    if (!tagRet.ok()) {
        LOG(ERROR) << "Get tag schemas failed for spaceId " << spaceId
                   << ", " << tagRet.status();
        return false;
    }

    auto edgeRet = listEdgeSchemas(spaceId).get();
    if (!edgeRet.ok()) {
        LOG(ERROR) << "Get edge schemas failed for spaceId " << spaceId
                   << ", " << edgeRet.status();
        return false;
    }

    auto tagItemVec = tagRet.value();
    auto edgeItemVec = edgeRet.value();
    TagSchemas tagSchemas;
    EdgeSchemas edgeSchemas;
    TagID lastTagId = -1;
    for (auto& tagIt : tagItemVec) {
        // meta will return the different version from new to old
        auto schema = std::make_shared<NebulaSchemaProvider>(tagIt.version);
        for (auto colIt : tagIt.schema.get_columns()) {
            bool hasDef = colIt.__isset.default_value;
            size_t len = colIt.__isset.type_length ? *colIt.get_type_length() : 0;
            bool nullable = colIt.__isset.nullable ? *colIt.get_nullable() : false;
            schema->addField(colIt.get_name(),
                             colIt.get_type(),
                             len,
                             nullable,
                             hasDef ? *colIt.get_default_value() : Value());
        }
        // handle schema property
        schema->setProp(tagIt.schema.get_schema_prop());
        if (tagIt.tag_id != lastTagId) {
            // init schema vector, since schema version is zero-based, need to add one
            tagSchemas[tagIt.tag_id].resize(schema->getVersion() + 1);
            lastTagId = tagIt.tag_id;
        }
        tagSchemas[tagIt.tag_id][schema->getVersion()] = std::move(schema);
        tagNameIdMap.emplace(std::make_pair(spaceId, tagIt.tag_name), tagIt.tag_id);
        tagIdNameMap.emplace(std::make_pair(spaceId, tagIt.tag_id), tagIt.tag_name);
        // get the latest tag version
        auto it = newestTagVerMap.find(std::make_pair(spaceId, tagIt.tag_id));
        if (it != newestTagVerMap.end()) {
            if (it->second < tagIt.version) {
                it->second = tagIt.version;
            }
        } else {
            newestTagVerMap.emplace(std::make_pair(spaceId, tagIt.tag_id), tagIt.version);
        }
        VLOG(3) << "Load Tag Schema Space " << spaceId
                << ", ID " << tagIt.tag_id
                << ", Name " << tagIt.tag_name
                << ", Version " << tagIt.version << " Successfully!";
    }

    std::unordered_set<std::pair<GraphSpaceID, EdgeType>> edges;
    EdgeType lastEdgeType = -1;
    for (auto& edgeIt : edgeItemVec) {
        // meta will return the different version from new to old
        auto schema = std::make_shared<NebulaSchemaProvider>(edgeIt.version);
        for (auto col : edgeIt.schema.get_columns()) {
            bool hasDef = col.__isset.default_value;
            size_t len = col.__isset.type_length ? *col.get_type_length() : 0;
            bool nullable = col.__isset.nullable ? *col.get_nullable() : false;
            schema->addField(col.get_name(),
                             col.get_type(),
                             len,
                             nullable,
                             hasDef ? *col.get_default_value() : Value());
        }
        // handle shcem property
        schema->setProp(edgeIt.schema.get_schema_prop());
        if (edgeIt.edge_type != lastEdgeType) {
            // init schema vector, since schema version is zero-based, need to add one
            edgeSchemas[edgeIt.edge_type].resize(schema->getVersion() + 1);
            lastTagId = edgeIt.edge_type;
        }
        edgeSchemas[edgeIt.edge_type][schema->getVersion()] = std::move(schema);
        edgeNameTypeMap.emplace(std::make_pair(spaceId, edgeIt.edge_name), edgeIt.edge_type);
        edgeTypeNameMap.emplace(std::make_pair(spaceId, edgeIt.edge_type), edgeIt.edge_name);
        auto it = allEdgeMap.find(spaceId);
        if (it == allEdgeMap.end()) {
            std::vector<std::string> v = {edgeIt.edge_name};
            allEdgeMap.emplace(spaceId, std::move(v));
            edges.emplace(spaceId, edgeIt.edge_type);
        } else {
            if (edges.find({spaceId, edgeIt.edge_type}) != edges.cend()) {
                continue;
            }
            edges.emplace(spaceId, edgeIt.edge_type);
            it->second.emplace_back(edgeIt.edge_name);
        }
        // get the latest edge version
        auto it2 = newestEdgeVerMap.find(std::make_pair(spaceId, edgeIt.edge_type));
        if (it2 != newestEdgeVerMap.end()) {
            if (it2->second < edgeIt.version) {
                it2->second = edgeIt.version;
            }
        } else {
            newestEdgeVerMap.emplace(std::make_pair(spaceId, edgeIt.edge_type),
                                     edgeIt.version);
        }
        VLOG(3) << "Load Edge Schema Space " << spaceId
                << ", Type " << edgeIt.edge_type
                << ", Name " << edgeIt.edge_name
                << ", Version " << edgeIt.version
                << " Successfully!";
    }

    spaceInfoCache->tagSchemas_ = std::move(tagSchemas);
    spaceInfoCache->edgeSchemas_ = std::move(edgeSchemas);
    return true;
}


bool MetaClient::loadIndexes(GraphSpaceID spaceId,
                             std::shared_ptr<SpaceInfoCache> cache) {
    auto tagIndexesRet = listTagIndexes(spaceId).get();
    if (!tagIndexesRet.ok()) {
        LOG(ERROR) << "Get tag indexes failed for spaceId " << spaceId
                   << ", " << tagIndexesRet.status();
        return false;
    }

    auto edgeIndexesRet = listEdgeIndexes(spaceId).get();
    if (!edgeIndexesRet.ok()) {
        LOG(ERROR) << "Get edge indexes failed for spaceId " << spaceId
                   << ", " << edgeIndexesRet.status();
        return false;
    }

    Indexes tagIndexes;
    for (auto tagIndex : tagIndexesRet.value()) {
        auto indexName = tagIndex.get_index_name();
        auto indexID = tagIndex.get_index_id();
        std::pair<GraphSpaceID, std::string> pair(spaceId, indexName);
        tagNameIndexMap_.emplace(std::move(pair), indexID);
        auto tagIndexPtr = std::make_shared<cpp2::IndexItem>(tagIndex);
        tagIndexes.emplace(indexID, tagIndexPtr);
    }
    cache->tagIndexes_ = std::move(tagIndexes);

    Indexes edgeIndexes;
    for (auto& edgeIndex : edgeIndexesRet.value()) {
        auto indexName = edgeIndex.get_index_name();
        auto indexID = edgeIndex.get_index_id();
        std::pair<GraphSpaceID, std::string> pair(spaceId, indexName);
        edgeNameIndexMap_.emplace(std::move(pair), indexID);
        auto edgeIndexPtr = std::make_shared<cpp2::IndexItem>(edgeIndex);
        edgeIndexes.emplace(indexID, edgeIndexPtr);
    }
    cache->edgeIndexes_ = std::move(edgeIndexes);
    return true;
}


Status MetaClient::checkTagIndexed(GraphSpaceID space, TagID tagID) {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = localCache_.find(space);
    if (it != localCache_.end()) {
        auto tagIt = it->second->tagIndexes_.find(tagID);
        if (tagIt != it->second->tagIndexes_.end()) {
            return Status::OK();
        } else {
            return Status::IndexNotFound();
        }
    }
    return Status::SpaceNotFound();
}


Status MetaClient::checkEdgeIndexed(GraphSpaceID space, EdgeType edgeType) {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = localCache_.find(space);
    if (it != localCache_.end()) {
        auto edgeIt = it->second->edgeIndexes_.find(edgeType);
        if (edgeIt != it->second->edgeIndexes_.end()) {
            return Status::OK();
        } else {
            return Status::IndexNotFound();
        }
    }
    return Status::SpaceNotFound();
}


std::unordered_map<HostAddr, std::vector<PartitionID>>
MetaClient::reverse(const PartsAlloc& parts) {
    std::unordered_map<HostAddr, std::vector<PartitionID>> hosts;
    for (auto& partHost : parts) {
        for (auto& h : partHost.second) {
            hosts[h].emplace_back(partHost.first);
        }
    }
    return hosts;
}


template<typename Request,
         typename RemoteFunc,
         typename RespGenerator,
         typename RpcResponse,
         typename Response>
void MetaClient::getResponse(Request req,
                             RemoteFunc remoteFunc,
                             RespGenerator respGen,
                             folly::Promise<StatusOr<Response>> pro,
                             bool toLeader,
                             int32_t retry,
                             int32_t retryLimit) {
    auto* evb = ioThreadPool_->getEventBase();
    HostAddr host;
    {
        folly::RWSpinLock::ReadHolder holder(&hostLock_);
        host = toLeader ? leader_ : active_;
    }
    folly::via(evb, [host,
                     evb,
                     req = std::move(req),
                     remoteFunc = std::move(remoteFunc),
                     respGen = std::move(respGen),
                     pro = std::move(pro),
                     toLeader,
                     retry,
                     retryLimit,
                     this] () mutable {
        auto client = clientsMan_->client(host, evb, false, FLAGS_meta_client_timeout_ms);
        VLOG(1) << "Send request to meta " << host;
        remoteFunc(client, req)
            .via(evb)
            .then([host,
                   req = std::move(req),
                   remoteFunc = std::move(remoteFunc),
                   respGen = std::move(respGen),
                   pro = std::move(pro),
                   toLeader,
                   retry,
                   retryLimit,
                   evb,
                   this] (folly::Try<RpcResponse>&& t) mutable {
            // exception occurred during RPC
            if (t.hasException()) {
                if (toLeader) {
                    updateLeader();
                } else {
                    updateActive();
                }
                if (retry < retryLimit) {
                    evb->runAfterDelay([req = std::move(req),
                                        remoteFunc = std::move(remoteFunc),
                                        respGen = std::move(respGen),
                                        pro = std::move(pro),
                                        toLeader,
                                        retry,
                                        retryLimit,
                                        this] () mutable {
                        getResponse(std::move(req),
                                    std::move(remoteFunc),
                                    std::move(respGen),
                                    std::move(pro),
                                    toLeader,
                                    retry + 1,
                                    retryLimit);
                    }, FLAGS_meta_client_retry_interval_secs * 1000);
                    return;
                } else {
                    LOG(ERROR) << "Send request to " << host << ", exceed retry limit";
                    pro.setValue(
                        Status::Error(folly::stringPrintf("RPC failure in MetaClient: %s",
                                                          t.exception().what().c_str())));
                }
                return;
            }

            auto&& resp = t.value();
            if (resp.code == cpp2::ErrorCode::SUCCEEDED) {
                // succeeded
                pro.setValue(respGen(std::move(resp)));

                return;
            } else if (resp.code == cpp2::ErrorCode::E_LEADER_CHANGED) {
                updateLeader(resp.get_leader());
                if (retry < retryLimit) {
                    evb->runAfterDelay([req = std::move(req),
                                        remoteFunc = std::move(remoteFunc),
                                        respGen = std::move(respGen),
                                        pro = std::move(pro),
                                        toLeader,
                                        retry,
                                        retryLimit,
                                        this] () mutable {
                        getResponse(std::move(req),
                                    std::move(remoteFunc),
                                    std::move(respGen),
                                    std::move(pro),
                                    toLeader,
                                    retry + 1,
                                    retryLimit);
                    }, FLAGS_meta_client_retry_interval_secs * 1000);
                    return;
                }
            }
            pro.setValue(this->handleResponse(resp));
        });  // then
    });  // via
}


std::vector<SpaceIdName>
MetaClient::toSpaceIdName(const std::vector<cpp2::IdName>& tIdNames) {
    std::vector<SpaceIdName> idNames;
    idNames.resize(tIdNames.size());
    std::transform(tIdNames.begin(), tIdNames.end(), idNames.begin(), [](const auto& tin) {
        return SpaceIdName(tin.id.get_space_id(), tin.name);
    });
    return idNames;
}


template<typename RESP>
Status MetaClient::handleResponse(const RESP& resp) {
    switch (resp.get_code()) {
        case cpp2::ErrorCode::SUCCEEDED:
            return Status::OK();
        case cpp2::ErrorCode::E_EXISTED:
            return Status::Error("existed!");
        case cpp2::ErrorCode::E_NOT_FOUND:
            return Status::Error("not existed!");
        case cpp2::ErrorCode::E_NO_HOSTS:
            return Status::Error("no hosts!");
        case cpp2::ErrorCode::E_CONFIG_IMMUTABLE:
            return Status::Error("Config immutable");
        case cpp2::ErrorCode::E_CONFLICT:
            return Status::Error("conflict!");
        case cpp2::ErrorCode::E_WRONGCLUSTER:
            return Status::Error("wrong cluster!");
        case cpp2::ErrorCode::E_LEADER_CHANGED:
            return Status::LeaderChanged("Leader changed!");
        case cpp2::ErrorCode::E_BALANCED:
            return Status::Error("The cluster is balanced!");
        case cpp2::ErrorCode::E_BALANCER_RUNNING:
            return Status::Error("The balancer is running!");
        case cpp2::ErrorCode::E_BAD_BALANCE_PLAN:
            return Status::Error("Bad balance plan!");
        case cpp2::ErrorCode::E_NO_RUNNING_BALANCE_PLAN:
            return Status::Error("No running balance plan!");
        case cpp2::ErrorCode::E_NO_VALID_HOST:
            return Status::Error("No valid host hold the partition");
        case cpp2::ErrorCode::E_CORRUPTTED_BALANCE_PLAN:
            return Status::Error("No corrupted blance plan");
        case cpp2::ErrorCode::E_INVALID_PARTITION_NUM:
            return Status::Error("No valid partition_num");
        case cpp2::ErrorCode::E_INVALID_REPLICA_FACTOR:
            return Status::Error("No valid replica_factor");
        case cpp2::ErrorCode::E_INVALID_CHARSET:
            return Status::Error("No valid charset");
        case cpp2::ErrorCode::E_INVALID_COLLATE:
            return Status::Error("No valid collate");
        case cpp2::ErrorCode::E_CHARSET_COLLATE_NOT_MATCH:
            return Status::Error("Charset and collate not match");
        case cpp2::ErrorCode::E_INVALID_PASSWORD:
            return Status::Error("Invalid password");
        case cpp2::ErrorCode::E_IMPROPER_ROLE:
            return Status::Error("Improper role");
        default:
            return Status::Error("Unknown code %d", static_cast<int32_t>(resp.get_code()));
    }
}


PartsMap MetaClient::doGetPartsMap(const HostAddr& host,
                                   const LocalCache& localCache) {
    PartsMap partMap;
    for (auto it = localCache.begin(); it != localCache.end(); it++) {
        auto spaceId = it->first;
        auto& cache = it->second;
        auto partsIt = cache->partsOnHost_.find(host);
        if (partsIt != cache->partsOnHost_.end()) {
            for (auto& partId : partsIt->second) {
                auto partAllocIter = cache->partsAlloc_.find(partId);
                CHECK(partAllocIter != cache->partsAlloc_.end());
                auto& partM = partMap[spaceId][partId];
                partM.spaceId_ = spaceId;
                partM.partId_  = partId;
                partM.hosts_   = partAllocIter->second;
            }
        }
    }
    return partMap;
}


void MetaClient::diff(const LocalCache& oldCache, const LocalCache& newCache) {
    folly::RWSpinLock::WriteHolder holder(listenerLock_);
    if (listener_ == nullptr) {
        VLOG(3) << "Listener is null!";
        return;
    }
    auto newPartsMap = doGetPartsMap(options_.localHost_, newCache);
    auto oldPartsMap = doGetPartsMap(options_.localHost_, oldCache);
    VLOG(1) << "Let's check if any new parts added/updated for " << options_.localHost_;
    for (auto it = newPartsMap.begin(); it != newPartsMap.end(); it++) {
        auto spaceId = it->first;
        const auto& newParts = it->second;
        auto oldIt = oldPartsMap.find(spaceId);
        if (oldIt == oldPartsMap.end()) {
            VLOG(1) << "SpaceId " << spaceId << " was added!";
            listener_->onSpaceAdded(spaceId);
            for (auto partIt = newParts.begin(); partIt != newParts.end(); partIt++) {
                listener_->onPartAdded(partIt->second);
            }
        } else {
            const auto& oldParts = oldIt->second;
            for (auto partIt = newParts.begin(); partIt != newParts.end(); partIt++) {
                auto oldPartIt = oldParts.find(partIt->first);
                if (oldPartIt == oldParts.end()) {
                    VLOG(1) << "SpaceId " << spaceId << ", partId "
                            << partIt->first << " was added!";
                    listener_->onPartAdded(partIt->second);
                } else {
                    const auto& oldPartHosts = oldPartIt->second;
                    const auto& newPartHosts = partIt->second;
                    if (oldPartHosts != newPartHosts) {
                        VLOG(1) << "SpaceId " << spaceId
                                << ", partId " << partIt->first << " was updated!";
                        listener_->onPartUpdated(newPartHosts);
                    }
                }
            }
        }
    }
    VLOG(1) << "Let's check if any old parts removed....";
    for (auto it = oldPartsMap.begin(); it != oldPartsMap.end(); it++) {
        auto spaceId = it->first;
        const auto& oldParts = it->second;
        auto newIt = newPartsMap.find(spaceId);
        if (newIt == newPartsMap.end()) {
            VLOG(1) << "SpaceId " << spaceId << " was removed!";
            for (auto partIt = oldParts.begin(); partIt != oldParts.end(); partIt++) {
                listener_->onPartRemoved(spaceId, partIt->first);
            }
            listener_->onSpaceRemoved(spaceId);
        } else {
            const auto& newParts = newIt->second;
            for (auto partIt = oldParts.begin(); partIt != oldParts.end(); partIt++) {
                auto newPartIt = newParts.find(partIt->first);
                if (newPartIt == newParts.end()) {
                    VLOG(1) << "SpaceId " << spaceId
                            << ", partId " << partIt->first << " was removed!";
                    listener_->onPartRemoved(spaceId, partIt->first);
                }
            }
        }
    }
}


/// ================================== public methods =================================

StatusOr<PartitionID> MetaClient::partId(GraphSpaceID spaceId, const VertexID id) const {
    auto status = partsNum(spaceId);
    if (!status.ok()) {
        return Status::Error("Space not found, spaceid: %d", spaceId);
    }

    auto numParts = status.value();
    // If the length of the id is 8, we will treat it as int64_t to be compatible
    // with the version 1.0
    uint64_t vid = 0;
    if (id.size() == 8) {
        memcpy(static_cast<void*>(&vid), id.data(), 8);
    } else {
        vid = folly::hash::fnv64_buf(id.data(), id.size());
    }
    PartitionID pId = vid % numParts + 1;
    CHECK_GT(pId, 0U);
    return pId;
}


folly::Future<StatusOr<cpp2::AdminJobResult>>
MetaClient::submitJob(cpp2::AdminJobOp op, std::vector<std::string> paras) {
    cpp2::AdminJobReq req;
    req.set_op(op);
    req.set_paras(std::move(paras));
    folly::Promise<StatusOr<cpp2::AdminJobResult>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req), [] (auto client, auto request) {
                    return client->future_runAdminJob(request);
                }, [] (cpp2::AdminJobResp&& resp) -> decltype(auto) {
                    return resp.get_result();
                }, std::move(promise));
    return future;
}


folly::Future<StatusOr<GraphSpaceID>> MetaClient::createSpace(SpaceDesc spaceDesc,
                                                              bool ifNotExists) {
    cpp2::SpaceProperties properties;
    properties.set_space_name(std::move(spaceDesc.spaceName_));
    properties.set_partition_num(spaceDesc.partNum_);
    properties.set_replica_factor(spaceDesc.replicaFactor_);
    properties.set_vid_size(spaceDesc.vidSize_);
    properties.set_charset_name(std::move(spaceDesc.charsetName_));
    properties.set_collate_name(std::move(spaceDesc.collationName_));

    cpp2::CreateSpaceReq req;
    req.set_properties(std::move(properties));
    req.set_if_not_exists(ifNotExists);
    folly::Promise<StatusOr<GraphSpaceID>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createSpace(request);
                },
                [] (cpp2::ExecResp&& resp) -> GraphSpaceID {
                    return resp.get_id().get_space_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<SpaceIdName>>> MetaClient::listSpaces() {
    cpp2::ListSpacesReq req;
    folly::Promise<StatusOr<std::vector<SpaceIdName>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listSpaces(request);
                },
                [this] (cpp2::ListSpacesResp&& resp) -> decltype(auto) {
                    return this->toSpaceIdName(resp.get_spaces());
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<cpp2::SpaceItem>> MetaClient::getSpace(std::string name) {
    cpp2::GetSpaceReq req;
    req.set_space_name(std::move(name));
    folly::Promise<StatusOr<cpp2::SpaceItem>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req), [] (auto client, auto request) {
                    return client->future_getSpace(request);
                }, [] (cpp2::GetSpaceResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_item();
                }, std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>> MetaClient::dropSpace(std::string name,
                                                    const bool ifExists) {
    cpp2::DropSpaceReq req;
    req.set_space_name(std::move(name));
    req.set_if_exists(ifExists);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropSpace(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::HostItem>>> MetaClient::listHosts() {
    cpp2::ListHostsReq req;
    folly::Promise<StatusOr<std::vector<cpp2::HostItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listHosts(request);
                },
                [] (cpp2::ListHostsResp&& resp) -> decltype(auto) {
                    return resp.hosts;
                },
                std::move(promise));
    return future;
}

folly::Future<StatusOr<std::vector<cpp2::PartItem>>>
MetaClient::listParts(GraphSpaceID spaceId, std::vector<PartitionID> partIds) {
    cpp2::ListPartsReq req;
    req.set_space_id(std::move(spaceId));
    req.set_part_ids(std::move(partIds));
    folly::Promise<StatusOr<std::vector<cpp2::PartItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listParts(request);
                },
                [] (cpp2::ListPartsResp&& resp) -> decltype(auto) {
                    return resp.parts;
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::unordered_map<PartitionID, std::vector<HostAddr>>>>
MetaClient::getPartsAlloc(GraphSpaceID spaceId) {
    cpp2::GetPartsAllocReq req;
    req.set_space_id(spaceId);
    folly::Promise<StatusOr<std::unordered_map<PartitionID, std::vector<HostAddr>>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getPartsAlloc(request);
                },
                [] (cpp2::GetPartsAllocResp&& resp) -> decltype(auto) {
                    std::unordered_map<PartitionID, std::vector<HostAddr>> parts;
                    for (auto it = resp.parts.begin(); it != resp.parts.end(); it++) {
                        parts.emplace(it->first, it->second);
                    }
                    return parts;
                },
                std::move(promise));
    return future;
}


StatusOr<GraphSpaceID>
MetaClient::getSpaceIdByNameFromCache(const std::string& name) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceIndexByName_.find(name);
    if (it != spaceIndexByName_.end()) {
        return it->second;
    }
    return Status::SpaceNotFound();
}


StatusOr<TagID> MetaClient::getTagIDByNameFromCache(const GraphSpaceID& space,
                                                    const std::string& name) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceTagIndexByName_.find(std::make_pair(space, name));
    if (it == spaceTagIndexByName_.end()) {
        std::string error = folly::stringPrintf("TagName `%s'  is nonexistent",
                                                name.c_str());
        return Status::Error(std::move(error));
    }
    return it->second;
}


StatusOr<std::string> MetaClient::getTagNameByIdFromCache(const GraphSpaceID& space,
                                                          const TagID& tagId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceTagIndexById_.find(std::make_pair(space, tagId));
    if (it == spaceTagIndexById_.end()) {
        std::string error = folly::stringPrintf("TagID `%d'  is nonexistent", tagId);
        return Status::Error(std::move(error));
    }
    return it->second;
}


StatusOr<EdgeType> MetaClient::getEdgeTypeByNameFromCache(const GraphSpaceID& space,
                                                          const std::string& name) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceEdgeIndexByName_.find(std::make_pair(space, name));
    if (it == spaceEdgeIndexByName_.end()) {
        std::string error = folly::stringPrintf("EdgeName `%s'  is nonexistent",
                                                name.c_str());
        return Status::Error(std::move(error));
    }
    return it->second;
}


StatusOr<std::string> MetaClient::getEdgeNameByTypeFromCache(const GraphSpaceID& space,
                                                             const EdgeType edgeType) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceEdgeIndexByType_.find(std::make_pair(space, edgeType));
    if (it == spaceEdgeIndexByType_.end()) {
        std::string error = folly::stringPrintf("EdgeType `%d'  is nonexistent", edgeType);
        return Status::Error(std::move(error));
    }
    return it->second;
}


StatusOr<std::vector<std::string>>
MetaClient::getAllEdgeFromCache(const GraphSpaceID& space) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceAllEdgeMap_.find(space);
    if (it == spaceAllEdgeMap_.end()) {
        std::string error = folly::stringPrintf("SpaceId `%d'  is nonexistent", space);
        return Status::Error(std::move(error));
    }
    return it->second;
}


folly::Future<StatusOr<bool>>
MetaClient::multiPut(std::string segment,
                     std::vector<std::pair<std::string, std::string>> pairs) {
    if (!nebula::meta::checkSegment(segment) || pairs.empty()) {
        return Status::Error("arguments invalid!");
    }

    cpp2::MultiPutReq req;
    std::vector<nebula::KeyValue> data;
    for (auto& element : pairs) {
        data.emplace_back(std::move(element));
    }
    req.set_segment(std::move(segment));
    req.set_pairs(std::move(data));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_multiPut(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::string>>
MetaClient::get(std::string segment, std::string key) {
    if (!nebula::meta::checkSegment(segment) || key.empty()) {
        return Status::Error("arguments invalid!");
    }

    cpp2::GetReq req;
    req.set_segment(std::move(segment));
    req.set_key(std::move(key));
    folly::Promise<StatusOr<std::string>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_get(request);
                },
                [] (cpp2::GetResp&& resp) -> std::string {
                    return resp.get_value();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::vector<std::string>>>
MetaClient::multiGet(std::string segment, std::vector<std::string> keys) {
    if (!nebula::meta::checkSegment(segment) || keys.empty()) {
        return Status::Error("arguments invalid!");
    }

    cpp2::MultiGetReq req;
    req.set_segment(std::move(segment));
    req.set_keys(std::move(keys));
    folly::Promise<StatusOr<std::vector<std::string>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_multiGet(request);
                },
                [] (cpp2::MultiGetResp&& resp) -> std::vector<std::string> {
                    return resp.get_values();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::vector<std::string>>>
MetaClient::scan(std::string segment, std::string start, std::string end) {
    if (!nebula::meta::checkSegment(segment) || start.empty() || end.empty()) {
        return Status::Error("arguments invalid!");
    }

    cpp2::ScanReq req;
    req.set_segment(std::move(segment));
    req.set_start(std::move(start));
    req.set_end(std::move(end));
    folly::Promise<StatusOr<std::vector<std::string>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_scan(request);
                },
                [] (cpp2::ScanResp&& resp) -> std::vector<std::string> {
                    return resp.get_values();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::remove(std::string segment, std::string key) {
    if (!nebula::meta::checkSegment(segment) || key.empty()) {
        return Status::Error("arguments invalid!");
    }

    cpp2::RemoveReq req;
    req.set_segment(std::move(segment));
    req.set_key(std::move(key));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_remove(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::removeRange(std::string segment, std::string start, std::string end) {
    if (!nebula::meta::checkSegment(segment) || start.empty() || end.empty()) {
        return Status::Error("arguments invalid!");
    }

    cpp2::RemoveRangeReq req;
    req.set_segment(std::move(segment));
    req.set_start(std::move(start));
    req.set_end(std::move(end));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_removeRange(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


PartsMap MetaClient::getPartsMapFromCache(const HostAddr& host) {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    return doGetPartsMap(host, localCache_);
}


StatusOr<PartHosts> MetaClient::getPartHostsFromCache(GraphSpaceID spaceId,
                                                      PartitionID partId) {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = localCache_.find(spaceId);
    if (it == localCache_.end()) {
        return Status::Error("Space not found, spaceid: %d", spaceId);
    }
    auto& cache = it->second;
    auto partAllocIter = cache->partsAlloc_.find(partId);
    if (partAllocIter == cache->partsAlloc_.end()) {
        return Status::Error("Part not found in cache, spaceid: %d, partid: %d",
                             spaceId,
                             partId);
    }
    PartHosts ph;
    ph.spaceId_ = spaceId;
    ph.partId_  = partId;
    ph.hosts_   = partAllocIter->second;
    return ph;
}


Status MetaClient::checkPartExistInCache(const HostAddr& host,
                                         GraphSpaceID spaceId,
                                         PartitionID partId) {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = localCache_.find(spaceId);
    if (it != localCache_.end()) {
        auto partsIt = it->second->partsOnHost_.find(host);
        if (partsIt != it->second->partsOnHost_.end()) {
            for (auto& pId : partsIt->second) {
                if (pId == partId) {
                    return Status::OK();
                }
            }
        } else {
            return Status::PartNotFound();
        }
    }
    return Status::SpaceNotFound();
}


Status MetaClient::checkSpaceExistInCache(const HostAddr& host,
                                        GraphSpaceID spaceId) {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = localCache_.find(spaceId);
    if (it != localCache_.end()) {
        auto partsIt = it->second->partsOnHost_.find(host);
        if (partsIt != it->second->partsOnHost_.end() && !partsIt->second.empty()) {
            return Status::OK();
        } else {
            return Status::PartNotFound();
        }
    }
    return Status::SpaceNotFound();
}


StatusOr<int32_t> MetaClient::partsNum(GraphSpaceID spaceId) const {
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = localCache_.find(spaceId);
    if (it == localCache_.end()) {
        return Status::Error("Space not found, spaceid: %d", spaceId);
    }
    return it->second->partsAlloc_.size();
}


folly::Future<StatusOr<TagID>> MetaClient::createTagSchema(GraphSpaceID spaceId,
                                                           std::string name,
                                                           cpp2::Schema schema,
                                                           bool ifNotExists) {
    cpp2::CreateTagReq req;
    req.set_space_id(std::move(spaceId));
    req.set_tag_name(std::move(name));
    req.set_schema(std::move(schema));
    req.set_if_not_exists(ifNotExists);
    folly::Promise<StatusOr<TagID>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createTag(request);
                },
                [] (cpp2::ExecResp&& resp) -> TagID {
                    return resp.get_id().get_tag_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<TagID>>
MetaClient::alterTagSchema(GraphSpaceID spaceId,
                           std::string name,
                           std::vector<cpp2::AlterSchemaItem> items,
                           cpp2::SchemaProp schemaProp) {
    cpp2::AlterTagReq req;
    req.set_space_id(std::move(spaceId));
    req.set_tag_name(std::move(name));
    req.set_tag_items(std::move(items));
    req.set_schema_prop(std::move(schemaProp));
    folly::Promise<StatusOr<TagID>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_alterTag(request);
                },
                [] (cpp2::ExecResp&& resp) -> TagID {
                    return resp.get_id().get_tag_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::TagItem>>>
MetaClient::listTagSchemas(GraphSpaceID spaceId) {
    cpp2::ListTagsReq req;
    req.set_space_id(std::move(spaceId));
    folly::Promise<StatusOr<std::vector<cpp2::TagItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listTags(request);
                },
                [] (cpp2::ListTagsResp&& resp) -> decltype(auto){
                    return std::move(resp).get_tags();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::dropTagSchema(int32_t spaceId, std::string tagName, const bool ifExists) {
    cpp2::DropTagReq req;
    req.set_space_id(spaceId);
    req.set_tag_name(std::move(tagName));
    req.set_if_exists(ifExists);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropTag(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<cpp2::Schema>>
MetaClient::getTagSchema(int32_t spaceId, std::string name, int64_t version) {
    cpp2::GetTagReq req;
    req.set_space_id(spaceId);
    req.set_tag_name(std::move(name));
    req.set_version(version);
    folly::Promise<StatusOr<cpp2::Schema>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getTag(request);
                },
                [] (cpp2::GetTagResp&& resp) -> cpp2::Schema {
                    return std::move(resp).get_schema();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<EdgeType>> MetaClient::createEdgeSchema(GraphSpaceID spaceId,
                                                               std::string name,
                                                               cpp2::Schema schema,
                                                               bool ifNotExists) {
    cpp2::CreateEdgeReq req;
    req.set_space_id(std::move(spaceId));
    req.set_edge_name(std::move(name));
    req.set_schema(schema);
    req.set_if_not_exists(ifNotExists);

    folly::Promise<StatusOr<EdgeType>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createEdge(request);
                },
                [] (cpp2::ExecResp&& resp) -> EdgeType {
                    return resp.get_id().get_edge_type();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::alterEdgeSchema(GraphSpaceID spaceId,
                            std::string name,
                            std::vector<cpp2::AlterSchemaItem> items,
                            cpp2::SchemaProp schemaProp) {
    cpp2::AlterEdgeReq req;
    req.set_space_id(std::move(spaceId));
    req.set_edge_name(std::move(name));
    req.set_edge_items(std::move(items));
    req.set_schema_prop(std::move(schemaProp));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_alterEdge(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::EdgeItem>>>
MetaClient::listEdgeSchemas(GraphSpaceID spaceId) {
    cpp2::ListEdgesReq req;
    req.set_space_id(std::move(spaceId));
    folly::Promise<StatusOr<std::vector<cpp2::EdgeItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listEdges(request);
                },
                [] (cpp2::ListEdgesResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_edges();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<cpp2::Schema>>
MetaClient::getEdgeSchema(GraphSpaceID spaceId, std::string name, SchemaVer version) {
    cpp2::GetEdgeReq req;
    req.set_space_id(std::move(spaceId));
    req.set_edge_name(std::move(name));
    req.set_version(version);
    folly::Promise<StatusOr<cpp2::Schema>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getEdge(request);
                },
                [] (cpp2::GetEdgeResp&& resp) -> cpp2::Schema {
                    return std::move(resp).get_schema();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::dropEdgeSchema(GraphSpaceID spaceId, std::string name, const bool ifExists) {
    cpp2::DropEdgeReq req;
    req.set_space_id(std::move(spaceId));
    req.set_edge_name(std::move(name));
    req.set_if_exists(ifExists);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropEdge(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<IndexID>>
MetaClient::createTagIndex(GraphSpaceID spaceID,
                           std::string  indexName,
                           std::string  tagName,
                           std::vector<std::string> fields,
                           bool ifNotExists) {
    cpp2::CreateTagIndexReq req;
    req.set_space_id(spaceID);
    req.set_index_name(std::move(indexName));
    req.set_tag_name(std::move(tagName));
    req.set_fields(std::move(fields));
    req.set_if_not_exists(ifNotExists);

    folly::Promise<StatusOr<IndexID>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createTagIndex(request);
                },
                [] (cpp2::ExecResp&& resp) -> IndexID {
                    return resp.get_id().get_index_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::dropTagIndex(GraphSpaceID spaceID, std::string name, bool ifExists) {
    cpp2::DropTagIndexReq req;
    req.set_space_id(std::move(spaceID));
    req.set_index_name(std::move(name));
    req.set_if_exists(ifExists);

    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropTagIndex(request);
                },
                [] (cpp2::ExecResp&& resp) -> IndexID {
                    return resp.get_id().get_index_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<cpp2::IndexItem>>
MetaClient::getTagIndex(GraphSpaceID spaceID, std::string name) {
    cpp2::GetTagIndexReq req;
    req.set_space_id(std::move(spaceID));
    req.set_index_name(std::move(name));

    folly::Promise<StatusOr<cpp2::IndexItem>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getTagIndex(request);
                },
                [] (cpp2::GetTagIndexResp&& resp) -> cpp2::IndexItem {
                    return std::move(resp).get_item();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::IndexItem>>>
MetaClient::listTagIndexes(GraphSpaceID spaceID) {
    cpp2::ListTagIndexesReq req;
    req.set_space_id(std::move(spaceID));

    folly::Promise<StatusOr<std::vector<cpp2::IndexItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listTagIndexes(request);
                },
                [] (cpp2::ListTagIndexesResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_items();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::rebuildTagIndex(GraphSpaceID spaceID,
                            std::string name,
                            bool isOffline) {
    cpp2::RebuildIndexReq req;
    req.set_space_id(spaceID);
    req.set_index_name(std::move(name));
    req.set_is_offline(isOffline);

    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_rebuildTagIndex(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::IndexStatus>>>
MetaClient::listTagIndexStatus(GraphSpaceID spaceID) {
    cpp2::ListIndexStatusReq req;
    req.set_space_id(spaceID);

    folly::Promise<StatusOr<std::vector<cpp2::IndexStatus>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listTagIndexStatus(request);
                },
                [] (cpp2::ListIndexStatusResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_statuses();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<IndexID>>
MetaClient::createEdgeIndex(GraphSpaceID spaceID,
                            std::string  indexName,
                            std::string  edgeName,
                            std::vector<std::string> fields,
                            bool ifNotExists) {
    cpp2::CreateEdgeIndexReq req;
    req.set_space_id(spaceID);
    req.set_index_name(std::move(indexName));
    req.set_edge_name(std::move(edgeName));
    req.set_fields(std::move(fields));
    req.set_if_not_exists(ifNotExists);

    folly::Promise<StatusOr<IndexID>> promise;
    auto future = promise.getFuture();

    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createEdgeIndex(request);
                },
                [] (cpp2::ExecResp&& resp) -> IndexID {
                    return resp.get_id().get_index_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::dropEdgeIndex(GraphSpaceID spaceID, std::string name, bool ifExists) {
    cpp2::DropEdgeIndexReq req;
    req.set_space_id(std::move(spaceID));
    req.set_index_name(std::move(name));
    req.set_if_exists(ifExists);

    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropEdgeIndex(request);
                },
                [] (cpp2::ExecResp&& resp) -> IndexID {
                    return resp.get_id().get_index_id();
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<cpp2::IndexItem>>
MetaClient::getEdgeIndex(GraphSpaceID spaceID, std::string name) {
    cpp2::GetEdgeIndexReq req;
    req.set_space_id(std::move(spaceID));
    req.set_index_name(std::move(name));

    folly::Promise<StatusOr<cpp2::IndexItem>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getEdgeIndex(request);
                },
                [] (cpp2::GetEdgeIndexResp&& resp) -> cpp2::IndexItem {
                    return std::move(resp).get_item();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::IndexItem>>>
MetaClient::listEdgeIndexes(GraphSpaceID spaceID) {
    cpp2::ListEdgeIndexesReq req;
    req.set_space_id(std::move(spaceID));

    folly::Promise<StatusOr<std::vector<cpp2::IndexItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listEdgeIndexes(request);
                }, [] (cpp2::ListEdgeIndexesResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_items();
                },
                std::move(promise));
    return future;
}

StatusOr<int32_t> MetaClient::getSpaceVidLen(const GraphSpaceID& spaceId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        LOG(ERROR) << "Space " << spaceId << " not found!";
        return Status::Error(folly::stringPrintf("Space %d not found", spaceId));
    }
    auto vIdLen = spaceIt->second->vertexIdLen_;
    if (vIdLen <= 0) {
        return Status::Error(folly::stringPrintf("Space %d vertexId length invalid", spaceId));
    }
    return vIdLen;
}

StatusOr<std::shared_ptr<const NebulaSchemaProvider>>
MetaClient::getTagSchemaFromCache(GraphSpaceID spaceId, TagID tagID, SchemaVer ver) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        LOG(ERROR) << "Space " << spaceId << " not found!";
        return std::shared_ptr<const NebulaSchemaProvider>();
    } else {
        auto tagIt = spaceIt->second->tagSchemas_.find(tagID);
        if (tagIt == spaceIt->second->tagSchemas_.end() ||
            tagIt->second.size() <= static_cast<size_t>(ver)) {
            return std::shared_ptr<const NebulaSchemaProvider>();
        } else {
            return tagIt->second[ver];
        }
    }
}


StatusOr<std::shared_ptr<const NebulaSchemaProvider>>
MetaClient::getEdgeSchemaFromCache(GraphSpaceID spaceId, EdgeType edgeType, SchemaVer ver) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        LOG(ERROR) << "Space " << spaceId << " not found!";
        return std::shared_ptr<const NebulaSchemaProvider>();
    } else {
        auto edgeIt = spaceIt->second->edgeSchemas_.find(edgeType);
        if (edgeIt == spaceIt->second->edgeSchemas_.end() ||
            edgeIt->second.size() <= static_cast<size_t>(ver)) {
            LOG(ERROR) << "Space " << spaceId << ", EdgeType " << edgeType << ", version "
                       << ver << " not found!";
            return std::shared_ptr<const NebulaSchemaProvider>();
        } else {
            return edgeIt->second[ver];
        }
    }
}


StatusOr<TagSchemas> MetaClient::getAllVerTagSchema(GraphSpaceID spaceId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto iter = localCache_.find(spaceId);
    if (iter == localCache_.end()) {
        return Status::Error(folly::stringPrintf("Space not %d found", spaceId));
    }
    return iter->second->tagSchemas_;
}


StatusOr<EdgeSchemas> MetaClient::getAllVerEdgeSchema(GraphSpaceID spaceId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto iter = localCache_.find(spaceId);
    if (iter == localCache_.end()) {
        return Status::Error(folly::stringPrintf("Space not %d found", spaceId));
    }
    return iter->second->edgeSchemas_;
}


folly::Future<StatusOr<bool>>
MetaClient::rebuildEdgeIndex(GraphSpaceID spaceID,
                             std::string name,
                             bool isOffline) {
    cpp2::RebuildIndexReq req;
    req.set_space_id(spaceID);
    req.set_index_name(std::move(name));
    req.set_is_offline(isOffline);

    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_rebuildEdgeIndex(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::IndexStatus>>>
MetaClient::listEdgeIndexStatus(GraphSpaceID spaceID) {
    cpp2::ListIndexStatusReq req;
    req.set_space_id(spaceID);

    folly::Promise<StatusOr<std::vector<cpp2::IndexStatus>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listEdgeIndexStatus(request);
                },
                [] (cpp2::ListIndexStatusResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_statuses();
                },
                std::move(promise));
    return future;
}

StatusOr<std::shared_ptr<cpp2::IndexItem>>
MetaClient::getTagIndexByNameFromCache(const GraphSpaceID space, const std::string& name) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    std::pair<GraphSpaceID, std::string> key(space, name);
    auto iter = tagNameIndexMap_.find(key);
    if (iter == tagNameIndexMap_.end()) {
        return Status::IndexNotFound();
    }
    auto indexID = iter->second;
    auto itemStatus = getTagIndexFromCache(space, indexID);
    if (!itemStatus.ok()) {
        return itemStatus.status();
    }
    return itemStatus.value();
}


StatusOr<std::shared_ptr<cpp2::IndexItem>>
MetaClient::getEdgeIndexByNameFromCache(const GraphSpaceID space, const std::string& name) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    std::pair<GraphSpaceID, std::string> key(space, name);
    auto iter = edgeNameIndexMap_.find(key);
    if (iter == edgeNameIndexMap_.end()) {
        return Status::IndexNotFound();
    }
    auto indexID = iter->second;
    auto itemStatus = getEdgeIndexFromCache(space, indexID);
    if (!itemStatus.ok()) {
        return itemStatus.status();
    }
    return itemStatus.value();
}


StatusOr<std::shared_ptr<cpp2::IndexItem>>
MetaClient::getTagIndexFromCache(GraphSpaceID spaceId, IndexID indexID) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        LOG(ERROR) << "Space " << spaceId << " not found!";
        return Status::SpaceNotFound();
    } else {
        auto iter = spaceIt->second->tagIndexes_.find(indexID);
        if (iter == spaceIt->second->tagIndexes_.end()) {
            LOG(ERROR) << "Space " << spaceId << ", Tag Index " << indexID << " not found!";
            return Status::IndexNotFound();
        } else {
            return iter->second;
        }
    }
}


StatusOr<TagID>
MetaClient::getRelatedTagIDByIndexNameFromCache(const GraphSpaceID space,
                                                const std::string& indexName) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    auto indexRet = getTagIndexByNameFromCache(space, indexName);
    if (!indexRet.ok()) {
        LOG(ERROR) << "Index " << indexName << " Not Found";
        return indexRet.status();
    }

    return indexRet.value()->get_schema_id().get_tag_id();
}


StatusOr<std::shared_ptr<cpp2::IndexItem>>
MetaClient::getEdgeIndexFromCache(GraphSpaceID spaceId, IndexID indexID) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        VLOG(3) << "Space " << spaceId << " not found!";
        return Status::SpaceNotFound();
    } else {
        auto iter = spaceIt->second->edgeIndexes_.find(indexID);
        if (iter == spaceIt->second->edgeIndexes_.end()) {
            VLOG(3) << "Space " << spaceId << ", Edge Index " << indexID << " not found!";
            return Status::IndexNotFound();
        } else {
            return iter->second;
        }
    }
}


StatusOr<EdgeType>
MetaClient::getRelatedEdgeTypeByIndexNameFromCache(const GraphSpaceID space,
                                                   const std::string& indexName) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    auto indexRet = getEdgeIndexByNameFromCache(space, indexName);
    if (!indexRet.ok()) {
        LOG(ERROR) << "Index " << indexName << " Not Found";
        return indexRet.status();
    }

    return indexRet.value()->get_schema_id().get_edge_type();
}


StatusOr<std::vector<std::shared_ptr<cpp2::IndexItem>>>
MetaClient::getTagIndexesFromCache(GraphSpaceID spaceId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        VLOG(3) << "Space " << spaceId << " not found!";
        return Status::SpaceNotFound();
    } else {
        auto tagIndexes = spaceIt->second->tagIndexes_;
        auto iter = tagIndexes.begin();
        std::vector<std::shared_ptr<cpp2::IndexItem>> items;
        while (iter != tagIndexes.end()) {
            items.emplace_back(iter->second);
            iter++;
        }
        return items;
    }
}


StatusOr<std::vector<std::shared_ptr<cpp2::IndexItem>>>
MetaClient::getEdgeIndexesFromCache(GraphSpaceID spaceId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto spaceIt = localCache_.find(spaceId);
    if (spaceIt == localCache_.end()) {
        VLOG(3) << "Space " << spaceId << " not found!";
        return Status::SpaceNotFound();
    } else {
        auto edgeIndexes = spaceIt->second->edgeIndexes_;
        auto iter = edgeIndexes.begin();
        std::vector<std::shared_ptr<cpp2::IndexItem>> items;
        while (iter != edgeIndexes.end()) {
            items.emplace_back(iter->second);
            iter++;
        }
        return items;
    }
}


const std::vector<HostAddr>& MetaClient::getAddresses() {
    return addrs_;
}


std::vector<cpp2::RoleItem>
MetaClient::getRolesByUserFromCache(const std::string& user) {
    auto iter = userRolesMap_.find(user);
    if (iter == userRolesMap_.end()) {
        return std::vector<cpp2::RoleItem>(0);
    }
    return iter->second;
}


bool MetaClient::authCheckFromCache(const std::string& account, const std::string& password) {
    auto iter = userPasswordMap_.find(account);
    if (iter == userPasswordMap_.end()) {
        return false;
    }
    return iter->second == password;
}


StatusOr<SchemaVer> MetaClient::getLatestTagVersionFromCache(const GraphSpaceID& space,
                                                             const TagID& tagId) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceNewestTagVerMap_.find(std::make_pair(space, tagId));
    if (it == spaceNewestTagVerMap_.end()) {
        return Status::TagNotFound();
    }
    return it->second;
}


StatusOr<SchemaVer> MetaClient::getLatestEdgeVersionFromCache(const GraphSpaceID& space,
                                                              const EdgeType& edgeType) {
    if (!ready_) {
        return Status::Error("Not ready!");
    }
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    auto it = spaceNewestEdgeVerMap_.find(std::make_pair(space, edgeType));
    if (it == spaceNewestEdgeVerMap_.end()) {
        return Status::EdgeNotFound();
    }
    return it->second;
}

folly::Future<StatusOr<bool>> MetaClient::heartbeat() {
    cpp2::HBReq req;
    req.set_in_storaged(options_.inStoraged_);
    if (options_.inStoraged_) {
        req.set_host(options_.localHost_);
        if (options_.clusterId_.load() == 0) {
            options_.clusterId_ =
                FileBasedClusterIdMan::getClusterIdFromFile(FLAGS_cluster_id_path);
        }
        req.set_cluster_id(options_.clusterId_.load());
        std::unordered_map<GraphSpaceID, std::vector<PartitionID>> leaderIds;
        if (listener_ != nullptr) {
            listener_->fetchLeaderInfo(leaderIds);
            if (leaderIds_ != leaderIds) {
                {
                    folly::RWSpinLock::WriteHolder holder(leaderIdsLock_);
                    leaderIds_.clear();
                    leaderIds_ = leaderIds;
                }
                req.set_leader_partIds(std::move(leaderIds));
            }
        } else {
            req.set_leader_partIds(std::move(leaderIds));
        }
    }

    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    VLOG(1) << "Send heartbeat to " << leader_ << ", clusterId " << req.get_cluster_id();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_heartBeat(request);
                },
                [this] (cpp2::HBResp&& resp) -> bool {
                    if (options_.inStoraged_ && options_.clusterId_.load() == 0) {
                        LOG(INFO) << "Persisit the cluster Id from metad "
                                  << resp.get_cluster_id();
                        if (FileBasedClusterIdMan::persistInFile(resp.get_cluster_id(),
                                                                 FLAGS_cluster_id_path)) {
                            options_.clusterId_.store(resp.get_cluster_id());
                        } else {
                            LOG(FATAL) << "Can't persist the clusterId in file "
                                       << FLAGS_cluster_id_path;
                        }
                    }
                    metadLastUpdateTime_ = resp.get_last_update_time_in_ms();
                    VLOG(1) << "Metad last update time: " << metadLastUpdateTime_;
                    return true;  // resp.code == cpp2::ErrorCode::SUCCEEDED
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::createUser(std::string account, std::string password, bool ifNotExists) {
    cpp2::CreateUserReq req;
    req.set_account(std::move(account));
    req.set_encoded_pwd(std::move(password));
    req.set_if_not_exists(ifNotExists);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createUser(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::dropUser(std::string account, bool ifExists) {
    cpp2::DropUserReq req;
    req.set_account(std::move(account));
    req.set_if_exists(ifExists);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropUser(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::alterUser(std::string account, std::string password) {
    cpp2::AlterUserReq req;
    req.set_account(std::move(account));
    req.set_encoded_pwd(std::move(password));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_alterUser(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::grantToUser(cpp2::RoleItem roleItem) {
    cpp2::GrantRoleReq req;
    req.set_role_item(std::move(roleItem));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_grantRole(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::revokeFromUser(cpp2::RoleItem roleItem) {
    cpp2::RevokeRoleReq req;
    req.set_role_item(std::move(roleItem));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_revokeRole(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::unordered_map<std::string, std::string>>>
MetaClient::listUsers() {
    cpp2::ListUsersReq req;
    folly::Promise<StatusOr<std::unordered_map<std::string, std::string>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listUsers(request);
                },
                [] (cpp2::ListUsersResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_users();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::RoleItem>>>
MetaClient::listRoles(GraphSpaceID space) {
    cpp2::ListRolesReq req;
    req.set_space_id(std::move(space));
    folly::Promise<StatusOr<std::vector<cpp2::RoleItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listRoles(request);
                },
                [] (cpp2::ListRolesResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_roles();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::changePassword(std::string account,
                           std::string newPwd,
                           std::string oldPwd) {
    cpp2::ChangePasswordReq req;
    req.set_account(std::move(account));
    req.set_new_encoded_pwd(std::move(newPwd));
    req.set_old_encoded_pwd(std::move(oldPwd));
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_changePassword(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::RoleItem>>>
MetaClient::getUserRoles(std::string account) {
    cpp2::GetUserRolesReq req;
    req.set_account(std::move(account));
    folly::Promise<StatusOr<std::vector<cpp2::RoleItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getUserRoles(request);
                },
                [] (cpp2::ListRolesResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_roles();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<int64_t>> MetaClient::balance(std::vector<HostAddr> hostDel,
                                                     bool isStop) {
    cpp2::BalanceReq req;
    if (!hostDel.empty()) {
        req.set_host_del(std::move(hostDel));
    }
    if (isStop) {
        req.set_stop(isStop);
    }

    folly::Promise<StatusOr<int64_t>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_balance(request);
                },
                [] (cpp2::BalanceResp&& resp) -> int64_t {
                    return resp.id;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::BalanceTask>>>
MetaClient::showBalance(int64_t balanceId) {
    cpp2::BalanceReq req;
    req.set_id(balanceId);
    folly::Promise<StatusOr<std::vector<cpp2::BalanceTask>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_balance(request);
                },
                [] (cpp2::BalanceResp&& resp) -> std::vector<cpp2::BalanceTask> {
                    return resp.tasks;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>> MetaClient::balanceLeader() {
    cpp2::LeaderBalanceReq req;
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_leaderBalance(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::string>> MetaClient::getTagDefaultValue(GraphSpaceID spaceId,
                                                                    TagID tagId,
                                                                    const std::string& field) {
    cpp2::GetReq req;
    static std::string defaultKey = "__default__";
    req.set_segment(defaultKey);
    std::string key;
    key.reserve(64);
    key.append(reinterpret_cast<const char*>(&spaceId), sizeof(GraphSpaceID));
    key.append(reinterpret_cast<const char*>(&tagId), sizeof(TagID));
    key.append(field);
    req.set_key(std::move(key));
    folly::Promise<StatusOr<std::string>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_get(request);
                },
                [] (cpp2::GetResp&& resp) -> std::string {
                    return resp.get_value();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<std::string>>
MetaClient::getEdgeDefaultValue(GraphSpaceID spaceId,
                                EdgeType edgeType,
                                const std::string& field) {
    cpp2::GetReq req;
    static std::string defaultKey = "__default__";
    req.set_segment(defaultKey);
    std::string key;
    key.reserve(64);
    key.append(reinterpret_cast<const char*>(&spaceId), sizeof(GraphSpaceID));
    key.append(reinterpret_cast<const char*>(&edgeType), sizeof(EdgeType));
    key.append(field);
    req.set_key(std::move(key));
    folly::Promise<StatusOr<std::string>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_get(request);
                },
                [] (cpp2::GetResp&& resp) -> std::string {
                    return resp.get_value();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::regConfig(const std::vector<cpp2::ConfigItem>& items) {
    cpp2::RegConfigReq req;
    req.set_items(items);
    folly::Promise<StatusOr<int64_t>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_regConfig(request);
                },
                [] (cpp2::ExecResp&& resp) -> decltype(auto) {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::ConfigItem>>>
MetaClient::getConfig(const cpp2::ConfigModule& module, const std::string& name) {
    if (!configReady_) {
        return Status::Error("Not ready!");
    }
    cpp2::ConfigItem item;
    item.set_module(module);
    item.set_name(name);
    cpp2::GetConfigReq req;
    req.set_item(item);
    folly::Promise<StatusOr<std::vector<cpp2::ConfigItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_getConfig(request);
                },
                [] (cpp2::GetConfigResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_items();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>>
MetaClient::setConfig(const cpp2::ConfigModule& module,
                      const std::string& name,
                      const cpp2::ConfigType& type,
                      const std::string& value) {
    cpp2::ConfigItem item;
    item.set_module(module);
    item.set_name(name);
    item.set_type(type);
    item.set_value(value);

    cpp2::SetConfigReq req;
    req.set_item(item);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_setConfig(request);
                },
                [] (cpp2::ExecResp&& resp) -> decltype(auto) {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::ConfigItem>>>
MetaClient::listConfigs(const cpp2::ConfigModule& module) {
    cpp2::ListConfigsReq req;
    req.set_module(module);
    folly::Promise<StatusOr<std::vector<cpp2::ConfigItem>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listConfigs(request);
                },
                [] (cpp2::ListConfigsResp&& resp) -> decltype(auto) {
                    return std::move(resp).get_items();
                },
                std::move(promise));
    return future;
}


folly::Future<StatusOr<bool>> MetaClient::createSnapshot() {
    cpp2::CreateSnapshotReq req;
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_createSnapshot(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<bool>> MetaClient::dropSnapshot(const std::string& name) {
    cpp2::DropSnapshotReq req;
    req.set_name(name);
    folly::Promise<StatusOr<bool>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_dropSnapshot(request);
                },
                [] (cpp2::ExecResp&& resp) -> bool {
                    return resp.code == cpp2::ErrorCode::SUCCEEDED;
                },
                std::move(promise),
                true);
    return future;
}


folly::Future<StatusOr<std::vector<cpp2::Snapshot>>> MetaClient::listSnapshots() {
    cpp2::ListSnapshotsReq req;
    folly::Promise<StatusOr<std::vector<cpp2::Snapshot>>> promise;
    auto future = promise.getFuture();
    getResponse(std::move(req),
                [] (auto client, auto request) {
                    return client->future_listSnapshots(request);
                },
                [] (cpp2::ListSnapshotsResp&& resp) -> decltype(auto){
                    return std::move(resp).get_snapshots();
                },
                std::move(promise));
    return future;
}


bool MetaClient::registerCfg() {
    auto ret = regConfig(gflagsDeclared_).get();
    if (ret.ok()) {
        LOG(INFO) << "Register gflags ok " << gflagsDeclared_.size();
        configReady_ = true;
    }
    return configReady_;
}


bool MetaClient::loadCfg() {
    if (!configReady_ && !registerCfg()) {
        return false;
    }
    // only load current module's config is enough
    auto ret = listConfigs(gflagsModule_).get();
    if (ret.ok()) {
        // if we load config from meta server successfully, update gflags and set configReady_
        auto tItems = ret.value();
        std::vector<ConfigItem> items;
        for (const auto& tItem : tItems) {
            items.emplace_back(toConfigItem(tItem));
        }
        MetaConfigMap metaConfigMap;
        for (auto& item : items) {
            std::pair<cpp2::ConfigModule, std::string> key = {item.module_, item.name_};
            metaConfigMap.emplace(std::move(key), std::move(item));
        }
        {
            // For any configurations that is in meta, update in cache to replace previous value
            folly::RWSpinLock::WriteHolder holder(configCacheLock_);
            for (const auto& entry : metaConfigMap) {
                auto& key = entry.first;
                auto it = metaConfigMap_.find(key);
                if (it == metaConfigMap_.end() ||
                    metaConfigMap[key].value_ != it->second.value_) {
                    updateGflagsValue(entry.second);
                    metaConfigMap_[key] = entry.second;
                }
            }
        }
    } else {
        LOG(ERROR) << "Load configs failed: " << ret.status();
        return false;
    }
    return true;
}


void MetaClient::updateGflagsValue(const ConfigItem& item) {
    if (item.mode_ != cpp2::ConfigMode::MUTABLE) {
        return;
    }

    std::string metaValue;
    switch (item.type_) {
        case cpp2::ConfigType::INT64:
            metaValue = folly::to<std::string>(boost::get<int64_t>(item.value_));
            break;
        case cpp2::ConfigType::DOUBLE:
            metaValue = folly::to<std::string>(boost::get<double>(item.value_));
            break;
        case cpp2::ConfigType::BOOL:
            metaValue = boost::get<bool>(item.value_) ? "true" : "false";
            break;
        case cpp2::ConfigType::STRING:
        case cpp2::ConfigType::NESTED:
            metaValue = boost::get<std::string>(item.value_);
            break;
    }

    std::string curValue;
    if (!gflags::GetCommandLineOption(item.name_.c_str(), &curValue)) {
        return;
    } else if (curValue != metaValue) {
        if (item.type_ == cpp2::ConfigType::NESTED && metaValue.empty()) {
            // Be compatible with previous configuration
            metaValue = "{}";
        }
        gflags::SetCommandLineOption(item.name_.c_str(), metaValue.c_str());
        // TODO: we simply judge the rocksdb by nested type for now
        if (listener_ != nullptr && item.type_ == cpp2::ConfigType::NESTED) {
            updateNestedGflags(item.name_);
        }
        LOG(INFO) << "update " << item.name_ << " from " << curValue << " to " << metaValue;
    }
}


void MetaClient::updateNestedGflags(const std::string& name) {
    std::string json;
    gflags::GetCommandLineOption(name.c_str(), &json);
    // generate option string map
    conf::Configuration conf;
    auto status = conf.parseFromString(json);
    if (!status.ok()) {
        LOG(ERROR) << "Parse nested gflags " << name << " failed";
        return;
    }
    std::unordered_map<std::string, std::string> optionMap;
    conf.forEachItem([&optionMap] (const std::string& key, const folly::dynamic& val) {
        optionMap.emplace(key, val.asString());
    });
    folly::RWSpinLock::ReadHolder holder(localCacheLock_);
    for (const auto& spaceEntry : localCache_) {
        listener_->onSpaceOptionUpdated(spaceEntry.first, optionMap);
    }
}


ConfigItem MetaClient::toConfigItem(const cpp2::ConfigItem& item) {
    VariantType value;
    switch (item.get_type()) {
        case cpp2::ConfigType::INT64:
            value = *reinterpret_cast<const int64_t*>(item.get_value().data());
            break;
        case cpp2::ConfigType::BOOL:
            value = *reinterpret_cast<const bool*>(item.get_value().data());
            break;
        case cpp2::ConfigType::DOUBLE:
            value = *reinterpret_cast<const double*>(item.get_value().data());
            break;
        case cpp2::ConfigType::STRING:
        case cpp2::ConfigType::NESTED:
            value = item.get_value();
            break;
    }
    return ConfigItem(item.get_module(),
                      item.get_name(),
                      item.get_type(),
                      item.get_mode(),
                      value);
}


Status MetaClient::refreshCache() {
    auto ret = bgThread_->addTask(&MetaClient::loadData, this).get();
    return ret ? Status::OK() : Status::Error("Load data failed");
}


StatusOr<LeaderMap> MetaClient::loadLeader() {
    // Return error if has not loadData before
    if (!ready_) {
        return Status::Error("Not ready!");
    }

    auto ret = listHosts().get();
    if (!ret.ok()) {
        return Status::Error("List hosts failed");
    }

    LeaderMap leaderMap;
    auto hostItems = std::move(ret).value();
    for (auto& item : hostItems) {
        for (auto& spaceEntry : item.get_leader_parts()) {
            auto spaceName = spaceEntry.first;
            auto status = getSpaceIdByNameFromCache(spaceName);
            if (!status.ok()) {
                continue;
            }
            auto spaceId = status.value();
            for (const auto& partId : spaceEntry.second) {
                leaderMap[{spaceId, partId}] = item.hostAddr;
            }
        }
        LOG(INFO) << "Load leader of " << item.hostAddr
                  << " in " << item.get_leader_parts().size() << " space";
    }
    LOG(INFO) << "Load leader ok";
    return leaderMap;
}

}  // namespace meta
}  // namespace nebula
