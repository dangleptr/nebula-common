/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#ifndef DATATYPES_DATEOPS_H_
#define DATATYPES_DATEOPS_H_

#include "base/Base.h"

#include <thrift/lib/cpp2/GeneratedSerializationCodeHelper.h>
#include <thrift/lib/cpp2/gen/module_types_tcc.h>
#include <thrift/lib/cpp2/protocol/ProtocolReaderStructReadState.h>

#include "datatypes/Date.h"

namespace apache {
namespace thrift {

/**************************************
 *
 * Ops for class Date
 *
 *************************************/
namespace detail {

template <>
struct TccStructTraits<nebula::Date> {
    static void translateFieldName(
            MAYBE_UNUSED folly::StringPiece _fname,
            MAYBE_UNUSED int16_t& fid,
            MAYBE_UNUSED apache::thrift::protocol::TType& _ftype) {
        if (_fname == "year") {
            fid = 1;
            _ftype = apache::thrift::protocol::T_I16;
        } else if (_fname == "month") {
            fid = 2;
            _ftype = apache::thrift::protocol::T_BYTE;
        } else if (_fname == "day") {
            fid = 3;
            _ftype = apache::thrift::protocol::T_BYTE;
        }
    }
};

}  // namespace detail


template<>
inline void Cpp2Ops<nebula::Date>::clear(nebula::Date* obj) {
  return obj->clear();
}


template<>
inline constexpr protocol::TType Cpp2Ops<nebula::Date>::thriftType() {
  return apache::thrift::protocol::T_STRUCT;
}


template<>
template<class Protocol>
uint32_t Cpp2Ops<nebula::Date>::write(Protocol* proto, nebula::Date const* obj) {
    uint32_t xfer = 0;
    xfer += proto->writeStructBegin("Date");

    xfer += proto->writeFieldBegin("year", apache::thrift::protocol::T_I16, 1);
    xfer += detail::pm::protocol_methods<type_class::integral, int16_t>
        ::write(*proto, obj->year);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("month", apache::thrift::protocol::T_BYTE, 2);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::write(*proto, obj->month);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("day", apache::thrift::protocol::T_BYTE, 3);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::write(*proto, obj->day);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldStop();
    xfer += proto->writeStructEnd();

    return xfer;
}


template<>
template<class Protocol>
void Cpp2Ops<nebula::Date>::read(Protocol* proto, nebula::Date* obj) {
    detail::ProtocolReaderStructReadState<Protocol> readState;

    readState.readStructBegin(proto);

    using apache::thrift::TProtocolException;

    if (UNLIKELY(!readState.advanceToNextField(proto, 0, 1, protocol::T_I16))) {
        goto _loop;
    }

_readField_year:
    {
        detail::pm::protocol_methods<type_class::integral, int16_t>::read(*proto, obj->year);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 1, 2, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_month:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->month);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 2, 3, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_day:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->day);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 3, 0, protocol::T_STOP))) {
        goto _loop;
    }

_end:
    readState.readStructEnd(proto);

    return;

_loop:
    if (readState.fieldType == apache::thrift::protocol::T_STOP) {
        goto _end;
    }
    if (proto->kUsesFieldNames()) {
        detail::TccStructTraits<nebula::Date>::translateFieldName(
            readState.fieldName(), readState.fieldId, readState.fieldType);
    }

    switch (readState.fieldId) {
        case 1:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_I16)) {
                goto _readField_year;
            } else {
                goto _skip;
            }
        }
        case 2:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_month;
            } else {
                goto _skip;
            }
        }
        case 3:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_day;
            } else {
                goto _skip;
            }
        }
        default:
        {
_skip:
            proto->skip(readState.fieldType);
            readState.readFieldEnd(proto);
            readState.readFieldBeginNoInline(proto);
            goto _loop;
        }
    }
}


template<>
template<class Protocol>
uint32_t Cpp2Ops<nebula::Date>::serializedSize(Protocol const* proto,
                                               nebula::Date const* obj) {
    uint32_t xfer = 0;
    xfer += proto->serializedStructSize("Date");

    xfer += proto->serializedFieldSize("year", apache::thrift::protocol::T_I16, 1);
    xfer += detail::pm::protocol_methods<type_class::integral, int16_t>
        ::serializedSize<false>(*proto, obj->year);

    xfer += proto->serializedFieldSize("month", apache::thrift::protocol::T_BYTE, 2);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->month);

    xfer += proto->serializedFieldSize("day", apache::thrift::protocol::T_BYTE, 3);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->day);

    xfer += proto->serializedSizeStop();
    return xfer;
}


template<>
template<class Protocol>
uint32_t Cpp2Ops<nebula::Date>::serializedSizeZC(Protocol const* proto,
                                                 nebula::Date const* obj) {
    uint32_t xfer = 0;
    xfer += proto->serializedStructSize("Date");

    xfer += proto->serializedFieldSize("year", apache::thrift::protocol::T_I16, 1);
    xfer += detail::pm::protocol_methods<type_class::integral, int16_t>
        ::serializedSize<false>(*proto, obj->year);

    xfer += proto->serializedFieldSize("month", apache::thrift::protocol::T_BYTE, 2);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->month);

    xfer += proto->serializedFieldSize("day", apache::thrift::protocol::T_BYTE, 3);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->day);

    xfer += proto->serializedSizeStop();
    return xfer;
}


/**************************************
 *
 * Ops for class DateTime
 *
 *************************************/
namespace detail {

template <>
struct TccStructTraits<nebula::DateTime> {
    static void translateFieldName(
            MAYBE_UNUSED folly::StringPiece _fname,
            MAYBE_UNUSED int16_t& fid,
            MAYBE_UNUSED apache::thrift::protocol::TType& _ftype) {
        if (_fname == "year") {
            fid = 1;
            _ftype = apache::thrift::protocol::T_I16;
        } else if (_fname == "month") {
            fid = 2;
            _ftype = apache::thrift::protocol::T_BYTE;
        } else if (_fname == "day") {
            fid = 3;
            _ftype = apache::thrift::protocol::T_BYTE;
        } else if (_fname == "hour") {
            fid = 4;
            _ftype = apache::thrift::protocol::T_BYTE;
        } else if (_fname == "minute") {
            fid = 5;
            _ftype = apache::thrift::protocol::T_BYTE;
        } else if (_fname == "sec") {
            fid = 6;
            _ftype = apache::thrift::protocol::T_BYTE;
        } else if (_fname == "microsec") {
            fid = 7;
            _ftype = apache::thrift::protocol::T_I32;
        } else if (_fname == "timezone") {
            fid = 8;
            _ftype = apache::thrift::protocol::T_I32;
        }
    }
};

}  // namespace detail


template<>
inline void Cpp2Ops<nebula::DateTime>::clear(nebula::DateTime* obj) {
  return obj->clear();
}


template<>
inline constexpr protocol::TType Cpp2Ops<nebula::DateTime>::thriftType() {
  return apache::thrift::protocol::T_STRUCT;
}


template<>
template<class Protocol>
uint32_t Cpp2Ops<nebula::DateTime>::write(Protocol* proto, nebula::DateTime const* obj) {
    uint32_t xfer = 0;
    xfer += proto->writeStructBegin("DateTime");

    xfer += proto->writeFieldBegin("year", apache::thrift::protocol::T_I16, 1);
    xfer += detail::pm::protocol_methods<type_class::integral, int16_t>
        ::write(*proto, obj->year);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("month", apache::thrift::protocol::T_BYTE, 2);
    xfer += detail::pm::protocol_methods<thrift::type_class::integral, int8_t>
        ::write(*proto, obj->month);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("day", apache::thrift::protocol::T_BYTE, 3);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::write(*proto, obj->day);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("hour", apache::thrift::protocol::T_BYTE, 4);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::write(*proto, obj->hour);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("minute", apache::thrift::protocol::T_BYTE, 5);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::write(*proto, obj->minute);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("sec", apache::thrift::protocol::T_BYTE, 6);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::write(*proto, obj->sec);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("microsec", apache::thrift::protocol::T_I32, 7);
    xfer += detail::pm::protocol_methods<type_class::integral, int32_t>
        ::write(*proto, obj->microsec);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldBegin("timezone", apache::thrift::protocol::T_I32, 8);
    xfer += detail::pm::protocol_methods<type_class::integral, int32_t>
        ::write(*proto, obj->timezone);
    xfer += proto->writeFieldEnd();

    xfer += proto->writeFieldStop();
    xfer += proto->writeStructEnd();
    return xfer;
}


template<>
template<class Protocol>
void Cpp2Ops<nebula::DateTime>::read(Protocol* proto, nebula::DateTime* obj) {
    detail::ProtocolReaderStructReadState<Protocol> readState;

    readState.readStructBegin(proto);

    using apache::thrift::TProtocolException;

    if (UNLIKELY(!readState.advanceToNextField(proto, 0, 1, protocol::T_I16))) {
        goto _loop;
    }

_readField_year:
    {
        detail::pm::protocol_methods<type_class::integral, int16_t>::read(*proto, obj->year);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 1, 2, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_month:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->month);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 2, 3, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_day:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->day);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 3, 4, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_hour:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->hour);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 4, 5, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_minute:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->minute);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 5, 6, protocol::T_BYTE))) {
        goto _loop;
    }

_readField_sec:
    {
        detail::pm::protocol_methods<type_class::integral, int8_t>::read(*proto, obj->sec);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 6, 7, protocol::T_I32))) {
        goto _loop;
    }

_readField_microsec:
    {
        detail::pm::protocol_methods<type_class::integral, int32_t>
            ::read(*proto, obj->microsec);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 7, 8, protocol::T_I32))) {
        goto _loop;
    }

_readField_timezone:
    {
        detail::pm::protocol_methods<type_class::integral, int32_t>
            ::read(*proto, obj->timezone);
    }

    if (UNLIKELY(!readState.advanceToNextField(proto, 8, 0, protocol::T_STOP))) {
        goto _loop;
    }

_end:
    readState.readStructEnd(proto);

    return;

_loop:
    if (readState.fieldType == apache::thrift::protocol::T_STOP) {
        goto _end;
    }

    if (proto->kUsesFieldNames()) {
        detail::TccStructTraits<nebula::DateTime>::translateFieldName(
            readState.fieldName(), readState.fieldId, readState.fieldType);
    }

    switch (readState.fieldId) {
        case 1:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_I16)) {
                goto _readField_year;
            } else {
                goto _skip;
            }
        }
        case 2:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_month;
            } else {
                goto _skip;
            }
        }
        case 3:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_day;
            } else {
                goto _skip;
            }
        }
        case 4:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_hour;
            } else {
                goto _skip;
            }
        }
        case 5:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_minute;
            } else {
                goto _skip;
            }
        }
        case 6:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_BYTE)) {
                goto _readField_sec;
            } else {
                goto _skip;
            }
        }
        case 7:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_I32)) {
                goto _readField_microsec;
            } else {
                goto _skip;
            }
        }
        case 8:
        {
            if (LIKELY(readState.fieldType == apache::thrift::protocol::T_I32)) {
                goto _readField_timezone;
            } else {
                goto _skip;
            }
        }
        default:
        {
_skip:
            proto->skip(readState.fieldType);
            readState.readFieldEnd(proto);
            readState.readFieldBeginNoInline(proto);
            goto _loop;
        }
    }
}


template<>
template<class Protocol>
uint32_t Cpp2Ops<nebula::DateTime>::serializedSize(Protocol const* proto,
                                                   nebula::DateTime const* obj) {
    uint32_t xfer = 0;
    xfer += proto->serializedStructSize("DateTime");

    xfer += proto->serializedFieldSize("year", apache::thrift::protocol::T_I16, 1);
    xfer += detail::pm::protocol_methods<type_class::integral, int16_t>
        ::serializedSize<false>(*proto, obj->year);

    xfer += proto->serializedFieldSize("month", apache::thrift::protocol::T_BYTE, 2);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->month);

    xfer += proto->serializedFieldSize("day", apache::thrift::protocol::T_BYTE, 3);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->day);

    xfer += proto->serializedFieldSize("hour", apache::thrift::protocol::T_BYTE, 4);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->hour);

    xfer += proto->serializedFieldSize("minute", apache::thrift::protocol::T_BYTE, 5);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->minute);

    xfer += proto->serializedFieldSize("sec", apache::thrift::protocol::T_BYTE, 6);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->sec);

    xfer += proto->serializedFieldSize("microsec", apache::thrift::protocol::T_I32, 7);
    xfer += detail::pm::protocol_methods<type_class::integral, int32_t>
        ::serializedSize<false>(*proto, obj->microsec);

    xfer += proto->serializedFieldSize("timezone", apache::thrift::protocol::T_I32, 8);
    xfer += detail::pm::protocol_methods<type_class::integral, int32_t>
        ::serializedSize<false>(*proto, obj->timezone);

    xfer += proto->serializedSizeStop();
    return xfer;
}


template<>
template<class Protocol>
uint32_t Cpp2Ops<nebula::DateTime>::serializedSizeZC(Protocol const* proto,
                                                     nebula::DateTime const* obj) {
    uint32_t xfer = 0;
    xfer += proto->serializedStructSize("DateTime");

    xfer += proto->serializedFieldSize("year", apache::thrift::protocol::T_I16, 1);
    xfer += detail::pm::protocol_methods<type_class::integral, int16_t>
        ::serializedSize<false>(*proto, obj->year);

    xfer += proto->serializedFieldSize("month", apache::thrift::protocol::T_BYTE, 2);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->month);

    xfer += proto->serializedFieldSize("day", apache::thrift::protocol::T_BYTE, 3);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->day);

    xfer += proto->serializedFieldSize("hour", apache::thrift::protocol::T_BYTE, 4);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->hour);

    xfer += proto->serializedFieldSize("minute", apache::thrift::protocol::T_BYTE, 5);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->minute);

    xfer += proto->serializedFieldSize("sec", apache::thrift::protocol::T_BYTE, 6);
    xfer += detail::pm::protocol_methods<type_class::integral, int8_t>
        ::serializedSize<false>(*proto, obj->sec);

    xfer += proto->serializedFieldSize("microsec", apache::thrift::protocol::T_I32, 7);
    xfer += detail::pm::protocol_methods<type_class::integral, int32_t>
        ::serializedSize<false>(*proto, obj->microsec);

    xfer += proto->serializedFieldSize("timezone", apache::thrift::protocol::T_I32, 8);
    xfer += detail::pm::protocol_methods<type_class::integral, int32_t>
        ::serializedSize<false>(*proto, obj->timezone);

    xfer += proto->serializedSizeStop();
    return xfer;
}

}  // namespace thrift
}  // namespace apache
#endif  // DATATYPES_DATEOPS_H_
