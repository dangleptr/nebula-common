/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include <folly/Try.h>
#include "time/WallClock.h"

namespace nebula {
namespace storage {

namespace {

template<class Request, class RemoteFunc, class Response>
struct ResponseContext {
public:
    ResponseContext(size_t reqsSent, RemoteFunc&& remoteFunc)
        : resp(reqsSent)
        , serverMethod(std::move(remoteFunc)) {}

    // Return true if processed all responses
    bool finishSending() {
        std::lock_guard<std::mutex> g(lock_);
        finishSending_ = true;
        if (ongoingRequests_.empty() && !fulfilled_) {
            fulfilled_ = true;
            return true;
        } else {
            return false;
        }
    }

    std::pair<const Request*, bool> insertRequest(HostAddr host, Request&& req) {
        std::lock_guard<std::mutex> g(lock_);
        auto res = ongoingRequests_.emplace(host, std::move(req));
        return std::make_pair(&res.first->second, res.second);
    }

    const Request& findRequest(HostAddr host) {
        std::lock_guard<std::mutex> g(lock_);
        auto it = ongoingRequests_.find(host);
        DCHECK(it != ongoingRequests_.end());
        return it->second;
    }

    // Return true if processed all responses
    bool removeRequest(HostAddr host) {
        std::lock_guard<std::mutex> g(lock_);
        ongoingRequests_.erase(host);
        if (finishSending_ && !fulfilled_ && ongoingRequests_.empty()) {
            fulfilled_ = true;
            return true;
        } else {
            return false;
        }
    }

public:
    folly::Promise<StorageRpcResponse<Response>> promise;
    StorageRpcResponse<Response> resp;
    RemoteFunc serverMethod;

private:
    std::mutex lock_;
    std::unordered_map<HostAddr, Request> ongoingRequests_;
    bool finishSending_{false};
    bool fulfilled_{false};
};

}  // Anonymous namespace


template<typename ClientType>
StorageClientBase<ClientType>::StorageClientBase(
    std::shared_ptr<folly::IOThreadPoolExecutor> threadPool,
    meta::MetaClient* metaClient)
        : metaClient_(metaClient)
        , ioThreadPool_(threadPool) {
    clientsMan_ = std::make_unique<thrift::ThriftClientManager<ClientType>>();
}


template<typename ClientType>
StorageClientBase<ClientType>::~StorageClientBase() {
    VLOG(3) << "Destructing StorageClientBase";
    if (nullptr != metaClient_) {
        metaClient_ = nullptr;
    }
}


template<typename ClientType>
void StorageClientBase<ClientType>::loadLeader() const {
    if (loadLeaderBefore_) {
        return;
    }
    bool expected = false;
    if (isLoadingLeader_.compare_exchange_strong(expected, true)) {
        CHECK(metaClient_ != nullptr);
        auto status = metaClient_->loadLeader();
        if (status.ok()) {
            folly::RWSpinLock::WriteHolder wh(leadersLock_);
            leaders_ = std::move(status).value();
            loadLeaderBefore_ = true;
        }
        isLoadingLeader_ = false;
    }
}


template<typename ClientType>
const HostAddr
StorageClientBase<ClientType>::getLeader(const meta::PartHosts& partHosts) const {
    loadLeader();
    auto part = std::make_pair(partHosts.spaceId_, partHosts.partId_);

    // Acquire the read lock
    folly::RWSpinLock::ReadHolder rh(leadersLock_);

    auto it = leaders_.find(part);
    if (it != leaders_.end()) {
        return it->second;
    } else {
        VLOG(1) << "No leader exists. Choose one random.";

        // Upgrade the read lock to the write lock
        folly::RWSpinLock::UpgradedHolder uh(leadersLock_);
        rh.reset();
        folly::RWSpinLock::WriteHolder wh(std::move(uh));

        auto& random = partHosts.hosts_[folly::Random::rand32(partHosts.hosts_.size())];
        leaders_[part] = random;
        return random;
    }
}


template<typename ClientType>
void StorageClientBase<ClientType>::updateLeader(GraphSpaceID spaceId,
                                                 PartitionID partId,
                                                 const HostAddr& leader) {
    LOG(INFO) << "Update the leader for [" << spaceId
              << ", " << partId
              << "] to " << leader;

    folly::RWSpinLock::WriteHolder wh(leadersLock_);
    leaders_[std::make_pair(spaceId, partId)] = leader;
}


template<typename ClientType>
void StorageClientBase<ClientType>::invalidLeader(GraphSpaceID spaceId,
                                                   PartitionID partId) {
    LOG(INFO) << "Invalidate the leader for [" << spaceId << ", " << partId << "]";
    folly::RWSpinLock::WriteHolder wh(leadersLock_);
    auto it = leaders_.find(std::make_pair(spaceId, partId));
    if (it != leaders_.end()) {
        leaders_.erase(it);
    }
}


template<typename ClientType>
template<class Request, class RemoteFunc, class GetPartIDFunc, class Response>
folly::SemiFuture<StorageRpcResponse<Response>>
StorageClientBase<ClientType>::collectResponse(
        folly::EventBase* evb,
        std::unordered_map<HostAddr, Request> requests,
        RemoteFunc&& remoteFunc,
        GetPartIDFunc getPartIDFunc) {
    auto context = std::make_shared<ResponseContext<Request, RemoteFunc, Response>>(
        requests.size(), std::move(remoteFunc));

    if (evb == nullptr) {
        DCHECK(!!ioThreadPool_);
        evb = ioThreadPool_->getEventBase();
    }

    for (auto& req : requests) {
        auto& host = req.first;
        auto spaceId = req.second.get_space_id();
        auto res = context->insertRequest(host, std::move(req.second));
        DCHECK(res.second);
        // Invoke the remote method
        folly::via(evb, [this,
                         evb,
                         context,
                         host,
                         spaceId,
                         res,
                         getPartIDFunc] () mutable {
            auto client = clientsMan_->client(host,
                                              evb,
                                              false,
                                              FLAGS_storage_client_timeout_ms);
            // Result is a pair of <Request&, bool>
            auto start = time::WallClock::fastNowInMicroSec();
            context->serverMethod(client.get(), *res.first)
            // Future process code will be executed on the IO thread
            // Since all requests are sent using the same eventbase, all then-callback
            // will be executed on the same IO thread
            .via(evb).then([this,
                            context,
                            host,
                            spaceId,
                            getPartIDFunc,
                            start] (folly::Try<Response>&& val) {
                auto& r = context->findRequest(host);
                if (val.hasException()) {
                    LOG(ERROR) << "Request to " << host
                               << " failed: " << val.exception().what();
                    for (auto& part : r.parts) {
                        auto partId = getPartIDFunc(part);
                        VLOG(3) << "Exception! Failed part " << partId;
                        context->resp.failedParts().emplace(
                            partId,
                            storage::cpp2::ErrorCode::E_RPC_FAILURE);
                        invalidLeader(spaceId, partId);
                    }
                    context->resp.markFailure();
                } else {
                    auto resp = std::move(val.value());
                    auto& result = resp.get_result();
                    bool hasFailure{false};
                    for (auto& code : result.get_failed_parts()) {
                        VLOG(3) << "Failure! Failed part " << code.get_part_id()
                                << ", failed code " << static_cast<int32_t>(code.get_code());
                        hasFailure = true;
                        if (code.get_code() == storage::cpp2::ErrorCode::E_LEADER_CHANGED) {
                            auto* leader = code.get_leader();
                            if (leader != nullptr &&
                                !leader->host.empty() &&
                                leader->port != 0) {
                                updateLeader(spaceId, code.get_part_id(), *leader);
                            } else {
                                invalidLeader(spaceId, code.get_part_id());
                            }
                        } else if (code.get_code() == cpp2::ErrorCode::E_PART_NOT_FOUND
                                || code.get_code() == cpp2::ErrorCode::E_SPACE_NOT_FOUND) {
                            invalidLeader(spaceId, code.get_part_id());
                        } else {
                            // Simply keep the result
                            context->resp.failedParts().emplace(code.get_part_id(),
                                                                code.get_code());
                        }
                    }
                    if (hasFailure) {
                        context->resp.markFailure();
                    }

                    // Adjust the latency
                    auto latency = result.get_latency_in_us();
                    context->resp.setLatency(host,
                                             latency,
                                             time::WallClock::fastNowInMicroSec() - start);

                    // Keep the response
                    context->resp.responses().emplace_back(std::move(resp));
                }

                if (context->removeRequest(host)) {
                    // Received all responses
                    context->promise.setValue(std::move(context->resp));
                }
            });
        });  // via
    }  // for

    if (context->finishSending()) {
        // Received all responses, most likely, all rpc failed
        context->promise.setValue(std::move(context->resp));
    }

    return context->promise.getSemiFuture();
}


template<typename ClientType>
template<class Request, class RemoteFunc, class Response>
folly::Future<StatusOr<Response>> StorageClientBase<ClientType>::getResponse(
        folly::EventBase* evb,
        std::pair<HostAddr, Request> request,
        RemoteFunc remoteFunc) {
    if (evb == nullptr) {
        DCHECK(!!ioThreadPool_);
        evb = ioThreadPool_->getEventBase();
    }
    folly::Promise<StatusOr<Response>> pro;
    auto f = pro.getFuture();
    folly::via(evb, [evb, request = std::move(request), remoteFunc = std::move(remoteFunc),
                     pro = std::move(pro), this] () mutable {
        auto host = request.first;
        auto client = clientsMan_->client(host, evb, false, FLAGS_storage_client_timeout_ms);
        auto spaceId = request.second.get_space_id();
        auto partId = request.second.get_part_id();
        LOG(INFO) << "Send request to storage " << host;
        remoteFunc(client.get(), std::move(request.second)).via(evb)
             .then([spaceId,
                    partId,
                    p = std::move(pro),
                    this] (folly::Try<Response>&& t) mutable {
            // exception occurred during RPC
            if (t.hasException()) {
                p.setValue(
                    Status::Error(
                        folly::stringPrintf("RPC failure in StorageClient: %s",
                                            t.exception().what().c_str())));
                invalidLeader(spaceId, partId);
                return;
            }
            auto&& resp = std::move(t.value());
            // leader changed
            auto& result = resp.get_result();
            for (auto& code : result.get_failed_parts()) {
                VLOG(3) << "Failure! Failed part " << code.get_part_id()
                        << ", failed code " << static_cast<int32_t>(code.get_code());
                if (code.get_code() == storage::cpp2::ErrorCode::E_LEADER_CHANGED) {
                    auto* leader = code.get_leader();
                    if (leader != nullptr &&
                        !leader->host.empty() &&
                        leader->port != 0) {
                        updateLeader(spaceId, code.get_part_id(), *leader);
                    } else {
                        invalidLeader(spaceId, code.get_part_id());
                    }
                } else if (code.get_code() == storage::cpp2::ErrorCode::E_PART_NOT_FOUND ||
                           code.get_code() == storage::cpp2::ErrorCode::E_SPACE_NOT_FOUND) {
                    invalidLeader(spaceId, code.get_part_id());
                }
            }
            p.setValue(std::move(resp));
        });
    });  // via
    return f;
}


template<typename ClientType>
template<class Container, class GetIdFunc>
StatusOr<
    std::unordered_map<
        HostAddr,
        std::unordered_map<
            PartitionID,
            std::vector<typename Container::value_type>
        >
    >
>
StorageClientBase<ClientType>::clusterIdsToHosts(GraphSpaceID spaceId,
                                                 const Container& ids,
                                                 GetIdFunc f) const {
    std::unordered_map<
        HostAddr,
        std::unordered_map<
            PartitionID,
            std::vector<typename Container::value_type>
        >
    > clusters;
    for (auto& id : ids) {
        CHECK(!!metaClient_);
        auto status = metaClient_->partId(spaceId, f(id));
        if (!status.ok()) {
            return status.status();
        }

        auto part = status.value();
        auto metaStatus = getPartHosts(spaceId, part);
        if (!metaStatus.ok()) {
            return status.status();
        }

        auto partHosts = metaStatus.value();
        CHECK_GT(partHosts.hosts_.size(), 0U);
        const auto leader = this->getLeader(partHosts);
        clusters[leader][part].emplace_back(std::move(id));
    }
    return clusters;
}


template<typename ClientType>
StatusOr<std::unordered_map<HostAddr, std::vector<PartitionID>>>
StorageClientBase<ClientType>::getHostParts(GraphSpaceID spaceId) const {
    std::unordered_map<HostAddr, std::vector<PartitionID>> hostParts;
    auto status = metaClient_->partsNum(spaceId);
    if (!status.ok()) {
        return Status::Error("Space not found, spaceid: %d", spaceId);
    }

    auto parts = status.value();
    for (auto partId = 1; partId <= parts; partId++) {
        auto metaStatus = getPartHosts(spaceId, partId);
        if (!metaStatus.ok()) {
            return metaStatus.status();
        }
        auto partHosts = std::move(metaStatus).value();
        DCHECK_GT(partHosts.hosts_.size(), 0U);
        const auto leader = getLeader(partHosts);
        hostParts[leader].emplace_back(partId);
    }
    return hostParts;
}

}   // namespace storage
}   // namespace nebula
