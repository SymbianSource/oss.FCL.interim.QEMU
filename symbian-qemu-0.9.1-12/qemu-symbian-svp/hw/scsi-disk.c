/*
 * SCSI Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Based on code by Fabrice Bellard
 *
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 *
 * Note that this file only handles the SCSI architecture model and device
 * commands.  Emulation of interface/link layer protocols is handled by
 * the host adapter emulator.
 */

//#define DEBUG_SCSI

#ifdef DEBUG_SCSI
#define DPRINTF(fmt, args...) \
do { printf("scsi-disk: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...) do {} while(0)
#endif

#define BADF(fmt, args...) \
do { fprintf(stderr, "scsi-disk: " fmt , ##args); } while (0)

#include "qemu-common.h"
#include "block.h"
#include "scsi-disk.h"

#define SENSE_NO_SENSE        0
#define SENSE_NOT_READY       2
#define SENSE_HARDWARE_ERROR  4
#define SENSE_ILLEGAL_REQUEST 5

#define STATUS_GOOD            0
#define STATUS_CHECK_CONDITION 2

#define SCSI_DMA_BUF_SIZE    131072
#define SCSI_MAX_INQUIRY_LEN 256

typedef struct SCSIRequest {
    SCSIDeviceState *dev;
    uint32_t tag;
    /* ??? We should probably keep track of whether the data trasfer is
       a read or a write.  Currently we rely on the host getting it right.  */
    /* Both sector and sector_count are in terms of qemu 512 byte blocks.  */
    int sector;
    int sector_count;
    /* The amounnt of data in the buffer.  */
    int buf_len;
    uint8_t *dma_buf;
    BlockDriverAIOCB *aiocb;
    struct SCSIRequest *next;
} SCSIRequest;

struct SCSIDeviceState
{
    BlockDriverState *bdrv;
    SCSIRequest *requests;
    /* The qemu block layer uses a fixed 512 byte sector size.
       This is the number of 512 byte blocks in a single scsi sector.  */
    int cluster_size;
    int sense;
    int tcq;
    /* Completion functions may be called from either scsi_{read,write}_data
       or from the AIO completion routines.  */
    scsi_completionfn completion;
    void *opaque;
};

/* Global pool of SCSIRequest structures.  */
static SCSIRequest *free_requests = NULL;

static SCSIRequest *scsi_new_request(SCSIDeviceState *s, uint32_t tag)
{
    SCSIRequest *r;

    if (free_requests) {
        r = free_requests;
        free_requests = r->next;
    } else {
        r = qemu_malloc(sizeof(SCSIRequest));
        r->dma_buf = qemu_memalign(512, SCSI_DMA_BUF_SIZE);
    }
    r->dev = s;
    r->tag = tag;
    r->sector_count = 0;
    r->buf_len = 0;
    r->aiocb = NULL;

    r->next = s->requests;
    s->requests = r;
    return r;
}

static void scsi_remove_request(SCSIRequest *r)
{
    SCSIRequest *last;
    SCSIDeviceState *s = r->dev;

    if (s->requests == r) {
        s->requests = r->next;
    } else {
        last = s->requests;
        while (last && last->next != r)
            last = last->next;
        if (last) {
            last->next = r->next;
        } else {
            BADF("Orphaned request\n");
        }
    }
    r->next = free_requests;
    free_requests = r;
}

static SCSIRequest *scsi_find_request(SCSIDeviceState *s, uint32_t tag)
{
    SCSIRequest *r;

    r = s->requests;
    while (r && r->tag != tag)
        r = r->next;

    return r;
}

/* Helper function for command completion.  */
static void scsi_command_complete(SCSIRequest *r, int status, int sense)
{
    SCSIDeviceState *s = r->dev;
    uint32_t tag;
    DPRINTF("Command complete tag=0x%x status=%d sense=%d\n", r->tag, status, sense);
    s->sense = sense;
    tag = r->tag;
    scsi_remove_request(r);
    s->completion(s->opaque, SCSI_REASON_DONE, tag, status);
}

/* Cancel a pending data transfer.  */
static void scsi_cancel_io(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
    DPRINTF("Cancel tag=0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (r) {
        if (r->aiocb)
            bdrv_aio_cancel(r->aiocb);
        r->aiocb = NULL;
        scsi_remove_request(r);
    }
}

static void scsi_read_complete(void * opaque, int ret)
{
    SCSIRequest *r = (SCSIRequest *)opaque;
    SCSIDeviceState *s = r->dev;

    if (ret) {
        DPRINTF("IO error\n");
        s->completion(s->opaque, SCSI_REASON_DATA, r->tag, 0);
        scsi_command_complete(r, STATUS_CHECK_CONDITION, SENSE_NO_SENSE);
        return;
    }
    DPRINTF("Data ready tag=0x%x len=%d\n", r->tag, r->buf_len);

    s->completion(s->opaque, SCSI_REASON_DATA, r->tag, r->buf_len);
}

/* Read more data from scsi device into buffer.  */
static void scsi_read_data(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
    uint32_t n;

    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad read tag 0x%x\n", tag);
        /* ??? This is the wrong error.  */
        scsi_command_complete(r, STATUS_CHECK_CONDITION, SENSE_HARDWARE_ERROR);
        return;
    }
    if (r->sector_count == (uint32_t)-1) {
        DPRINTF("Read buf_len=%d\n", r->buf_len);
        r->sector_count = 0;
        s->completion(s->opaque, SCSI_REASON_DATA, r->tag, r->buf_len);
        return;
    }
    DPRINTF("Read sector_count=%d\n", r->sector_count);
    if (r->sector_count == 0) {
        scsi_command_complete(r, STATUS_GOOD, SENSE_NO_SENSE);
        return;
    }

    n = r->sector_count;
    if (n > SCSI_DMA_BUF_SIZE / 512)
        n = SCSI_DMA_BUF_SIZE / 512;

    r->buf_len = n * 512;
    r->aiocb = bdrv_aio_read(s->bdrv, r->sector, r->dma_buf, n,
                             scsi_read_complete, r);
    if (r->aiocb == NULL)
        scsi_command_complete(r, STATUS_CHECK_CONDITION, SENSE_HARDWARE_ERROR);
    r->sector += n;
    r->sector_count -= n;
}

static void scsi_write_complete(void * opaque, int ret)
{
    SCSIRequest *r = (SCSIRequest *)opaque;
    SCSIDeviceState *s = r->dev;
    uint32_t len;

    if (ret) {
        fprintf(stderr, "scsi-disc: IO write error\n");
        exit(1);
    }

    r->aiocb = NULL;
    if (r->sector_count == 0) {
        scsi_command_complete(r, STATUS_GOOD, SENSE_NO_SENSE);
    } else {
        len = r->sector_count * 512;
        if (len > SCSI_DMA_BUF_SIZE) {
            len = SCSI_DMA_BUF_SIZE;
        }
        r->buf_len = len;
        DPRINTF("Write complete tag=0x%x more=%d\n", r->tag, len);
        s->completion(s->opaque, SCSI_REASON_DATA, r->tag, len);
    }
}

/* Write data to a scsi device.  Returns nonzero on failure.
   The transfer may complete asynchronously.  */
static int scsi_write_data(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;
    uint32_t n;

    DPRINTF("Write data tag=0x%x\n", tag);
    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad write tag 0x%x\n", tag);
        scsi_command_complete(r, STATUS_CHECK_CONDITION, SENSE_HARDWARE_ERROR);
        return 1;
    }
    if (r->aiocb)
        BADF("Data transfer already in progress\n");
    n = r->buf_len / 512;
    if (n) {
        r->aiocb = bdrv_aio_write(s->bdrv, r->sector, r->dma_buf, n,
                                  scsi_write_complete, r);
        if (r->aiocb == NULL)
            scsi_command_complete(r, STATUS_CHECK_CONDITION,
                                  SENSE_HARDWARE_ERROR);
        r->sector += n;
        r->sector_count -= n;
    } else {
        /* Invoke completion routine to fetch data from host.  */
        scsi_write_complete(r, 0);
    }

    return 0;
}

/* Return a pointer to the data buffer.  */
static uint8_t *scsi_get_buf(SCSIDevice *d, uint32_t tag)
{
    SCSIDeviceState *s = d->state;
    SCSIRequest *r;

    r = scsi_find_request(s, tag);
    if (!r) {
        BADF("Bad buffer tag 0x%x\n", tag);
        return NULL;
    }
    return r->dma_buf;
}

/* Execute a scsi command.  Returns the length of the data expected by the
   command.  This will be Positive for data transfers from the device
   (eg. disk reads), negative for transfers to the device (eg. disk writes),
   and zero if the command does not transfer any data.  */

static int32_t scsi_send_command(SCSIDevice *d, uint32_t tag,
                                 uint8_t *buf, int lun)
{
    SCSIDeviceState *s = d->state;
    uint64_t nb_sectors;
    uint32_t lba;
    uint32_t len;
    int cmdlen;
    int is_write;
    uint8_t command;
    uint8_t *outbuf;
    SCSIRequest *r;

    command = buf[0];
    r = scsi_find_request(s, tag);
    if (r) {
        BADF("Tag 0x%x already in use\n", tag);
        scsi_cancel_io(d, tag);
    }
    /* ??? Tags are not unique for different luns.  We only implement a
       single lun, so this should not matter.  */
    r = scsi_new_request(s, tag);
    outbuf = r->dma_buf;
    is_write = 0;
    DPRINTF("Command: lun=%d tag=0x%x data=0x%02x", lun, tag, buf[0]);
    switch (command >> 5) {
    case 0:
        lba = buf[3] | (buf[2] << 8) | ((buf[1] & 0x1f) << 16);
        len = buf[4];
        cmdlen = 6;
        break;
    case 1:
    case 2:
        lba = buf[5] | (buf[4] << 8) | (buf[3] << 16) | (buf[2] << 24);
        len = buf[8] | (buf[7] << 8);
        cmdlen = 10;
        break;
    case 4:
        lba = buf[5] | (buf[4] << 8) | (buf[3] << 16) | (buf[2] << 24);
        len = buf[13] | (buf[12] << 8) | (buf[11] << 16) | (buf[10] << 24);
        cmdlen = 16;
        break;
    case 5:
        lba = buf[5] | (buf[4] << 8) | (buf[3] << 16) | (buf[2] << 24);
        len = buf[9] | (buf[8] << 8) | (buf[7] << 16) | (buf[6] << 24);
        cmdlen = 12;
        break;
    default:
        BADF("Unsupported command length, command %x\n", command);
        goto fail;
    }
#ifdef DEBUG_SCSI
    {
        int i;
        for (i = 1; i < cmdlen; i++) {
            printf(" 0x%02x", buf[i]);
        }
        printf("\n");
    }
#endif
    if (lun || buf[1] >> 5) {
        /* Only LUN 0 supported.  */
        DPRINTF("Unimplemented LUN %d\n", lun ? lun : buf[1] >> 5);
        if (command != 0x03 && command != 0x12) /* REQUEST SENSE and INQUIRY */
            goto fail;
    }
    switch (command) {
    case 0x0:
	DPRINTF("Test Unit Ready\n");
	break;
    case 0x03:
        DPRINTF("Request Sense (len %d)\n", len);
        if (len < 4)
            goto fail;
        memset(outbuf, 0, 4);
        outbuf[0] = 0xf0;
        outbuf[1] = 0;
        outbuf[2] = s->sense;
        r->buf_len = 4;
        break;
    case 0x12:
        DPRINTF("Inquiry (len %d)\n", len);
        if (buf[1] & 0x2) {
            /* Command support data - optional, not implemented */
            BADF("optional INQUIRY command support request not implemented\n");
            goto fail;
        }
        else if (buf[1] & 0x1) {
            /* Vital product data */
            uint8_t page_code = buf[2];
            if (len < 4) {
                BADF("Error: Inquiry (EVPD[%02X]) buffer size %d is "
                     "less than 4\n", page_code, len);
                goto fail;
            }

            switch (page_code) {
                case 0x00:
                    {
                        /* Supported page codes, mandatory */
                        DPRINTF("Inquiry EVPD[Supported pages] "
                                "buffer size %d\n", len);

                        r->buf_len = 0;

                        if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
                            outbuf[r->buf_len++] = 5;
                        } else {
                            outbuf[r->buf_len++] = 0;
                        }

                        outbuf[r->buf_len++] = 0x00; // this page
                        outbuf[r->buf_len++] = 0x00;
                        outbuf[r->buf_len++] = 3;    // number of pages
                        outbuf[r->buf_len++] = 0x00; // list of supported pages (this page)
                        outbuf[r->buf_len++] = 0x80; // unit serial number
                        outbuf[r->buf_len++] = 0x83; // device identification
                    }
                    break;
                case 0x80:
                    {
                        /* Device serial number, optional */
                        if (len < 4) {
                            BADF("Error: EVPD[Serial number] Inquiry buffer "
                                 "size %d too small, %d needed\n", len, 4);
                            goto fail;
                        }

                        DPRINTF("Inquiry EVPD[Serial number] buffer size %d\n", len);

                        r->buf_len = 0;

                        /* Supported page codes */
                        if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
                            outbuf[r->buf_len++] = 5;
                        } else {
                            outbuf[r->buf_len++] = 0;
                        }

                        outbuf[r->buf_len++] = 0x80; // this page
                        outbuf[r->buf_len++] = 0x00;
                        outbuf[r->buf_len++] = 0x01; // 1 byte data follow

                        outbuf[r->buf_len++] = '0';  // 1 byte data follow 
                    }

                    break;
                case 0x83:
                    {
                        /* Device identification page, mandatory */
                        int max_len = 255 - 8;
                        int id_len = strlen(bdrv_get_device_name(s->bdrv));
                        if (id_len > max_len)
                            id_len = max_len;

                        DPRINTF("Inquiry EVPD[Device identification] "
                                "buffer size %d\n", len);
                        r->buf_len = 0;
                        if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
                            outbuf[r->buf_len++] = 5;
                        } else {
                            outbuf[r->buf_len++] = 0;
                        }

                        outbuf[r->buf_len++] = 0x83; // this page
                        outbuf[r->buf_len++] = 0x00;
                        outbuf[r->buf_len++] = 3 + id_len;

                        outbuf[r->buf_len++] = 0x2; // ASCII
                        outbuf[r->buf_len++] = 0;   // not officially assigned
                        outbuf[r->buf_len++] = 0;   // reserved
                        outbuf[r->buf_len++] = id_len; // length of data following

                        memcpy(&outbuf[r->buf_len],
                               bdrv_get_device_name(s->bdrv), id_len);
                        r->buf_len += id_len;
                    }
                    break;
                default:
                    BADF("Error: unsupported Inquiry (EVPD[%02X]) "
                         "buffer size %d\n", page_code, len);
                    goto fail;
            }
            /* done with EVPD */
            break;
        }
        else {
            /* Standard INQUIRY data */
            if (buf[2] != 0) {
                BADF("Error: Inquiry (STANDARD) page or code "
                     "is non-zero [%02X]\n", buf[2]);
                goto fail;
            }

            /* PAGE CODE == 0 */
            if (len < 5) {
                BADF("Error: Inquiry (STANDARD) buffer size %d "
                     "is less than 5\n", len);
                goto fail;
            }

            if (len < 36) {
                BADF("Error: Inquiry (STANDARD) buffer size %d "
                     "is less than 36 (TODO: only 5 required)\n", len);
            }
        }

        if(len > SCSI_MAX_INQUIRY_LEN)
            len = SCSI_MAX_INQUIRY_LEN;

        memset(outbuf, 0, len);

        if (lun || buf[1] >> 5) {
            outbuf[0] = 0x7f;	/* LUN not supported */
	} else if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
	    outbuf[0] = 5;
            outbuf[1] = 0x80;
	    memcpy(&outbuf[16], "QEMU CD-ROM    ", 16);
	} else {
	    outbuf[0] = 0;
	    memcpy(&outbuf[16], "QEMU HARDDISK  ", 16);
	}
	memcpy(&outbuf[8], "QEMU   ", 8);
        memcpy(&outbuf[32], QEMU_VERSION, 4);
        /* Identify device as SCSI-3 rev 1.
           Some later commands are also implemented. */
	outbuf[2] = 3;
	outbuf[3] = 2; /* Format 2 */
	outbuf[4] = len - 5; /* Additional Length = (Len - 1) - 4 */
        /* Sync data transfer and TCQ.  */
        outbuf[7] = 0x10 | (s->tcq ? 0x02 : 0);
	r->buf_len = len;
	break;
    case 0x16:
        DPRINTF("Reserve(6)\n");
        if (buf[1] & 1)
            goto fail;
        break;
    case 0x17:
        DPRINTF("Release(6)\n");
        if (buf[1] & 1)
            goto fail;
        break;
    case 0x1a:
    case 0x5a:
        {
            uint8_t *p;
            int page;

            page = buf[2] & 0x3f;
            DPRINTF("Mode Sense (page %d, len %d)\n", page, len);
            p = outbuf;
            memset(p, 0, 4);
            outbuf[1] = 0; /* Default media type.  */
            outbuf[3] = 0; /* Block descriptor length.  */
            if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
                outbuf[2] = 0x80; /* Readonly.  */
            }
            p += 4;
            if (page == 4) {
                int cylinders, heads, secs;

                /* Rigid disk device geometry page. */
                p[0] = 4;
                p[1] = 0x16;
                /* if a geometry hint is available, use it */
                bdrv_get_geometry_hint(s->bdrv, &cylinders, &heads, &secs);
                p[2] = (cylinders >> 16) & 0xff;
                p[3] = (cylinders >> 8) & 0xff;
                p[4] = cylinders & 0xff;
                p[5] = heads & 0xff;
                /* Write precomp start cylinder, disabled */
                p[6] = (cylinders >> 16) & 0xff;
                p[7] = (cylinders >> 8) & 0xff;
                p[8] = cylinders & 0xff;
                /* Reduced current start cylinder, disabled */
                p[9] = (cylinders >> 16) & 0xff;
                p[10] = (cylinders >> 8) & 0xff;
                p[11] = cylinders & 0xff;
                /* Device step rate [ns], 200ns */
                p[12] = 0;
                p[13] = 200;
                /* Landing zone cylinder */
                p[14] = 0xff;
                p[15] =  0xff;
                p[16] = 0xff;
                /* Medium rotation rate [rpm], 5400 rpm */
                p[20] = (5400 >> 8) & 0xff;
                p[21] = 5400 & 0xff;
                p += 0x16;
            } else if (page == 5) {
                int cylinders, heads, secs;

                /* Flexible disk device geometry page. */
                p[0] = 5;
                p[1] = 0x1e;
                /* Transfer rate [kbit/s], 5Mbit/s */
                p[2] = 5000 >> 8;
                p[3] = 5000 & 0xff;
                /* if a geometry hint is available, use it */
                bdrv_get_geometry_hint(s->bdrv, &cylinders, &heads, &secs);
                p[4] = heads & 0xff;
                p[5] = secs & 0xff;
                p[6] = s->cluster_size * 2;
                p[8] = (cylinders >> 8) & 0xff;
                p[9] = cylinders & 0xff;
                /* Write precomp start cylinder, disabled */
                p[10] = (cylinders >> 8) & 0xff;
                p[11] = cylinders & 0xff;
                /* Reduced current start cylinder, disabled */
                p[12] = (cylinders >> 8) & 0xff;
                p[13] = cylinders & 0xff;
                /* Device step rate [100us], 100us */
                p[14] = 0;
                p[15] = 1;
                /* Device step pulse width [us], 1us */
                p[16] = 1;
                /* Device head settle delay [100us], 100us */
                p[17] = 0;
                p[18] = 1;
                /* Motor on delay [0.1s], 0.1s */
                p[19] = 1;
                /* Motor off delay [0.1s], 0.1s */
                p[20] = 1;
                /* Medium rotation rate [rpm], 5400 rpm */
                p[28] = (5400 >> 8) & 0xff;
                p[29] = 5400 & 0xff;
                p += 0x1e;
            } else if ((page == 8 || page == 0x3f)) {
                /* Caching page.  */
                memset(p,0,20);
                p[0] = 8;
                p[1] = 0x12;
                p[2] = 4; /* WCE */
                p += 20;
            }
            if ((page == 0x3f || page == 0x2a)
                    && (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM)) {
                /* CD Capabilities and Mechanical Status page. */
                p[0] = 0x2a;
                p[1] = 0x14;
                p[2] = 3; // CD-R & CD-RW read
                p[3] = 0; // Writing not supported
                p[4] = 0x7f; /* Audio, composite, digital out,
                                         mode 2 form 1&2, multi session */
                p[5] = 0xff; /* CD DA, DA accurate, RW supported,
                                         RW corrected, C2 errors, ISRC,
                                         UPC, Bar code */
                p[6] = 0x2d | (bdrv_is_locked(s->bdrv)? 2 : 0);
                /* Locking supported, jumper present, eject, tray */
                p[7] = 0; /* no volume & mute control, no
                                      changer */
                p[8] = (50 * 176) >> 8; // 50x read speed
                p[9] = (50 * 176) & 0xff;
                p[10] = 0 >> 8; // No volume
                p[11] = 0 & 0xff;
                p[12] = 2048 >> 8; // 2M buffer
                p[13] = 2048 & 0xff;
                p[14] = (16 * 176) >> 8; // 16x read speed current
                p[15] = (16 * 176) & 0xff;
                p[18] = (16 * 176) >> 8; // 16x write speed
                p[19] = (16 * 176) & 0xff;
                p[20] = (16 * 176) >> 8; // 16x write speed current
                p[21] = (16 * 176) & 0xff;
                p += 22;
            }
            r->buf_len = p - outbuf;
            outbuf[0] = r->buf_len - 4;
            if (r->buf_len > len)
                r->buf_len = len;
        }
        break;
    case 0x1b:
        DPRINTF("Start Stop Unit\n");
	break;
    case 0x1e:
        DPRINTF("Prevent Allow Medium Removal (prevent = %d)\n", buf[4] & 3);
        bdrv_set_locked(s->bdrv, buf[4] & 1);
	break;
    case 0x25:
	DPRINTF("Read Capacity\n");
        /* The normal LEN field for this command is zero.  */
	memset(outbuf, 0, 8);
	bdrv_get_geometry(s->bdrv, &nb_sectors);
        /* Returned value is the address of the last sector.  */
        if (nb_sectors) {
            nb_sectors--;
            outbuf[0] = (nb_sectors >> 24) & 0xff;
            outbuf[1] = (nb_sectors >> 16) & 0xff;
            outbuf[2] = (nb_sectors >> 8) & 0xff;
            outbuf[3] = nb_sectors & 0xff;
            outbuf[4] = 0;
            outbuf[5] = 0;
            outbuf[6] = s->cluster_size * 2;
            outbuf[7] = 0;
            r->buf_len = 8;
        } else {
            scsi_command_complete(r, STATUS_CHECK_CONDITION, SENSE_NOT_READY);
            return 0;
        }
	break;
    case 0x08:
    case 0x28:
        DPRINTF("Read (sector %d, count %d)\n", lba, len);
        r->sector = lba * s->cluster_size;
        r->sector_count = len * s->cluster_size;
        break;
    case 0x0a:
    case 0x2a:
        DPRINTF("Write (sector %d, count %d)\n", lba, len);
        r->sector = lba * s->cluster_size;
        r->sector_count = len * s->cluster_size;
        is_write = 1;
        break;
    case 0x35:
        DPRINTF("Synchronise cache (sector %d, count %d)\n", lba, len);
        bdrv_flush(s->bdrv);
        break;
    case 0x43:
        {
            int start_track, format, msf, toclen;

            msf = buf[1] & 2;
            format = buf[2] & 0xf;
            start_track = buf[6];
            bdrv_get_geometry(s->bdrv, &nb_sectors);
            DPRINTF("Read TOC (track %d format %d msf %d)\n", start_track, format, msf >> 1);
            switch(format) {
            case 0:
                toclen = cdrom_read_toc(nb_sectors, outbuf, msf, start_track);
                break;
            case 1:
                /* multi session : only a single session defined */
                toclen = 12;
                memset(outbuf, 0, 12);
                outbuf[1] = 0x0a;
                outbuf[2] = 0x01;
                outbuf[3] = 0x01;
                break;
            case 2:
                toclen = cdrom_read_toc_raw(nb_sectors, outbuf, msf, start_track);
                break;
            default:
                goto error_cmd;
            }
            if (toclen > 0) {
                if (len > toclen)
                  len = toclen;
                r->buf_len = len;
                break;
            }
        error_cmd:
            DPRINTF("Read TOC error\n");
            goto fail;
        }
    case 0x46:
        DPRINTF("Get Configuration (rt %d, maxlen %d)\n", buf[1] & 3, len);
        memset(outbuf, 0, 8);
        /* ??? This should probably return much more information.  For now
           just return the basic header indicating the CD-ROM profile.  */
        outbuf[7] = 8; // CD-ROM
        r->buf_len = 8;
        break;
    case 0x56:
        DPRINTF("Reserve(10)\n");
        if (buf[1] & 3)
            goto fail;
        break;
    case 0x57:
        DPRINTF("Release(10)\n");
        if (buf[1] & 3)
            goto fail;
        break;
    case 0xa0:
        DPRINTF("Report LUNs (len %d)\n", len);
        if (len < 16)
            goto fail;
        memset(outbuf, 0, 16);
        outbuf[3] = 8;
        r->buf_len = 16;
        break;
    case 0x2f:
        DPRINTF("Verify (sector %d, count %d)\n", lba, len);
        break;
    default:
	DPRINTF("Unknown SCSI command (%2.2x)\n", buf[0]);
    fail:
        scsi_command_complete(r, STATUS_CHECK_CONDITION, SENSE_ILLEGAL_REQUEST);
	return 0;
    }
    if (r->sector_count == 0 && r->buf_len == 0) {
        scsi_command_complete(r, STATUS_GOOD, SENSE_NO_SENSE);
    }
    len = r->sector_count * 512 + r->buf_len;
    if (is_write) {
        return -len;
    } else {
        if (!r->sector_count)
            r->sector_count = -1;
        return len;
    }
}

static void scsi_destroy(SCSIDevice *d)
{
    qemu_free(d->state);
    qemu_free(d);
}

SCSIDevice *scsi_disk_init(BlockDriverState *bdrv, int tcq,
                           scsi_completionfn completion, void *opaque)
{
    SCSIDevice *d;
    SCSIDeviceState *s;

    s = (SCSIDeviceState *)qemu_mallocz(sizeof(SCSIDeviceState));
    s->bdrv = bdrv;
    s->tcq = tcq;
    s->completion = completion;
    s->opaque = opaque;
    if (bdrv_get_type_hint(s->bdrv) == BDRV_TYPE_CDROM) {
        s->cluster_size = 4;
    } else {
        s->cluster_size = 1;
    }

    d = (SCSIDevice *)qemu_mallocz(sizeof(SCSIDevice));
    d->state = s;
    d->destroy = scsi_destroy;
    d->send_command = scsi_send_command;
    d->read_data = scsi_read_data;
    d->write_data = scsi_write_data;
    d->cancel_io = scsi_cancel_io;
    d->get_buf = scsi_get_buf;

    return d;
}
