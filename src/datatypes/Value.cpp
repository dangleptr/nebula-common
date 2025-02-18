/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "datatypes/Value.h"
#include <folly/hash/Hash.h>
#include "datatypes/List.h"
#include "datatypes/Map.h"
#include "datatypes/Set.h"
#include "datatypes/DataSet.h"
#include "datatypes/Vertex.h"
#include "datatypes/Edge.h"
#include "datatypes/Path.h"

namespace std {

std::size_t hash<nebula::Value>::operator()(const nebula::Value& v) const noexcept {
    switch (v.type()) {
        case nebula::Value::Type::__EMPTY__: {
            return 0;
        }
        case nebula::Value::Type::NULLVALUE: {
            return folly::hash::fnv64_buf(reinterpret_cast<const void*>(&v.getNull()),
                                          sizeof(nebula::NullType));
        }
        case nebula::Value::Type::BOOL: {
            return folly::hash::fnv64_buf(reinterpret_cast<const void*>(&v.getBool()),
                                          sizeof(bool));
        }
        case nebula::Value::Type::INT: {
            return folly::hash::fnv64_buf(reinterpret_cast<const void*>(&v.getInt()),
                                          sizeof(int64_t));
        }
        case nebula::Value::Type::FLOAT: {
            return folly::hash::fnv64_buf(reinterpret_cast<const void*>(&v.getFloat()),
                                          sizeof(double));
        }
        case nebula::Value::Type::STRING: {
            return folly::hash::fnv64(v.getStr());
        }
        case nebula::Value::Type::DATE: {
            return hash<nebula::Date>()(v.getDate());
        }
        case nebula::Value::Type::DATETIME: {
            return hash<nebula::DateTime>()(v.getDateTime());
        }
        case nebula::Value::Type::VERTEX: {
            return hash<nebula::Vertex>()(v.getVertex());
        }
        case nebula::Value::Type::EDGE: {
            return hash<nebula::Edge>()(v.getEdge());
        }
        case nebula::Value::Type::PATH: {
            return hash<nebula::Path>()(v.getPath());
        }
        case nebula::Value::Type::LIST: {
            LOG(FATAL) << "Hash for LIST has not been implemented";
        }
        case nebula::Value::Type::MAP: {
            LOG(FATAL) << "Hash for MAP has not been implemented";
        }
        case nebula::Value::Type::SET: {
            LOG(FATAL) << "Hash for SET has not been implemented";
        }
        case nebula::Value::Type::DATASET: {
            LOG(FATAL) << "Hash for DATASET has not been implemented";
        }
        default: {
            LOG(FATAL) << "Unknown type";
        }
    }
}

}  // namespace std


namespace nebula {

constexpr auto EPSILON = 1e-8;

Value::Value(Value&& rhs) : type_(Value::Type::__EMPTY__) {
    if (this == &rhs) { return; }
    if (rhs.type_ == Type::__EMPTY__) { return; }
    switch (rhs.type_) {
        case Type::NULLVALUE:
        {
            setN(std::move(rhs.value_.nVal));
            break;
        }
        case Type::BOOL:
        {
            setB(std::move(rhs.value_.bVal));
            break;
        }
        case Type::INT:
        {
            setI(std::move(rhs.value_.iVal));
            break;
        }
        case Type::FLOAT:
        {
            setF(std::move(rhs.value_.fVal));
            break;
        }
        case Type::STRING:
        {
            setS(std::move(rhs.value_.sVal));
            break;
        }
        case Type::DATE:
        {
            setD(std::move(rhs.value_.dVal));
            break;
        }
        case Type::DATETIME:
        {
            setT(std::move(rhs.value_.tVal));
            break;
        }
        case Type::VERTEX:
        {
            setV(std::move(rhs.value_.vVal));
            break;
        }
        case Type::EDGE:
        {
            setE(std::move(rhs.value_.eVal));
            break;
        }
        case Type::PATH:
        {
            setP(std::move(rhs.value_.pVal));
            break;
        }
        case Type::LIST:
        {
            setL(std::move(rhs.value_.lVal));
            break;
        }
        case Type::MAP:
        {
            setM(std::move(rhs.value_.mVal));
            break;
        }
        case Type::SET:
        {
            setU(std::move(rhs.value_.uVal));
            break;
        }
        case Type::DATASET:
        {
            setG(std::move(rhs.value_.gVal));
            break;
        }
        default:
        {
            assert(false);
            break;
        }
    }
    rhs.clear();
}


Value::Value(const Value& rhs) : type_(Value::Type::__EMPTY__) {
    if (this == &rhs) { return; }
    if (rhs.type_ == Type::__EMPTY__) { return; }
    switch (rhs.type_) {
        case Type::NULLVALUE:
        {
            setN(rhs.value_.nVal);
            break;
        }
        case Type::BOOL:
        {
            setB(rhs.value_.bVal);
            break;
        }
        case Type::INT:
        {
            setI(rhs.value_.iVal);
            break;
        }
        case Type::FLOAT:
        {
            setF(rhs.value_.fVal);
            break;
        }
        case Type::STRING:
        {
            setS(rhs.value_.sVal);
            break;
        }
        case Type::DATE:
        {
            setD(rhs.value_.dVal);
            break;
        }
        case Type::DATETIME:
        {
            setT(rhs.value_.tVal);
            break;
        }
        case Type::VERTEX:
        {
            setV(rhs.value_.vVal);
            break;
        }
        case Type::EDGE:
        {
            setE(rhs.value_.eVal);
            break;
        }
        case Type::PATH:
        {
            setP(rhs.value_.pVal);
            break;
        }
        case Type::LIST:
        {
            setL(rhs.value_.lVal);
            break;
        }
        case Type::MAP:
        {
            setM(rhs.value_.mVal);
            break;
        }
        case Type::SET:
        {
            setU(rhs.value_.uVal);
            break;
        }
        case Type::DATASET:
        {
            setG(rhs.value_.gVal);
            break;
        }
        default:
        {
            assert(false);
            break;
        }
    }
}

Value::Value(const NullType& v) {
    setN(v);
}

Value::Value(NullType&& v) {
    setN(std::move(v));
}

Value::Value(const bool& v) {
    setB(v);
}

Value::Value(bool&& v) {
    setB(std::move(v));
}

Value::Value(const int8_t& v) {
    setI(v);
}

Value::Value(int8_t&& v) {
    setI(std::move(v));
}

Value::Value(const int16_t& v) {
    setI(v);
}

Value::Value(int16_t&& v) {
    setI(std::move(v));
}

Value::Value(const int32_t& v) {
    setI(v);
}

Value::Value(int32_t&& v) {
    setI(std::move(v));
}

Value::Value(const int64_t& v) {
    setI(v);
}

Value::Value(int64_t&& v) {
    setI(std::move(v));
}

Value::Value(const double& v) {
    setF(v);
}

Value::Value(double&& v) {
    setF(std::move(v));
}

Value::Value(const std::string& v) {
    setS(v);
}

Value::Value(std::string&& v) {
    setS(std::move(v));
}

Value::Value(const char* v) {
    setS(v);
}

Value::Value(folly::StringPiece v) {
    setS(v);
}

Value::Value(const Date& v) {
    setD(v);
}

Value::Value(Date&& v) {
    setD(std::move(v));
}

Value::Value(const DateTime& v) {
    setT(v);
}

Value::Value(DateTime&& v) {
    setT(std::move(v));
}

Value::Value(const Vertex& v) {
    setV(v);
}

Value::Value(Vertex&& v) {
    setV(std::move(v));
}

Value::Value(const Edge& v) {
    setE(v);
}

Value::Value(Edge&& v) {
    setE(std::move(v));
}

Value::Value(const Path& v) {
    setP(v);
}

Value::Value(Path&& v) {
    setP(std::move(v));
}

Value::Value(const List& v) {
    auto c = std::make_unique<List>(v);
    setL(std::move(c));
}

Value::Value(List&& v) {
    setL(std::make_unique<List>(std::move(v)));
}

Value::Value(const Map& v) {
    auto c = std::make_unique<Map>(v);
    setM(std::move(c));
}

Value::Value(Map&& v) {
    setM(std::make_unique<Map>(std::move(v)));
}

Value::Value(const Set& v) {
    auto c = std::make_unique<Set>(v);
    setU(std::move(c));
}

Value::Value(Set&& v) {
    setU(std::make_unique<Set>(std::move(v)));
}

Value::Value(const DataSet& v) {
    auto c = std::make_unique<DataSet>(v);
    setG(std::move(c));
}

Value::Value(DataSet&& v) {
    setG(std::make_unique<DataSet>(std::move(v)));
}


void Value::setNull(const NullType& v) {
    clear();
    setN(v);
}

void Value::setNull(NullType&& v) {
    clear();
    setN(std::move(v));
}

void Value::setBool(const bool& v) {
    clear();
    setB(v);
}

void Value::setBool(bool&& v) {
    clear();
    setB(std::move(v));
}

void Value::setInt(const int8_t& v) {
    clear();
    setI(v);
}

void Value::setInt(int8_t&& v) {
    clear();
    setI(std::move(v));
}

void Value::setInt(const int16_t& v) {
    clear();
    setI(v);
}

void Value::setInt(int16_t&& v) {
    clear();
    setI(std::move(v));
}

void Value::setInt(const int32_t& v) {
    clear();
    setI(v);
}

void Value::setInt(int32_t&& v) {
    clear();
    setI(std::move(v));
}

void Value::setInt(const int64_t& v) {
    clear();
    setI(v);
}

void Value::setInt(int64_t&& v) {
    clear();
    setI(std::move(v));
}

void Value::setFloat(const double& v) {
    clear();
    setF(v);
}

void Value::setFloat(double&& v) {
    clear();
    setF(std::move(v));
}

void Value::setStr(const std::string& v) {
    clear();
    setS(v);
}

void Value::setStr(std::string&& v) {
    clear();
    setS(std::move(v));
}

void Value::setStr(const char* v) {
    clear();
    setS(v);
}

void Value::setStr(folly::StringPiece v) {
    clear();
    setS(v);
}

void Value::setDate(const Date& v) {
    clear();
    setD(v);
}

void Value::setDate(Date&& v) {
    clear();
    setD(std::move(v));
}

void Value::setDateTime(const DateTime& v) {
    clear();
    setT(v);
}

void Value::setDateTime(DateTime&& v) {
    clear();
    setT(std::move(v));
}

void Value::setVertex(const Vertex& v) {
    clear();
    setV(v);
}

void Value::setVertex(Vertex&& v) {
    clear();
    setV(std::move(v));
}

void Value::setVertex(std::unique_ptr<Vertex>&& v) {
    clear();
    setV(std::move(v));
}

void Value::setEdge(const Edge& v) {
    clear();
    setE(v);
}

void Value::setEdge(Edge&& v) {
    clear();
    setE(std::move(v));
}

void Value::setEdge(std::unique_ptr<Edge>&& v) {
    clear();
    setE(std::move(v));
}

void Value::setPath(const Path& v) {
    clear();
    setP(v);
}

void Value::setPath(Path&& v) {
    clear();
    setP(std::move(v));
}

void Value::setPath(std::unique_ptr<Path>&& v) {
    clear();
    setP(std::move(v));
}

void Value::setList(const List& v) {
    clear();
    setL(v);
}

void Value::setList(List&& v) {
    clear();
    setL(std::move(v));
}

void Value::setList(std::unique_ptr<List>&& v) {
    clear();
    setL(std::move(v));
}

void Value::setMap(const Map& v) {
    clear();
    setM(v);
}

void Value::setMap(Map&& v) {
    clear();
    setM(std::move(v));
}

void Value::setMap(std::unique_ptr<Map>&& v) {
    clear();
    setM(std::move(v));
}

void Value::setSet(const Set& v) {
    clear();
    setU(v);
}

void Value::setSet(Set&& v) {
    clear();
    setU(std::move(v));
}

void Value::setSet(std::unique_ptr<Set>&& v) {
    clear();
    setU(std::move(v));
}

void Value::setDataSet(const DataSet& v) {
    clear();
    setG(v);
}

void Value::setDataSet(DataSet&& v) {
    clear();
    setG(std::move(v));
}

void Value::setDataSet(std::unique_ptr<DataSet>&& v) {
    clear();
    setG(std::move(v));
}


const NullType& Value::getNull() const {
    CHECK_EQ(type_, Type::NULLVALUE);
    return value_.nVal;
}

const bool& Value::getBool() const {
    CHECK_EQ(type_, Type::BOOL);
    return value_.bVal;
}

const int64_t& Value::getInt() const {
    CHECK_EQ(type_, Type::INT);
    return value_.iVal;
}

const double& Value::getFloat() const {
    CHECK_EQ(type_, Type::FLOAT);
    return value_.fVal;
}

const std::string& Value::getStr() const {
    CHECK_EQ(type_, Type::STRING);
    return value_.sVal;
}

const Date& Value::getDate() const {
    CHECK_EQ(type_, Type::DATE);
    return value_.dVal;
}

const DateTime& Value::getDateTime() const {
    CHECK_EQ(type_, Type::DATETIME);
    return value_.tVal;
}

const Vertex& Value::getVertex() const {
    CHECK_EQ(type_, Type::VERTEX);
    return *(value_.vVal);
}

const Vertex* Value::getVertexPtr() const {
    CHECK_EQ(type_, Type::VERTEX);
    return value_.vVal.get();
}

const Edge& Value::getEdge() const {
    CHECK_EQ(type_, Type::EDGE);
    return *(value_.eVal);
}

const Edge* Value::getEdgePtr() const {
    CHECK_EQ(type_, Type::EDGE);
    return value_.eVal.get();
}

const Path& Value::getPath() const {
    CHECK_EQ(type_, Type::PATH);
    return *(value_.pVal);
}

const Path* Value::getPathPtr() const {
    CHECK_EQ(type_, Type::PATH);
    return value_.pVal.get();
}

const List& Value::getList() const {
    CHECK_EQ(type_, Type::LIST);
    return *(value_.lVal);
}

const List* Value::getListPtr() const {
    CHECK_EQ(type_, Type::LIST);
    return value_.lVal.get();
}

const Map& Value::getMap() const {
    CHECK_EQ(type_, Type::MAP);
    return *(value_.mVal);
}

const Map* Value::getMapPtr() const {
    CHECK_EQ(type_, Type::MAP);
    return value_.mVal.get();
}

const Set& Value::getSet() const {
    CHECK_EQ(type_, Type::SET);
    return *(value_.uVal);
}

const Set* Value::getSetPtr() const {
    CHECK_EQ(type_, Type::SET);
    return value_.uVal.get();
}

const DataSet& Value::getDataSet() const {
    CHECK_EQ(type_, Type::DATASET);
    return *(value_.gVal);
}

const DataSet* Value::getDataSetPtr() const {
    CHECK_EQ(type_, Type::DATASET);
    return value_.gVal.get();
}


NullType& Value::mutableNull() {
    CHECK_EQ(type_, Type::NULLVALUE);
    return value_.nVal;
}

bool& Value::mutableBool() {
    CHECK_EQ(type_, Type::BOOL);
    return value_.bVal;
}

int64_t& Value::mutableInt() {
    CHECK_EQ(type_, Type::INT);
    return value_.iVal;
}

double& Value::mutableFloat() {
    CHECK_EQ(type_, Type::FLOAT);
    return value_.fVal;
}

std::string& Value::mutableStr() {
    CHECK_EQ(type_, Type::STRING);
    return value_.sVal;
}

Date& Value::mutableDate() {
    CHECK_EQ(type_, Type::DATE);
    return value_.dVal;
}

DateTime& Value::mutableDateTime() {
    CHECK_EQ(type_, Type::DATETIME);
    return value_.tVal;
}

Vertex& Value::mutableVertex() {
    CHECK_EQ(type_, Type::VERTEX);
    return *(value_.vVal);
}

Edge& Value::mutableEdge() {
    CHECK_EQ(type_, Type::EDGE);
    return *(value_.eVal);
}

Path& Value::mutablePath() {
    CHECK_EQ(type_, Type::PATH);
    return *(value_.pVal);
}

List& Value::mutableList() {
    CHECK_EQ(type_, Type::LIST);
    return *(value_.lVal);
}

Map& Value::mutableMap() {
    CHECK_EQ(type_, Type::MAP);
    return *(value_.mVal);
}

Set& Value::mutableSet() {
    CHECK_EQ(type_, Type::SET);
    return *(value_.uVal);
}

DataSet& Value::mutableDataSet() {
    CHECK_EQ(type_, Type::DATASET);
    return *(value_.gVal);
}


NullType Value::moveNull() {
    CHECK_EQ(type_, Type::NULLVALUE);
    NullType v = std::move(value_.nVal);
    clear();
    return std::move(v);
}

bool Value::moveBool() {
    CHECK_EQ(type_, Type::BOOL);
    bool v = std::move(value_.bVal);
    clear();
    return std::move(v);
}

int64_t Value::moveInt() {
    CHECK_EQ(type_, Type::INT);
    int64_t v = std::move(value_.iVal);
    clear();
    return std::move(v);
}

double Value::moveFloat() {
    CHECK_EQ(type_, Type::FLOAT);
    double v = std::move(value_.fVal);
    clear();
    return std::move(v);
}

std::string Value::moveStr() {
    CHECK_EQ(type_, Type::STRING);
    std::string v = std::move(value_.sVal);
    clear();
    return v;
}

Date Value::moveDate() {
    CHECK_EQ(type_, Type::DATE);
    Date v = std::move(value_.dVal);
    clear();
    return v;
}

DateTime Value::moveDateTime() {
    CHECK_EQ(type_, Type::DATETIME);
    DateTime v = std::move(value_.tVal);
    clear();
    return v;
}

Vertex Value::moveVertex() {
    CHECK_EQ(type_, Type::VERTEX);
    Vertex v = std::move(*(value_.vVal));
    clear();
    return v;
}

Edge Value::moveEdge() {
    CHECK_EQ(type_, Type::EDGE);
    Edge v = std::move(*(value_.eVal));
    clear();
    return v;
}

Path Value::movePath() {
    CHECK_EQ(type_, Type::PATH);
    Path v = std::move(*(value_.pVal));
    clear();
    return v;
}

List Value::moveList() {
    CHECK_EQ(type_, Type::LIST);
    List list = std::move(*(value_.lVal));
    clear();
    return list;
}

Map Value::moveMap() {
    CHECK_EQ(type_, Type::MAP);
    Map map = std::move(*(value_.mVal));
    clear();
    return map;
}

Set Value::moveSet() {
    CHECK_EQ(type_, Type::SET);
    Set set = std::move(*(value_.uVal));
    clear();
    return set;
}

DataSet Value::moveDataSet() {
    CHECK_EQ(type_, Type::DATASET);
    DataSet ds = std::move(*(value_.gVal));
    clear();
    return ds;
}

void Value::clear() {
    switch (type_) {
        case Type::__EMPTY__:
        {
            return;
        }
        case Type::NULLVALUE:
        {
            destruct(value_.nVal);
            break;
        }
        case Type::BOOL:
        {
            destruct(value_.bVal);
            break;
        }
        case Type::INT:
        {
            destruct(value_.iVal);
            break;
        }
        case Type::FLOAT:
        {
            destruct(value_.fVal);
            break;
        }
        case Type::STRING:
        {
            destruct(value_.sVal);
            break;
        }
        case Type::DATE:
        {
            destruct(value_.dVal);
            break;
        }
        case Type::DATETIME:
        {
            destruct(value_.tVal);
            break;
        }
        case Type::VERTEX:
        {
            destruct(value_.vVal);
            break;
        }
        case Type::EDGE:
        {
            destruct(value_.eVal);
            break;
        }
        case Type::PATH:
        {
            destruct(value_.pVal);
            break;
        }
        case Type::LIST:
        {
            destruct(value_.lVal);
            break;
        }
        case Type::MAP:
        {
            destruct(value_.mVal);
            break;
        }
        case Type::SET:
        {
            destruct(value_.uVal);
            break;
        }
        case Type::DATASET:
        {
            destruct(value_.gVal);
            break;
        }
    }
    type_ = Type::__EMPTY__;
}


Value& Value::operator=(Value&& rhs) {
    if (this == &rhs) { return *this; }
    clear();
    if (rhs.type_ == Type::__EMPTY__) { return *this; }
    switch (rhs.type_) {
        case Type::NULLVALUE:
        {
            setN(std::move(rhs.value_.nVal));
            break;
        }
        case Type::BOOL:
        {
            setB(std::move(rhs.value_.bVal));
            break;
        }
        case Type::INT:
        {
            setI(std::move(rhs.value_.iVal));
            break;
        }
        case Type::FLOAT:
        {
            setF(std::move(rhs.value_.fVal));
            break;
        }
        case Type::STRING:
        {
            setS(std::move(rhs.value_.sVal));
            break;
        }
        case Type::DATE:
        {
            setD(std::move(rhs.value_.dVal));
            break;
        }
        case Type::DATETIME:
        {
            setT(std::move(rhs.value_.tVal));
            break;
        }
        case Type::VERTEX:
        {
            setV(std::move(rhs.value_.vVal));
            break;
        }
        case Type::EDGE:
        {
            setE(std::move(rhs.value_.eVal));
            break;
        }
        case Type::PATH:
        {
            setP(std::move(rhs.value_.pVal));
            break;
        }
        case Type::LIST:
        {
            setL(std::move(rhs.value_.lVal));
            break;
        }
        case Type::MAP:
        {
            setM(std::move(rhs.value_.mVal));
            break;
        }
        case Type::SET:
        {
            setU(std::move(rhs.value_.uVal));
            break;
        }
        case Type::DATASET:
        {
            setG(std::move(rhs.value_.gVal));
            break;
        }
        default:
        {
            assert(false);
            break;
        }
    }
    rhs.clear();
    return *this;
}


Value& Value::operator=(const Value& rhs) {
    if (this == &rhs) { return *this; }
    clear();
    if (rhs.type_ == Type::__EMPTY__) { return *this; }
    switch (rhs.type_) {
        case Type::NULLVALUE:
        {
            setN(rhs.value_.nVal);
            break;
        }
        case Type::BOOL:
        {
            setB(rhs.value_.bVal);
            break;
        }
        case Type::INT:
        {
            setI(rhs.value_.iVal);
            break;
        }
        case Type::FLOAT:
        {
            setF(rhs.value_.fVal);
            break;
        }
        case Type::STRING:
        {
            setS(rhs.value_.sVal);
            break;
        }
        case Type::DATE:
        {
            setD(rhs.value_.dVal);
            break;
        }
        case Type::DATETIME:
        {
            setT(rhs.value_.tVal);
            break;
        }
        case Type::VERTEX:
        {
            setV(rhs.value_.vVal);
            break;
        }
        case Type::EDGE:
        {
            setE(rhs.value_.eVal);
            break;
        }
        case Type::PATH:
        {
            setP(rhs.value_.pVal);
            break;
        }
        case Type::LIST:
        {
            setL(rhs.value_.lVal);
            break;
        }
        case Type::MAP:
        {
            setM(rhs.value_.mVal);
            break;
        }
        case Type::SET:
        {
            setU(rhs.value_.uVal);
            break;
        }
        case Type::DATASET:
        {
            setG(rhs.value_.gVal);
            break;
        }
        default:
        {
            assert(false);
            break;
        }
    }
    type_ = rhs.type_;
    return *this;
}


void Value::setN(const NullType& v) {
    type_ = Type::NULLVALUE;
    new (std::addressof(value_.nVal)) NullType(v);
}

void Value::setN(NullType&& v) {
    type_ = Type::NULLVALUE;
    new (std::addressof(value_.nVal)) NullType(std::move(v));
}

void Value::setB(const bool& v) {
    type_ = Type::BOOL;
    new (std::addressof(value_.bVal)) bool(v);                  // NOLINT
}

void Value::setB(bool&& v) {
    type_ = Type::BOOL;
    new (std::addressof(value_.bVal)) bool(std::move(v));       // NOLINT
}

void Value::setI(const int64_t& v) {
    type_ = Type::INT;
    new (std::addressof(value_.iVal)) int64_t(v);               // NOLINT
}

void Value::setI(int64_t&& v) {
    type_ = Type::INT;
    new (std::addressof(value_.iVal)) int64_t(std::move(v));    // NOLINT
}

void Value::setF(const double& v) {
    type_ = Type::FLOAT;
    new (std::addressof(value_.fVal)) double(v);                // NOLINT
}

void Value::setF(double&& v) {
    type_ = Type::FLOAT;
    new (std::addressof(value_.fVal)) double(std::move(v));     // NOLINT
}

void Value::setS(const std::string& v) {
    type_ = Type::STRING;
    new (std::addressof(value_.sVal)) std::string(v);
}

void Value::setS(std::string&& v) {
    type_ = Type::STRING;
    new (std::addressof(value_.sVal)) std::string(std::move(v));
}

void Value::setS(const char* v) {
    type_ = Type::STRING;
    new (std::addressof(value_.sVal)) std::string(v);
}

void Value::setS(folly::StringPiece v) {
    type_ = Type::STRING;
    new (std::addressof(value_.sVal)) std::string(v.data(), v.size());
}

void Value::setD(const Date& v) {
    type_ = Type::DATE;
    new (std::addressof(value_.dVal)) Date(v);
}

void Value::setD(Date&& v) {
    type_ = Type::DATE;
    new (std::addressof(value_.dVal)) Date(std::move(v));
}

void Value::setT(const DateTime& v) {
    type_ = Type::DATETIME;
    new (std::addressof(value_.tVal)) DateTime(v);
}

void Value::setT(DateTime&& v) {
    type_ = Type::DATETIME;
    new (std::addressof(value_.tVal)) DateTime(std::move(v));
}

void Value::setV(const std::unique_ptr<Vertex>& v) {
    type_ = Type::VERTEX;
    new (std::addressof(value_.vVal)) std::unique_ptr<Vertex>(new Vertex(*v));
}

void Value::setV(std::unique_ptr<Vertex>&& v) {
    type_ = Type::VERTEX;
    new (std::addressof(value_.vVal)) std::unique_ptr<Vertex>(std::move(v));
}

void Value::setV(const Vertex& v) {
    type_ = Type::VERTEX;
    new (std::addressof(value_.vVal)) std::unique_ptr<Vertex>(new Vertex(v));
}

void Value::setV(Vertex&& v) {
    type_ = Type::VERTEX;
    new (std::addressof(value_.vVal)) std::unique_ptr<Vertex>(new Vertex(std::move(v)));
}

void Value::setE(const std::unique_ptr<Edge>& v) {
    type_ = Type::EDGE;
    new (std::addressof(value_.eVal)) std::unique_ptr<Edge>(new Edge(*v));
}

void Value::setE(std::unique_ptr<Edge>&& v) {
    type_ = Type::EDGE;
    new (std::addressof(value_.eVal)) std::unique_ptr<Edge>(std::move(v));
}

void Value::setE(const Edge& v) {
    type_ = Type::EDGE;
    new (std::addressof(value_.eVal)) std::unique_ptr<Edge>(new Edge(v));
}

void Value::setE(Edge&& v) {
    type_ = Type::EDGE;
    new (std::addressof(value_.eVal)) std::unique_ptr<Edge>(new Edge(std::move(v)));
}

void Value::setP(const std::unique_ptr<Path>& v) {
    type_ = Type::PATH;
    new (std::addressof(value_.pVal)) std::unique_ptr<Path>(new Path(*v));
}

void Value::setP(std::unique_ptr<Path>&& v) {
    type_ = Type::PATH;
    new (std::addressof(value_.pVal)) std::unique_ptr<Path>(std::move(v));
}

void Value::setP(const Path& v) {
    type_ = Type::PATH;
    new (std::addressof(value_.pVal)) std::unique_ptr<Path>(new Path(v));
}

void Value::setP(Path&& v) {
    type_ = Type::PATH;
    new (std::addressof(value_.pVal)) std::unique_ptr<Path>(new Path(std::move(v)));
}

void Value::setL(const std::unique_ptr<List>& v) {
    type_ = Type::LIST;
    new (std::addressof(value_.lVal)) std::unique_ptr<List>(new List(*v));
}

void Value::setL(std::unique_ptr<List>&& v) {
    type_ = Type::LIST;
    new (std::addressof(value_.lVal)) std::unique_ptr<List>(std::move(v));
}

void Value::setL(const List& v) {
    type_ = Type::LIST;
    new (std::addressof(value_.lVal)) std::unique_ptr<List>(new List(v));
}

void Value::setL(List&& v) {
    type_ = Type::LIST;
    new (std::addressof(value_.lVal)) std::unique_ptr<List>(new List(std::move(v)));
}

void Value::setM(const std::unique_ptr<Map>& v) {
    type_ = Type::MAP;
    new (std::addressof(value_.mVal)) std::unique_ptr<Map>(new Map(*v));
}

void Value::setM(std::unique_ptr<Map>&& v) {
    type_ = Type::MAP;
    new (std::addressof(value_.mVal)) std::unique_ptr<Map>(std::move(v));
}

void Value::setM(const Map& v) {
    type_ = Type::MAP;
    new (std::addressof(value_.mVal)) std::unique_ptr<Map>(new Map(v));
}

void Value::setM(Map&& v) {
    type_ = Type::MAP;
    new (std::addressof(value_.mVal)) std::unique_ptr<Map>(new Map(std::move(v)));
}

void Value::setU(const std::unique_ptr<Set>& v) {
    type_ = Type::SET;
    new (std::addressof(value_.uVal)) std::unique_ptr<Set>(new Set(*v));
}

void Value::setU(std::unique_ptr<Set>&& v) {
    type_ = Type::SET;
    new (std::addressof(value_.uVal)) std::unique_ptr<Set>(std::move(v));
}

void Value::setU(const Set& v) {
    type_ = Type::SET;
    new (std::addressof(value_.uVal)) std::unique_ptr<Set>(new Set(v));
}

void Value::setU(Set&& v) {
    type_ = Type::SET;
    new (std::addressof(value_.vVal)) std::unique_ptr<Set>(new Set(std::move(v)));
}

void Value::setG(const std::unique_ptr<DataSet>& v) {
    type_ = Type::DATASET;
    new (std::addressof(value_.gVal)) std::unique_ptr<DataSet>(new DataSet(*v));
}

void Value::setG(std::unique_ptr<DataSet>&& v) {
    type_ = Type::DATASET;
    new (std::addressof(value_.gVal)) std::unique_ptr<DataSet>(std::move(v));
}

void Value::setG(const DataSet& v) {
    type_ = Type::DATASET;
    new (std::addressof(value_.gVal)) std::unique_ptr<DataSet>(new DataSet(v));
}

void Value::setG(DataSet&& v) {
    type_ = Type::DATASET;
    new (std::addressof(value_.gVal)) std::unique_ptr<DataSet>(new DataSet(std::move(v)));
}

StatusOr<std::string> Value::toString() {
    switch (type_) {
        case Value::Type::__EMPTY__: {
            return std::string("");
        }
        case Value::Type::NULLVALUE: {
            if (getNull() == NullType::__NULL__) {
                return std::string("NULL");
            } else {
                return Status::Error("Value is illegal");
            }
        }
        case Value::Type::BOOL: {
            return getBool() ? "true" : "false";
        }
        case Value::Type::INT: {
            return folly::stringPrintf("%ld", getInt());
        }
        case Value::Type::FLOAT: {
            return folly::stringPrintf("%lf", getFloat());
        }
        case Value::Type::STRING: {
            return getStr();
        }
        case Value::Type::DATE: {
            return getDate().toString();
        }
        case Value::Type::DATETIME: {
            return getDateTime().toString();
        }
        default: {
            return Status::Error("Value can not convert to string");
        }
    }
}

void swap(Value& a, Value& b) {
    Value temp(std::move(a));
    a = std::move(b);
    b = std::move(temp);
}

std::ostream& operator<<(std::ostream& os, const Value::Type& type) {
    switch (type) {
        case Value::Type::__EMPTY__: {
            os << "__EMPTY__";
            break;
        }
        case Value::Type::NULLVALUE: {
            os << "NULL";
            break;
        }
        case Value::Type::BOOL: {
            os << "BOOL";
            break;
        }
        case Value::Type::INT: {
            os << "INT";
            break;
        }
        case Value::Type::FLOAT: {
            os << "FLOAT";
            break;
        }
        case Value::Type::STRING: {
            os << "STRING";
            break;
        }
        case Value::Type::DATE: {
            os << "DATE";
            break;
        }
        case Value::Type::DATETIME: {
            os << "DATETIME";
            break;
        }
        case Value::Type::VERTEX: {
            os << "VERTEX";
            break;
        }
        case Value::Type::EDGE: {
            os << "EDGE";
            break;
        }
        case Value::Type::PATH: {
            os << "PATH";
            break;
        }
        case Value::Type::LIST: {
            os << "LIST";
            break;
        }
        case Value::Type::MAP: {
            os << "MAP";
            break;
        }
        case Value::Type::SET: {
            os << "SET";
            break;
        }
        case Value::Type::DATASET: {
            os << "DATASET";
            break;
        }
        default: {
            os << "__UNKNOWN__";
            break;
        }
    }

    return os;
}


Value operator+(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    switch (lhs.type()) {
        case Value::Type::BOOL: {
            switch (rhs.type()) {
                case Value::Type::STRING: {
                    return folly::stringPrintf("%s%s",
                                               lhs.getBool() ? "true" : "false",
                                               rhs.getStr().c_str());
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getInt() + rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getInt() + rhs.getFloat();
                }
                case Value::Type::STRING: {
                    return folly::stringPrintf("%ld%s",
                                               lhs.getInt(),
                                               rhs.getStr().c_str());
                }
                case Value::Type::DATE: {
                    return rhs.getDate() + lhs.getInt();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getFloat() + rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getFloat() + rhs.getFloat();
                }
                case Value::Type::STRING: {
                    return folly::stringPrintf("%lf%s",
                                               lhs.getFloat(),
                                               rhs.getStr().c_str());
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::STRING: {
            switch (rhs.type()) {
                case Value::Type::BOOL: {
                    return folly::stringPrintf("%s%s",
                                               lhs.getStr().c_str(),
                                               rhs.getBool() ? "true" : "false");
                }
                case Value::Type::INT: {
                    return folly::stringPrintf("%s%ld",
                                               lhs.getStr().c_str(),
                                               rhs.getInt());
                }
                case Value::Type::FLOAT: {
                    return folly::stringPrintf("%s%lf",
                                               lhs.getStr().c_str(),
                                               rhs.getFloat());
                }
                case Value::Type::STRING: {
                    return lhs.getStr() + rhs.getStr();
                }
                case Value::Type::DATE: {
                    return lhs.getStr() + rhs.getDate().toString();
                }
                case Value::Type::DATETIME: {
                    return lhs.getStr() + rhs.getDateTime().toString();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::DATE: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getDate() + rhs.getInt();
                }
                case Value::Type::STRING: {
                    return lhs.getDate().toString() + rhs.getStr();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::DATETIME: {
            switch (rhs.type()) {
                case Value::Type::STRING: {
                    return lhs.getDateTime().toString() + rhs.getStr();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        default: {
            return Value(NullType::BAD_TYPE);
        }
    }
}


Value operator-(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    switch (lhs.type()) {
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getInt() - rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getInt() - rhs.getFloat();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getFloat() - rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getFloat() - rhs.getFloat();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::DATE: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getDate() - rhs.getInt();
                }
                case Value::Type::DATE: {
                    return lhs.getDate().toInt() - rhs.getDate().toInt();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        default: {
            return Value(NullType::BAD_TYPE);
        }
    }
}


Value operator*(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    switch (lhs.type()) {
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getInt() * rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getInt() * rhs.getFloat();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getFloat() * rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getFloat() * rhs.getFloat();
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        default: {
            return Value(NullType::BAD_TYPE);
        }
    }
}


Value operator/(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    switch (lhs.type()) {
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    int64_t denom = rhs.getInt();
                    if (denom != 0) {
                        return lhs.getInt() / denom;
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                case Value::Type::FLOAT: {
                    double denom = rhs.getFloat();
                    if (std::abs(denom) > EPSILON) {
                        return lhs.getInt() / denom;
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    int64_t denom = rhs.getInt();
                    if (denom != 0) {
                        return lhs.getFloat() / denom;
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                case Value::Type::FLOAT: {
                    double denom = rhs.getFloat();
                    if (std::abs(denom) > EPSILON) {
                        return lhs.getFloat() / denom;
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        default: {
            return Value(NullType::BAD_TYPE);
        }
    }
}

Value operator%(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    switch (lhs.type()) {
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    int64_t denom = rhs.getInt();
                    if (denom != 0) {
                        return lhs.getInt() % denom;
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                case Value::Type::FLOAT: {
                    double denom = rhs.getFloat();
                    if (std::abs(denom) > EPSILON) {
                        return std::fmod(lhs.getInt(), denom);
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    int64_t denom = rhs.getInt();
                    if (denom != 0) {
                        return std::fmod(lhs.getFloat(), denom);
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                case Value::Type::FLOAT: {
                    double denom = rhs.getFloat();
                    if (std::abs(denom) > EPSILON) {
                        return std::fmod(lhs.getFloat(), denom);
                    } else {
                        return Value(NullType::DIV_BY_ZERO);
                    }
                }
                default: {
                    return Value(NullType::BAD_TYPE);
                }
            }
        }
        default: {
            return Value(NullType::BAD_TYPE);
        }
    }
}

Value operator-(const Value& rhs) {
    if (rhs.isNull()) {
        return rhs.getNull();
    }

    switch (rhs.type()) {
        case Value::Type::INT: {
            auto val = -rhs.getInt();
            return val;
        }
        case Value::Type::FLOAT: {
            auto val = -rhs.getFloat();
            return val;
        }
        default: {
            return Value(NullType::BAD_TYPE);
        }
    }
}

Value operator!(const Value& rhs) {
    if (rhs.isNull()) {
        return rhs.getNull();
    }

    if (rhs.type() != Value::Type::BOOL) {
        return Value(NullType::BAD_TYPE);
    }

    auto val = rhs.getBool();
    return !val;
}

bool operator<(const Value& lhs, const Value& rhs) {
    if (lhs.isNull() || rhs.isNull()) {
        return false;
    }

    if (!(lhs.isNumeric() && rhs.isNumeric())
            && (lhs.type() != rhs.type())) {
        return false;
    }

    switch (lhs.type()) {
        case Value::Type::BOOL: {
            return lhs.getBool() < rhs.getBool();
        }
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getInt() < rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getInt() < rhs.getFloat();
                }
                default: {
                    return false;
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getFloat() < rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return lhs.getFloat() < rhs.getFloat();
                }
                default: {
                    return false;
                }
            }
        }
        case Value::Type::STRING: {
            return lhs.getStr() < rhs.getStr();
        }
        case Value::Type::DATE: {
            return lhs.getDate() < rhs.getDate();
        }
        case Value::Type::DATETIME:
        case Value::Type::VERTEX:
        case Value::Type::EDGE:
        case Value::Type::PATH:
        case Value::Type::LIST:
        case Value::Type::MAP:
        case Value::Type::SET:
        case Value::Type::DATASET: {
            // TODO:
            return false;
        }
        default: {
            return false;
        }
    }
}

bool operator==(const Value& lhs, const Value& rhs) {
    if (lhs.isNull() && rhs.isNull()) {
        return true;
    } else if (lhs.isNull() || rhs.isNull()) {
        return false;
    }

    if (!(lhs.isNumeric() && rhs.isNumeric())
            && (lhs.type() != rhs.type())) {
        return false;
    }

    switch (lhs.type()) {
        case Value::Type::BOOL: {
            return lhs.getBool() == rhs.getBool();
        }
        case Value::Type::INT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return lhs.getInt() == rhs.getInt();
                }
                case Value::Type::FLOAT: {
                    return std::abs(lhs.getInt() - rhs.getFloat()) < EPSILON;
                }
                default: {
                    return false;
                }
            }
        }
        case Value::Type::FLOAT: {
            switch (rhs.type()) {
                case Value::Type::INT: {
                    return std::abs(lhs.getFloat() - rhs.getInt()) < EPSILON;
                }
                case Value::Type::FLOAT: {
                    return std::abs(lhs.getFloat() - rhs.getFloat()) < EPSILON;
                }
                default: {
                    return false;
                }
            }
        }
        case Value::Type::STRING: {
            return lhs.getStr() == rhs.getStr();
        }
        case Value::Type::DATE: {
            return lhs.getDate() == rhs.getDate();
        }
        case Value::Type::DATETIME: {
            return lhs.getDateTime() == rhs.getDateTime();
        }
        case Value::Type::VERTEX: {
            return lhs.getVertex() == rhs.getVertex();
        }
        case Value::Type::EDGE: {
            return lhs.getEdge() == rhs.getEdge();
        }
        case Value::Type::PATH: {
            return lhs.getPath() == rhs.getPath();
        }
        case Value::Type::LIST: {
            return lhs.getList() == rhs.getList();
        }
        case Value::Type::MAP: {
            return lhs.getMap() == rhs.getMap();
        }
        case Value::Type::SET: {
            return lhs.getSet() == rhs.getSet();
        }
        case Value::Type::DATASET: {
            return lhs.getDataSet() == rhs.getDataSet();
        }
        default: {
            return false;
        }
    }
}

bool operator!=(const Value& lhs, const Value& rhs) {
    return !(lhs == rhs);
}

bool operator> (const Value& lhs, const Value& rhs) {
    return rhs < lhs;
}

bool operator<=(const Value& lhs, const Value& rhs) {
    return !(rhs < lhs);
}

bool operator>=(const Value& lhs, const Value& rhs) {
    return !(lhs < rhs);
}

Value operator&&(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    if (lhs.type() == Value::Type::BOOL
            && rhs.type() == Value::Type::BOOL) {
        return lhs.getBool() && rhs.getBool();
    } else {
        return Value(NullType::BAD_TYPE);
    }
}

Value operator||(const Value& lhs, const Value& rhs) {
    if (lhs.isNull()) {
        return lhs.getNull();
    }

    if (rhs.isNull()) {
        return rhs.getNull();
    }

    if (lhs.type() == Value::Type::BOOL
            && rhs.type() == Value::Type::BOOL) {
        return lhs.getBool() || rhs.getBool();
    } else {
        return Value(NullType::BAD_TYPE);
    }
}
}  // namespace nebula
