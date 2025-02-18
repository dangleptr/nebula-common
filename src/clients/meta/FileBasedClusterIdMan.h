/* Copyright (c) 2019 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#ifndef CLIENTS_META_FILEBASEDCLUSTERIDMAN_H_
#define CLIENTS_META_FILEBASEDCLUSTERIDMAN_H_

#include "base/Base.h"
#include "fs/FileUtils.h"
#include "meta/ClusterIdManBase.h"


namespace nebula {
namespace meta {

/**
 * This class manages clusterId used for meta server and storage server.
 * */
class FileBasedClusterIdMan : public ClusterIdManBase {
public:
    static bool persistInFile(ClusterID clusterId, const std::string& filename);

    static ClusterID getClusterIdFromFile(const std::string& filename);
};

}  // namespace meta
}  // namespace nebula
#endif  // CLIENTS_META_FILEBASEDCLUSTERIDMAN_H_

