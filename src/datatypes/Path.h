/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#ifndef DATATYPES_PATH_H_
#define DATATYPES_PATH_H_

#include "base/Base.h"
#include "thrift/ThriftTypes.h"
#include "datatypes/Value.h"
#include "datatypes/Vertex.h"

namespace nebula {

struct Step {
    Vertex dst;
    EdgeType type;
    std::string name;
    EdgeRanking ranking;
    std::unordered_map<std::string, Value> props;

    Step() = default;
    Step(const Step& s) : dst(s.dst)
                        , type(s.type)
                        , name(s.name)
                        , ranking(s.ranking)
                        , props(s.props) {}
    Step(Step&& s) : dst(std::move(s.dst))
                   , type(std::move(s.type))
                   , name(std::move(s.name))
                   , ranking(std::move(s.ranking))
                   , props(std::move(s.props)) {}

    void clear() {
        dst.clear();
        type = 0;
        name.clear();
        ranking = 0;
        props.clear();
    }

    bool operator==(const Step& rhs) const {
        return dst == rhs.dst &&
               type == rhs.type &&
               ranking == rhs.ranking &&
               props == rhs.props;
    }
};


struct Path {
    Vertex src;
    std::vector<Step> steps;

    Path() = default;
    Path(const Path& p) : src(p.src), steps(p.steps) {}
    Path(Path&& p) : src(std::move(p.src)), steps(std::move(p.steps)) {}

    void clear() {
        src.clear();
        steps.clear();
    }

    bool operator==(const Path& rhs) const {
        return src == rhs.src &&
               steps == rhs.steps;
    }
};

}  // namespace nebula


namespace std {

template<>
struct hash<nebula::Step> {
    std::size_t operator()(const nebula::Step& h) const noexcept {
        size_t hv = hash<nebula::Vertex>()(h.dst);
        hv = folly::hash::fnv64_buf(reinterpret_cast<const void*>(&h.type),
                                    sizeof(h.type),
                                    hv);
        return folly::hash::fnv64_buf(reinterpret_cast<const void*>(&h.ranking),
                                      sizeof(h.ranking),
                                      hv);
    }
};


template<>
struct hash<nebula::Path> {
    std::size_t operator()(const nebula::Path& h) const noexcept {
    size_t hv = hash<nebula::Vertex>()(h.src);
        for (auto& s : h.steps) {
            hv += (hv << 1) + (hv << 4) + (hv << 5) + (hv << 7) + (hv << 8) + (hv << 40);
            hv ^= hash<nebula::Step>()(s);
        }

        return hv;
    }
};

}  // namespace std
#endif  // DATATYPES_PATH_H_

