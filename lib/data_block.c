#define LOG_PREFIX " data_block"
#include <tetrapol/log.h>
#include <tetrapol/data_block.h>

#include <limits.h>
#include <string.h>

// http://ghsi.de/CRC/index.php?Polynom=10010
static void mk_crc5(uint8_t *res, const uint8_t *input, int input_len)
{
    uint8_t inv;
    memset(res, 0, 5);

    for (int i = 0; i < input_len; ++i)
    {
        inv = input[i] ^ res[0];

        res[0] = res[1];
        res[1] = res[2];
        res[2] = res[3] ^ inv;
        res[3] = res[4];
        res[4] = inv;
    }
}

// http://ghsi.de/CRC/index.php?Polynom=1010
static void mk_crc3(uint8_t *res, const uint8_t *input, int input_len)
{
    uint8_t inv;
    memset(res, 0, 3);

    for (int i = 0; i < input_len; ++i)
    {
        inv = input[i] ^ res[0];

        res[0] = res[1];
        res[1] = res[2] ^ inv;
        res[2] = inv;
    }
    res[0] = res[0] ^ 1;
    res[1] = res[1] ^ 1;
    res[2] = res[2] ^ 1;
}
/**
  PAS 0001-2 6.1.2
  PAS 0001-2 6.2.2
  */
static int decode_data_frame(uint8_t *res, uint8_t *err, const uint8_t *in, int res_len)
{
#ifdef GET_IN_
#error "Collision in definition of macro GET_IN_!"
#endif
#define GET_IN_(x, y) in[((x) + (y)) % (2*res_len)]

    int errs = 0;
    for (int i = 0; i < res_len; ++i) {
        res[i] = GET_IN_(2*i, 2) ^ GET_IN_(2*i, 3);
        err[i] = GET_IN_(2*i, 5) ^ GET_IN_(2*i, 6) ^ GET_IN_(2*i, 7);

        // we have 2 solutions, if match set to 0, 1 othervise
        err[i] ^= res[i];
        errs += err[i];
    }
#undef GET_IN_

    return errs;
}

void data_block_decode_frame1(data_block_t *data_blk, const uint8_t *data,
        frame_type_t fr_type)
{
    data_blk->fr_type = fr_type;
    data_blk->nerrs = 0;

    switch (fr_type) {
        case FRAME_TYPE_AUTO:
        case FRAME_TYPE_DATA:
        case FRAME_TYPE_VOICE:
            // decode first 52 bits of frame
            data_blk->nerrs = decode_data_frame(
                    data_blk->data, data_blk->err, data, 26);
            if (fr_type == FRAME_TYPE_AUTO) {
                data_blk->fr_type = data_blk->data[0];
            }
        break;

        case FRAME_TYPE_HR_DATA:
            // TODO
            LOG(ERR, "decoding frame type %d not implemented", fr_type);
            data_blk->nerrs = INT_MAX;
            break;

        default:
            // TODO
            LOG(ERR, "decoding frame type %d not implemented", fr_type);
            data_blk->nerrs = INT_MAX;
    }
}

void data_block_decode_frame2(data_block_t *data_blk, const uint8_t *data)
{
    switch (data_blk->fr_type) {
        case FRAME_TYPE_VOICE:
            memcpy(data_blk->data + 26, data + 2*26, 100);
            break;

        case FRAME_TYPE_DATA:
            // decode remaining part of frame
            data_blk->nerrs += decode_data_frame(
                    data_blk->data + 26, data_blk->err + 26, data + 2*26, 50);
            if (!data_blk->nerrs && ( data_blk->data[74] || data_blk->data[75] )) {
                LOG(WTF, "nonzero padding in frame: %d %d",
                        data_blk->data[74], data_blk->data[75]);
            }
        break;

        case FRAME_TYPE_HR_DATA:
            // TODO
            LOG(ERR, "decoding frame type %d not implemented", data_blk->fr_type);
            data_blk->nerrs = INT_MAX;
            break;

        case FRAME_TYPE_AUTO:
        default:
            // TODO
            LOG(ERR, "decoding frame type %d not implemented", data_blk->fr_type);
            data_blk->nerrs = INT_MAX;
    }
}

bool data_block_check_crc(data_block_t *data_blk)
{
    if (data_blk->fr_type == FRAME_TYPE_AUTO) {
        data_blk->fr_type = data_blk->data[0];
    } else {
        if (data_blk->fr_type != data_blk->data[0]) {
            return false;
        }
    }

    if (data_blk->fr_type == FRAME_TYPE_DATA) {
        uint8_t crc[5];

        mk_crc5(crc, data_blk->data, 69);
        return !memcmp(data_blk->data + 69, crc, 5);
    }

    if (data_blk->fr_type == FRAME_TYPE_VOICE) {
        uint8_t crc[3];

        mk_crc3(crc, data_blk->data, 23);
        return !memcmp(data_blk->data + 23, crc, 3);
    }
    return false;
}

asb_t data_block_get_asb(data_block_t *data_blk)
{
    asb_t asb;

    if (data_blk->fr_type == FRAME_TYPE_DATA) {
        asb.x = data_blk->data[67];
        asb.y = data_blk->data[68];
    } else if (data_blk->fr_type == FRAME_TYPE_VOICE) {
        asb.x = data_blk->data[21];
        asb.y = data_blk->data[22];
    } else {
        LOG(WTF, "Invalid frame type for get_ASB: %d", data_blk->fr_type);
        asb.xy = 0x3;
    }

    return asb;
}
