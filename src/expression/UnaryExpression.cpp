/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "expression/UnaryExpression.h"

namespace nebula {
Value UnaryExpression::eval() const {
   switch (type_) {
       case Type::EXP_UNARY_PLUS:
           return operand_->eval();
       case Type::EXP_UNARY_NEGATE:
           return -(operand_->eval());
       case Type::EXP_UNARY_NOT:
           return !(operand_->eval());
       default:
           break;
   }
   LOG(FATAL) << "Unknown type: " << type_;
}
}  // namespace nebula
