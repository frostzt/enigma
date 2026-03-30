/**
 * @file encoding.h
 * @brief Utility to encode/decode unsigned integer types to and from raw bytes.
 *
 * All values are written and stored in big-endian (network) byte order.
 * The caller is responsible for ensuring that the buffer has sufficient
 * space starting from @p offset for the width of the type being encoded
 * or decoded.
 *
 * @author frostzt
 * @date 2026-03-30
 */

#ifndef ENIGMA_DB_ENCODING_H
#define ENIGMA_DB_ENCODING_H

#include <cstddef>
#include <cstdint>

/**
 * @brief Encodes a uint8_t into @p buffer at the given @p offset.
 *
 * @param value           The value to encode.
 * @param[in,out] buffer  Destination buffer; must have at least offset + 1
 * bytes.
 * @param offset          Byte offset into @p buffer at which to begin writing.
 * @return The offset immediately past the written byte (offset + 1),
 *         suitable for chaining sequential encodes.
 */
size_t encode_uint8(uint8_t value, char* buffer, size_t offset);

/**
 * @brief Decodes a uint8_t from @p buffer at the given @p offset.
 *
 * @param[in] buffer  Source buffer; must have at least offset + 1 readable
 * bytes.
 * @param offset      Byte offset into @p buffer from which to read.
 * @return The decoded value.
 */
uint8_t decode_uint8(const char* buffer, size_t offset);

/**
 * @brief Encodes a uint16_t into @p buffer at the given @p offset in big-endian
 * order.
 *
 * @param value           The value to encode.
 * @param[in,out] buffer  Destination buffer; must have at least offset + 2
 * bytes.
 * @param offset          Byte offset into @p buffer at which to begin writing.
 * @return The offset immediately past the written bytes (offset + 2).
 */
size_t encode_uint16(uint16_t value, char* buffer, size_t offset);

/**
 * @brief Decodes a uint16_t from @p buffer at the given @p offset,
 *        interpreting the bytes as big-endian.
 *
 * @param[in] buffer  Source buffer; must have at least offset + 2 readable
 * bytes.
 * @param offset      Byte offset into @p buffer from which to read.
 * @return The decoded value in host byte order.
 */
uint16_t decode_uint16(const char* buffer, size_t offset);

/**
 * @brief Encodes a uint32_t into @p buffer at the given @p offset in big-endian
 * order.
 *
 * @param value           The value to encode.
 * @param[in,out] buffer  Destination buffer; must have at least offset + 4
 * bytes.
 * @param offset          Byte offset into @p buffer at which to begin writing.
 * @return The offset immediately past the written bytes (offset + 4).
 */
size_t encode_uint32(uint32_t value, char* buffer, size_t offset);

/**
 * @brief Decodes a uint32_t from @p buffer at the given @p offset,
 *        interpreting the bytes as big-endian.
 *
 * @param[in] buffer  Source buffer; must have at least offset + 4 readable
 * bytes.
 * @param offset      Byte offset into @p buffer from which to read.
 * @return The decoded value in host byte order.
 */
uint32_t decode_uint32(const char* buffer, size_t offset);

/**
 * @brief Encodes a uint64_t into @p buffer at the given @p offset in big-endian
 * order.
 *
 * @param value           The value to encode.
 * @param[in,out] buffer  Destination buffer; must have at least offset + 8
 * bytes.
 * @param offset          Byte offset into @p buffer at which to begin writing.
 * @return The offset immediately past the written bytes (offset + 8).
 */
size_t encode_uint64(uint64_t value, char* buffer, size_t offset);

/**
 * @brief Decodes a uint64_t from @p buffer at the given @p offset,
 *        interpreting the bytes as big-endian.
 *
 * @param[in] buffer  Source buffer; must have at least offset + 8 readable
 * bytes.
 * @param offset      Byte offset into @p buffer from which to read.
 * @return The decoded value in host byte order.
 */
uint64_t decode_uint64(const char* buffer, size_t offset);

#endif  // ENIGMA_DB_ENCODING_H
