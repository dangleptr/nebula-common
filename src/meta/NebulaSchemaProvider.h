/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#ifndef META_NEBULASCHEMAPROVIDER_H_
#define META_NEBULASCHEMAPROVIDER_H_

#include "base/Base.h"
#include "base/StatusOr.h"
#include <folly/RWSpinLock.h>
#include "meta/SchemaProviderIf.h"

namespace nebula {
namespace meta {

class NebulaSchemaProvider : public SchemaProviderIf {
    friend class FileBasedSchemaManager;
public:
    class SchemaField final : public SchemaProviderIf::Field {
    public:
        SchemaField(std::string name,
                    cpp2::PropertyType type,
                    bool nullable,
                    bool hasDefault,
                    Value defaultValue,
                    size_t size,
                    size_t offset,
                    size_t nullFlagPos)
            : name_(std::move(name))
            , type_(std::move(type))
            , nullable_(nullable)
            , hasDefault_(hasDefault)
            , defaultValue_(defaultValue)
            , size_(size)
            , offset_(offset)
            , nullFlagPos_(nullFlagPos) {}

        const char* name() const override {
            return name_.c_str();
        }

        cpp2::PropertyType type() const override {
            return type_;
        }

        bool nullable() const override {
            return nullable_;
        }

        bool hasDefault() const override {
            return hasDefault_;
        }

        const Value& defaultValue() const override {
            return defaultValue_;
        }

        size_t size() const override {
            return size_;
        }

        size_t offset() const override {
            return offset_;
        }

        size_t nullFlagPos() const override {
            DCHECK(nullable_);
            return nullFlagPos_;
        }

    private:
        std::string name_;
        cpp2::PropertyType type_;
        bool nullable_;
        bool hasDefault_;
        Value defaultValue_;
        size_t size_;
        size_t offset_;
        size_t nullFlagPos_;
    };

public:
    explicit NebulaSchemaProvider(SchemaVer ver)
        : ver_(ver)
        , numNullableFields_(0) {}

    SchemaVer getVersion() const noexcept override;
    size_t getNumFields() const noexcept override;
    size_t getNumNullableFields() const noexcept override;

    size_t size() const noexcept override;

    int64_t getFieldIndex(const folly::StringPiece name) const override;
    const char* getFieldName(int64_t index) const override;

    cpp2::PropertyType getFieldType(int64_t index) const override;
    cpp2::PropertyType getFieldType(const folly::StringPiece name) const override;

    const SchemaProviderIf::Field* field(int64_t index) const override;
    const SchemaProviderIf::Field* field(const folly::StringPiece name) const override;

    void addField(folly::StringPiece name,
                  cpp2::PropertyType type,
                  size_t fixedStrLen = 0,
                  bool nullable = false,
                  Value defaultValue = Value());

    void setProp(cpp2::SchemaProp schemaProp);

    const cpp2::SchemaProp getProp() const;

    StatusOr<std::pair<std::string, int64_t>> getTTLInfo() const;

protected:
    NebulaSchemaProvider() = default;

protected:
    SchemaVer ver_{-1};

    // fieldname -> index
    std::unordered_map<std::string, int64_t>    fieldNameIndex_;
    std::vector<SchemaField>                    fields_;
    size_t                                      numNullableFields_;
    cpp2::SchemaProp                            schemaProp_;
};

}  // namespace meta
}  // namespace nebula
#endif  // META_NEBULASCHEMAPROVIDER_H_

