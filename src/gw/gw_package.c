/*
 * Bounded LCD-Game-Shrinker package parser.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gw_package.h"

#include <stdlib.h>
#include <string.h>

#define GW_FLAG_SEGMENTS_4BIT UINT32_C(0x10)
#define GW_FLAG_BACKGROUND_JPEG UINT32_C(0x20)
#define GW_FLAG_SEGMENTS_2BIT UINT32_C(0x100)
#define LZ4_MAGIC UINT32_C(0x184d2204)

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

static int span_fits(size_t total, size_t offset, size_t length)
{
    return offset <= total && length <= total - offset;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;
    unsigned bit;

    for (i = 0; i < size; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static GWPackageResult lz4_decompress_block(const uint8_t *source,
                                             size_t source_size,
                                             uint8_t *destination,
                                             size_t destination_capacity,
                                             size_t *written)
{
    size_t src = 0;
    size_t dst = 0;

    while (src < source_size) {
        uint8_t token = source[src++];
        size_t literal_length = token >> 4;
        size_t match_length;
        uint16_t match_offset;

        if (literal_length == 15) {
            uint8_t extension;
            do {
                if (src >= source_size)
                    return GW_PACKAGE_ERR_COMPRESSION;
                extension = source[src++];
                if (literal_length > SIZE_MAX - extension)
                    return GW_PACKAGE_ERR_COMPRESSION;
                literal_length += extension;
            } while (extension == 255);
        }

        if (!span_fits(source_size, src, literal_length) ||
            !span_fits(destination_capacity, dst, literal_length))
            return GW_PACKAGE_ERR_COMPRESSION;
        memcpy(destination + dst, source + src, literal_length);
        src += literal_length;
        dst += literal_length;

        if (src == source_size)
            break;
        if (!span_fits(source_size, src, 2))
            return GW_PACKAGE_ERR_COMPRESSION;

        match_offset = read_le16(source + src);
        src += 2;
        if (match_offset == 0 || match_offset > dst)
            return GW_PACKAGE_ERR_COMPRESSION;

        match_length = (token & 0x0f) + 4u;
        if ((token & 0x0f) == 15) {
            uint8_t extension;
            do {
                if (src >= source_size)
                    return GW_PACKAGE_ERR_COMPRESSION;
                extension = source[src++];
                if (match_length > SIZE_MAX - extension)
                    return GW_PACKAGE_ERR_COMPRESSION;
                match_length += extension;
            } while (extension == 255);
        }

        if (!span_fits(destination_capacity, dst, match_length))
            return GW_PACKAGE_ERR_COMPRESSION;
        while (match_length-- != 0) {
            destination[dst] = destination[dst - match_offset];
            ++dst;
        }
    }

    *written = dst;
    return GW_PACKAGE_OK;
}

static GWPackageResult decode_lz4_frame(const uint8_t *source,
                                        size_t source_size,
                                        uint8_t **payload,
                                        size_t *payload_size,
                                        size_t *frame_size)
{
    uint8_t flags;
    size_t cursor = 4;
    uint64_t content_size;
    uint8_t *output;
    size_t output_cursor = 0;

    if (!span_fits(source_size, 0, 7) || read_le32(source) != LZ4_MAGIC)
        return GW_PACKAGE_ERR_FORMAT;

    flags = source[cursor++];
    if ((flags & 0xc0) != 0x40 || (flags & 0x20) == 0 ||
        (flags & 0x02) != 0 || (flags & 0x01) != 0)
        return GW_PACKAGE_ERR_COMPRESSION;

    /* BD is consumed but only its reserved bits and maximum block code matter. */
    if ((source[cursor] & 0x8f) != 0 || ((source[cursor] >> 4) & 7) < 4 ||
        ((source[cursor] >> 4) & 7) > 7)
        return GW_PACKAGE_ERR_COMPRESSION;
    ++cursor;

    if ((flags & 0x08) == 0 || !span_fits(source_size, cursor, 8 + 1))
        return GW_PACKAGE_ERR_COMPRESSION;
    content_size = read_le64(source + cursor);
    cursor += 8;
    ++cursor; /* descriptor checksum */

    if (content_size < GW_PACKAGE_HEADER_SIZE ||
        content_size > GW_PACKAGE_MAX_PAYLOAD_SIZE)
        return GW_PACKAGE_ERR_TOO_LARGE;

    output = malloc((size_t)content_size);
    if (output == NULL)
        return GW_PACKAGE_ERR_MEMORY;

    for (;;) {
        uint32_t block_header;
        size_t block_size;
        int uncompressed;
        size_t block_written = 0;
        GWPackageResult result;

        if (!span_fits(source_size, cursor, 4)) {
            free(output);
            return GW_PACKAGE_ERR_COMPRESSION;
        }
        block_header = read_le32(source + cursor);
        cursor += 4;
        if (block_header == 0)
            break;

        uncompressed = (block_header & UINT32_C(0x80000000)) != 0;
        block_size = block_header & UINT32_C(0x7fffffff);
        if (block_size == 0 || !span_fits(source_size, cursor, block_size)) {
            free(output);
            return GW_PACKAGE_ERR_COMPRESSION;
        }

        if (uncompressed) {
            if (!span_fits((size_t)content_size, output_cursor, block_size)) {
                free(output);
                return GW_PACKAGE_ERR_COMPRESSION;
            }
            memcpy(output + output_cursor, source + cursor, block_size);
            block_written = block_size;
        } else {
            result = lz4_decompress_block(source + cursor, block_size,
                                          output + output_cursor,
                                          (size_t)content_size - output_cursor,
                                          &block_written);
            if (result != GW_PACKAGE_OK) {
                free(output);
                return result;
            }
        }
        cursor += block_size;
        output_cursor += block_written;

        if (flags & 0x10) {
            if (!span_fits(source_size, cursor, 4)) {
                free(output);
                return GW_PACKAGE_ERR_COMPRESSION;
            }
            cursor += 4;
        }
    }

    if (flags & 0x04) {
        if (!span_fits(source_size, cursor, 4)) {
            free(output);
            return GW_PACKAGE_ERR_COMPRESSION;
        }
        cursor += 4;
    }

    if (output_cursor != (size_t)content_size) {
        free(output);
        return GW_PACKAGE_ERR_COMPRESSION;
    }

    *payload = output;
    *payload_size = output_cursor;
    *frame_size = cursor;
    return GW_PACKAGE_OK;
}

static GWPackageObject read_object(const uint8_t *header, size_t index)
{
    GWPackageObject object;
    size_t cursor = 28 + index * 8;
    object.offset = read_le32(header + cursor);
    object.size = read_le32(header + cursor + 4);
    return object;
}

static int object_valid(const GWPackageObject *object, size_t payload_size,
                        size_t alignment)
{
    return (object->offset % alignment) == 0 &&
           span_fits(payload_size, object->offset, object->size);
}

static GWPackageResult validate_segments(const GWPackage *package)
{
    const uint8_t *payload = package->payload;
    size_t i;
    unsigned pixels_per_byte = 1;

    if (package->segment_offsets.size != GW_PACKAGE_SEGMENT_COUNT * 4 ||
        package->segment_x.size != GW_PACKAGE_SEGMENT_COUNT * 2 ||
        package->segment_y.size != GW_PACKAGE_SEGMENT_COUNT * 2 ||
        package->segment_height.size != GW_PACKAGE_SEGMENT_COUNT * 2 ||
        package->segment_width.size != GW_PACKAGE_SEGMENT_COUNT * 2)
        return GW_PACKAGE_ERR_FORMAT;

    if (package->flags & GW_FLAG_SEGMENTS_2BIT)
        pixels_per_byte = 4;
    else if (package->flags & GW_FLAG_SEGMENTS_4BIT)
        pixels_per_byte = 2;

    if ((package->flags & GW_FLAG_SEGMENTS_2BIT) &&
        (package->flags & GW_FLAG_SEGMENTS_4BIT))
        return GW_PACKAGE_ERR_FORMAT;

    for (i = 0; i < GW_PACKAGE_SEGMENT_COUNT; ++i) {
        uint32_t pixel_offset = read_le32(payload + package->segment_offsets.offset + i * 4);
        uint32_t x = read_le16(payload + package->segment_x.offset + i * 2);
        uint32_t y = read_le16(payload + package->segment_y.offset + i * 2);
        uint32_t width = read_le16(payload + package->segment_width.offset + i * 2);
        uint32_t height = read_le16(payload + package->segment_height.offset + i * 2);
        uint64_t pixels;
        uint64_t byte_end;

        if (x > 320 || y > 240 || width > 320 - x || height > 240 - y)
            return GW_PACKAGE_ERR_FORMAT;
        if (width == 0 || height == 0)
            continue;

        pixels = (uint64_t)width * height;
        byte_end = ((uint64_t)pixel_offset + pixels + pixels_per_byte - 1) /
                   pixels_per_byte;
        if (byte_end > package->segments.size)
            return GW_PACKAGE_ERR_FORMAT;
    }

    return GW_PACKAGE_OK;
}

static GWPackageResult parse_payload(GWPackage *package)
{
    const uint8_t *header = package->payload;
    GWPackageObject *objects[] = {
        &package->background, &package->segments, &package->segment_offsets,
        &package->segment_x, &package->segment_y, &package->segment_height,
        &package->segment_width, &package->melody, &package->program,
        &package->keyboard
    };
    size_t i;
    GWPackageResult result;

    if (package->payload_size < GW_PACKAGE_HEADER_SIZE)
        return GW_PACKAGE_ERR_FORMAT;

    memcpy(package->cpu_name, header, 8);
    memcpy(package->signature, header + 8, 8);
    memcpy(package->time_addresses, header + 16, 7);
    if (header[23] != 0)
        return GW_PACKAGE_ERR_FORMAT;
    package->flags = read_le32(header + 24);
    if ((package->flags & ~UINT32_C(0x1ff)) != 0 ||
        ((package->flags >> 1) & 7u) > 5u)
        return GW_PACKAGE_ERR_FORMAT;

    for (i = 0; i < 10; ++i) {
        *objects[i] = read_object(header, i);
        if (!object_valid(objects[i], package->payload_size, 4))
            return GW_PACKAGE_ERR_FORMAT;
        if (i == 0 && objects[i]->size == 0)
            continue;
        if (objects[i]->offset < GW_PACKAGE_HEADER_SIZE)
            return GW_PACKAGE_ERR_FORMAT;
        if (i > 0 && objects[i]->offset < objects[i - 1]->offset + objects[i - 1]->size)
            return GW_PACKAGE_ERR_FORMAT;
    }

    if (package->keyboard.size != 10 * 4 ||
        package->keyboard.offset + package->keyboard.size != package->payload_size)
        return GW_PACKAGE_ERR_FORMAT;

    package->game = gw_game_db_find(package->signature, package->cpu_name);
    if (package->game == NULL)
        return GW_PACKAGE_ERR_UNSUPPORTED_GAME;
    if (!package->game->enabled)
        return GW_PACKAGE_ERR_NOT_ENABLED;
    if (package->game->display_config != GW_DISPLAY_SINGLE_320X240)
        return GW_PACKAGE_ERR_UNSUPPORTED_GAME;
    if (package->program.size != package->game->program_size)
        return GW_PACKAGE_ERR_FORMAT;
    if (crc32_bytes(package->payload + package->program.offset,
                    package->program.size) != package->game->program_crc32)
        return GW_PACKAGE_ERR_CHECKSUM;

    if ((package->flags & GW_FLAG_BACKGROUND_JPEG) != 0) {
        if (package->background.size != 0 || package->jpeg_size == 0)
            return GW_PACKAGE_ERR_FORMAT;
    } else if (package->background.size != 320u * 240u * 2u ||
               package->jpeg_size != 0) {
        return GW_PACKAGE_ERR_FORMAT;
    }

    result = validate_segments(package);
    return result;
}

GWPackageResult gw_package_parse(const uint8_t *file_data, size_t file_size,
                                 GWPackage *package)
{
    GWPackageResult result;
    size_t frame_size = 0;

    if (file_data == NULL || package == NULL || file_size < 4)
        return GW_PACKAGE_ERR_FORMAT;
    memset(package, 0, sizeof(*package));
    if (file_size > GW_PACKAGE_MAX_FILE_SIZE)
        return GW_PACKAGE_ERR_TOO_LARGE;

    if (read_le32(file_data) == LZ4_MAGIC) {
        result = decode_lz4_frame(file_data, file_size, &package->payload,
                                  &package->payload_size, &frame_size);
        if (result != GW_PACKAGE_OK)
            return result;
        package->jpeg_size = file_size - frame_size;
        if (package->jpeg_size != 0) {
            package->jpeg_data = malloc(package->jpeg_size);
            if (package->jpeg_data == NULL) {
                gw_package_free(package);
                return GW_PACKAGE_ERR_MEMORY;
            }
            memcpy(package->jpeg_data, file_data + frame_size,
                   package->jpeg_size);
        }
    } else if (memcmp(file_data, "SM5", 3) == 0) {
        if (file_size > GW_PACKAGE_MAX_PAYLOAD_SIZE)
            return GW_PACKAGE_ERR_TOO_LARGE;
        package->payload = malloc(file_size);
        if (package->payload == NULL)
            return GW_PACKAGE_ERR_MEMORY;
        memcpy(package->payload, file_data, file_size);
        package->payload_size = file_size;
    } else {
        return GW_PACKAGE_ERR_FORMAT;
    }

    result = parse_payload(package);
    if (result != GW_PACKAGE_OK)
        gw_package_free(package);
    return result;
}

void gw_package_free(GWPackage *package)
{
    if (package != NULL) {
        free(package->payload);
        free(package->jpeg_data);
        memset(package, 0, sizeof(*package));
    }
}

const char *gw_package_result_string(GWPackageResult result)
{
    switch (result) {
    case GW_PACKAGE_OK: return "Package is valid";
    case GW_PACKAGE_ERR_IO: return "The package could not be read";
    case GW_PACKAGE_ERR_TOO_LARGE: return "The package is too large";
    case GW_PACKAGE_ERR_MEMORY: return "Not enough memory to load the package";
    case GW_PACKAGE_ERR_FORMAT: return "Malformed or unsupported .gw format";
    case GW_PACKAGE_ERR_COMPRESSION: return "Invalid or unsupported LZ4 data";
    case GW_PACKAGE_ERR_UNSUPPORTED_GAME: return "This game is not in the supported whitelist";
    case GW_PACKAGE_ERR_NOT_ENABLED: return "This whitelisted game is not enabled yet";
    case GW_PACKAGE_ERR_CHECKSUM: return "The program ROM checksum does not match";
    default: return "Unknown package error";
    }
}
