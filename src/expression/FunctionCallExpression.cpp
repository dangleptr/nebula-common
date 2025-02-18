/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "expression/FunctionCallExpression.h"

namespace nebula {
Value FunctionCallExpression::eval() const {
    return Value(NullType::NaN);
}
}  // namespace nebula
