# Copyright (c) 2020 vesoft inc. All rights reserved.
#
# This source code is licensed under Apache 2.0 License,
# attached with Common Clause Condition 1.0, found in the LICENSES directory.

nebula_add_library(
    meta_obj OBJECT
    GflagsManager.cpp
    ClientBasedGflagsManager.cpp
    SchemaProviderIf.cpp
    NebulaSchemaProvider.cpp
    SchemaManager.cpp
    ServerBasedSchemaManager.cpp
    IndexManager.cpp
    ServerBasedIndexManager.cpp
)
