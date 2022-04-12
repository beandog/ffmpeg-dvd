/*
 * DVD (libdvdread) protocol
 *
 * Copyright (c) 2021 Steve Dibb <steve.dibb <at> gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>

#include "libavutil/avstring.h"
#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "libavutil/opt.h"

#define DVD_PROTO_PREFIX     "dvd:"
#ifndef DVD_VIDEO_LB_LEN
#define DVD_VIDEO_LB_LEN 2048
#endif

typedef struct {
    const AVClass *class;

    dvd_reader_t *dvd;
    ifo_handle_t *vmg;
    ifo_handle_t *vts;
    dvd_file_t *file;
    int blocks;
    int size;
    int offset;
    int title_set;

    int title;
    // int chapter;
} DVDContext;

#define OFFSET(x) offsetof(DVDContext, x)
static const AVOption options[] = {
{"title", "", OFFSET(title), AV_OPT_TYPE_INT, { .i64=-1 }, -1, 99999, AV_OPT_FLAG_DECODING_PARAM },
// {"chapter",  "", OFFSET(chapter),  AV_OPT_TYPE_INT, { .i64=1 },   1, 0xfffe, AV_OPT_FLAG_DECODING_PARAM },
{NULL}
};

static const AVClass dvd_context_class = {
    .class_name     = "dvd",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int check_disc_info(URLContext *h)
{
    DVDContext *dvd = h->priv_data;
    const ifo_handle_t *disc_info;

    disc_info = ifoOpen(dvd->dvd, 0);
    if (!disc_info) {
        av_log(h, AV_LOG_ERROR, "ifoOpen() failed\n");
        return -1;
    }

    return 0;
}

static int dvd_close(URLContext *h)
{
    DVDContext *dvd = h->priv_data;

    if (dvd->vmg) {
        ifoClose(dvd->vmg);
    }

    if (dvd->vts) {
        ifoClose(dvd->vts);
    }

    if (dvd->dvd) {
        DVDClose(dvd->dvd);
    }

    return 0;
}

static int dvd_open(URLContext *h, const char *path, int flags)
{
    DVDContext *dvd = h->priv_data;
    int num_title_idx;
    const char *diskname = path;

    av_strstart(path, DVD_PROTO_PREFIX, &diskname);

    dvd->dvd = DVDOpen(diskname);
    if (!dvd->dvd) {
        av_log(h, AV_LOG_ERROR, "DVDOpen() failed\n");
        return AVERROR(EIO);
    }

    /* check if disc can be played */
    if (check_disc_info(h) < 0) {
        return AVERROR(EIO);
    }

    /* load DVD info */
    dvd->vmg = ifoOpen(dvd->dvd, 0);
    if (dvd->vmg == NULL || dvd->vmg->vmgi_mat == NULL || dvd->vmg->tt_srpt == NULL) {
        return AVERROR(EIO);
    }

    /* load title list */
    num_title_idx = dvd->vmg->tt_srpt->nr_of_srpts;
    av_log(h, AV_LOG_INFO, "%d usable titles:\n", num_title_idx);
    if (num_title_idx < 1) {
        return AVERROR(EIO);
    }

    /* play first title if none is given */
    if (dvd->title < 1) {
        dvd->title = 1;
    }

    av_log(h, AV_LOG_INFO, "selected title %02d\n", dvd->title);

    /* select title */
    dvd->title_set = dvd->vmg->tt_srpt->title[dvd->title - 1].title_set_nr;

    /* open DVD file */
    dvd->file = DVDOpenFile(dvd->dvd, dvd->title_set, DVD_READ_TITLE_VOBS);
    if (dvd->file == 0) {
        return AVERROR(EIO);
    }

    dvd->blocks = DVDFileSize(dvd->file);
    dvd->size = dvd->blocks * DVD_VIDEO_LB_LEN;

    /* set cell block offset */
    dvd->offset = 0;

    return 0;
}

static int dvd_read(URLContext *h, unsigned char *buf, int size)
{
    DVDContext *dvd = h->priv_data;
    int len;
    int blocks;

    if (!dvd || !dvd->dvd) {
        return AVERROR(EFAULT);
    }

    blocks = DVDReadBlocks(dvd->file, dvd->offset, 1, buf);

    dvd->offset++;

    len = blocks * DVD_VIDEO_LB_LEN;

    // TODO this isn't working for checking past offset, quitting at EOF
    if (len == 0 || dvd->offset >= dvd->blocks) {
        return AVERROR_EOF;
    } else {
        return len;
    }

}

static int64_t dvd_seek(URLContext *h, int64_t pos, int whence)
{
    DVDContext *dvd = h->priv_data;

    if (!dvd || !dvd->dvd) {
        return AVERROR(EFAULT);
    }
    printf("seek position: %li\n\n", pos);

    /*
    switch (whence) {
    case SEEK_SET:
    case SEEK_CUR:
    case SEEK_END:
        return -1;
    }
    */

    av_log(h, AV_LOG_ERROR, "Unsupported whence operation %d\n", whence);
    return AVERROR(EINVAL);
}


const URLProtocol ff_dvd_protocol = {
    .name            = "dvd",
    .url_close       = dvd_close,
    .url_open        = dvd_open,
    .url_read        = dvd_read,
    .url_seek        = dvd_seek,
    .priv_data_size  = sizeof(DVDContext),
    .priv_data_class = &dvd_context_class,
};
