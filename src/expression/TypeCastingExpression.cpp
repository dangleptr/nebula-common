/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "expression/TypeCastingExpression.h"

namespace nebula {
Value TypeCastingExpression::eval() const {
    // TODO:
    UNUSED(vType_);
    return Value(NullType::NaN);
}
}  // namespace nebula
