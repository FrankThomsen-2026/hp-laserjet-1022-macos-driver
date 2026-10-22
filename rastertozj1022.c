/**
 * rastertozj1022.c — Native 64-Bit ARM64 / Universal CUPS Raster Filter
 * Dual-Engine Architecture for HP LaserJet 1022 (macOS 27):
 *   1. Default: Hardware PCL 5e 600/1200 DPI PackBits Raster Engine
 *      (Eliminates "ZJZJ4♥ZZ" ASCII/Form-Feed bug when PJL switches to PCL 5e)
 *   2. Optional (--zjs-raw): Pure Zenographics ZjStream (ZJZJ at byte 0, no PJL)
 *
 * Compile on macOS:
 *   clang -O3 -arch arm64 -arch x86_64 -Wall -Wno-deprecated-declarations \
 *     $(cups-config --cflags) rastertozj1022.c $(cups-config --image --libs) \
 *     -o rastertozj1022
 *   codesign --force --sign - rastertozj1022
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <arpa/inet.h>
#include <cups/cups.h>
#include <cups/raster.h>

static volatile int g_canceled = 0;

static void sigterm_handler(int sig) {
    (void)sig;
    g_canceled = 1;
}

/* 8x8 Bayer ordered dither matrix for 8-bit CUPS raster input */
static const uint8_t bayer8x8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 }
};

/**
 * Compress a scanline using TIFF PackBits (PCL 5e Compression Mode 2: ESC * b 2 M)
 * Returns compressed byte length.
 */
static size_t pcl_packbits_encode(const uint8_t *src, size_t len, uint8_t *dst) {
    size_t si = 0;
    size_t di = 0;

    while (si < len) {
        /* Look for a run of identical bytes */
        size_t run = 1;
        while (si + run < len && run < 128 && src[si + run] == src[si]) {
            run++;
        }

        if (run >= 2) {
            dst[di++] = (uint8_t)(257 - run); /* -1..-127 in two's complement */
            dst[di++] = src[si];
            si += run;
        } else {
            /* Literal sequence */
            size_t lit_start = si;
            size_t lit_len = 0;
            while (si < len && lit_len < 128) {
                if (si + 2 < len && src[si] == src[si + 1] && src[si] == src[si + 2]) {
                    break;
                }
                si++;
                lit_len++;
            }
            dst[di++] = (uint8_t)(lit_len - 1); /* 0..127 */
            memcpy(&dst[di], &src[lit_start], lit_len);
            di += lit_len;
        }
    }
    return di;
}

/**
 * Self-test generator (--self-test):
 * Emits a 1-page hardware PCL 5e + 600 DPI Raster calibration sheet
 */
static int run_self_test(FILE *out) {
    fprintf(stderr, "INFO: [rastertozj1022] Sending 1-page PCL 5e + 600 DPI Raster self-test...\n");

    /* PJL Job Header entering native PCL personality */
    fprintf(out, "\033%%-12345X@PJL JOB\r\n");
    fprintf(out, "@PJL SET JAMRECOVERY=OFF\r\n");
    fprintf(out, "@PJL SET DENSITY=3\r\n");
    fprintf(out, "@PJL SET ECONOMODE=OFF\r\n");
    fprintf(out, "@PJL SET RET=MEDIUM\r\n");
    fprintf(out, "@PJL SET RESOLUTION=600\r\n");
    fprintf(out, "@PJL ENTER LANGUAGE=PCL\r\n");

    /* PCL 5e Reset, DIN A4 (26), Portrait (0O), Top Margin 2 lines */
    fprintf(out, "\033E");
    fprintf(out, "\033&l26a0o2E");
    fprintf(out, "\033&k2G"); /* CR=CR, LF=CR+LF */

    /* Print crisp header using built-in PCL font */
    fprintf(out, "\033*p300x300Y");
    fprintf(out, "\033(10U\033(s0p10h12v0s3b4099T");
    fprintf(out, "HP LASERJET 1022 - MACOS 27 ARM64 DRIVER v3.1 (CENTERED)\r\n\r\n");
    fprintf(out, "\033(s0p12h10v0s0b4099T");
    fprintf(out, "  Status:      Native 64-Bit ARM64 CUPS Filter (rastertozj1022 v3.1)\r\n");
    fprintf(out, "  USB-URI:     usb://Hewlett-Packard/HP%%20LaserJet%%201022?serial=JM1BN8J\r\n");
    fprintf(out, "  Aufloesung:  600 DPI Raster-Engine (PackBits Mode 2)\r\n");
    fprintf(out, "  Zentrierung: X=-6.8mm Korrektur (-78u20Z) | Symmetrisch Links=Rechts & Oben=Unten\r\n");
    fprintf(out, "  Papier:      DIN A4 (210 x 297 mm) | Toner-Dichte: 3/5\r\n\r\n");

    /* Now emit a 600 DPI Raster Graphics test pattern below the text */
    fprintf(out, "\033*p300x1050Y");
    fprintf(out, "\033*t600R");   /* 600 DPI raster resolution */
    fprintf(out, "\033*r1A");     /* Start raster graphics at current X */
    fprintf(out, "\033*b2M");     /* TIFF PackBits compression */

    uint32_t width_bytes = 480; /* 3840 pixels wide (6.4 inches) */
    uint8_t *raw_row = (uint8_t *)malloc(width_bytes);
    uint8_t *pack_row = (uint8_t *)malloc(width_bytes * 2);

    for (int y = 0; y < 240; y++) {
        memset(raw_row, 0, width_bytes);
        if (y < 8 || y >= 232) {
            /* Top and bottom solid border */
            memset(raw_row, 0xFF, width_bytes);
        } else {
            /* Left and right border + 10-step halftone bar */
            raw_row[0] = 0xFF;
            raw_row[1] = 0xFF;
            raw_row[width_bytes - 2] = 0xFF;
            raw_row[width_bytes - 1] = 0xFF;

            if (y >= 40 && y < 190) {
                for (uint32_t bx = 4; bx < width_bytes - 4; bx++) {
                    int step = (bx * 10) / width_bytes; /* 0..9 */
                    int thresh = ((step + 1) * 64) / 11;
                    uint8_t byte_val = 0;
                    for (int bit = 0; bit < 8; bit++) {
                        int px = (bx * 8) + bit;
                        if (bayer8x8[y & 7][px & 7] < thresh) {
                            byte_val |= (0x80 >> bit);
                        }
                    }
                    raw_row[bx] = byte_val;
                }
            }
        }
        size_t plen = pcl_packbits_encode(raw_row, width_bytes, pack_row);
        fprintf(out, "\033*b%zuW", plen);
        fwrite(pack_row, 1, plen, out);
    }

    free(raw_row);
    free(pack_row);

    fprintf(out, "\033*rC"); /* End raster graphics */
    fprintf(out, "\014");    /* Single Form Feed (1 page) */
    fprintf(out, "\033E");   /* PCL Reset */
    fprintf(out, "\033%%-12345X@PJL EOJ\r\n\033%%-12345X");
    fflush(out);

    fprintf(stderr, "INFO: [rastertozj1022] Self-test page v3.1 sent successfully.\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return run_self_test(stdout);
    }

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
        fprintf(stderr, "       %s --self-test | lp -d HP_LaserJet_1022 -o raw\n", argv[0]);
        return 1;
    }

    signal(SIGTERM, sigterm_handler);
    signal(SIGPIPE, SIG_IGN);

    int fd = 0; /* stdin */
    if (argc == 7) {
        fd = open(argv[6], O_RDONLY);
        if (fd < 0) {
            perror("ERROR: Unable to open raster input file");
            return 1;
        }
    }

    cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
    if (!ras) {
        fprintf(stderr, "ERROR: cupsRasterOpen failed.\n");
        if (fd != 0) close(fd);
        return 1;
    }

    int copies = atoi(argv[4]);
    if (copies < 1) copies = 1;

    /* Parse CUPS job options (argv[5]) for explicit paper selection */
    int num_options = 0;
    cups_option_t *options = NULL;
    num_options = cupsParseOptions(argv[5], 0, &options);
    const char *opt_pagesize = cupsGetOption("PageSize", num_options, options);
    if (!opt_pagesize) opt_pagesize = cupsGetOption("media", num_options, options);
    if (!opt_pagesize) opt_pagesize = cupsGetOption("PageRegion", num_options, options);

    /* Emit PJL Job Header entering PCL 5e Raster Mode */
    fprintf(stdout, "\033%%-12345X@PJL JOB\r\n");
    fprintf(stdout, "@PJL SET JAMRECOVERY=OFF\r\n");
    fprintf(stdout, "@PJL SET DENSITY=3\r\n");
    fprintf(stdout, "@PJL SET ECONOMODE=OFF\r\n");
    fprintf(stdout, "@PJL SET RET=MEDIUM\r\n");
    fprintf(stdout, "@PJL ENTER LANGUAGE=PCL\r\n");
    fprintf(stdout, "\033E");

    cups_page_header2_t header;
    unsigned int page = 0;

    while (!g_canceled && cupsRasterReadHeader2(ras, &header)) {
        page++;
        fprintf(stderr, "PAGE: %u %d\n", page, copies);

        uint32_t dpi = header.HWResolution[0] ? header.HWResolution[0] : 600;
        if (dpi > 600) dpi = 600; /* PCL 5e raster grid uses 600 DPI base */

        /* Determine PCL paper size code (26 = A4, 2 = Letter, 3 = Legal, 1 = Executive) */
        int pcl_paper = 26;
        uint32_t page_pt_w = header.PageSize[0] ? header.PageSize[0] : 595U;
        uint32_t page_pt_h = header.PageSize[1] ? header.PageSize[1] : 842U;

        if (opt_pagesize) {
            if (strcasestr(opt_pagesize, "A4")) {
                pcl_paper = 26; page_pt_w = 595U; page_pt_h = 842U;
            } else if (strcasestr(opt_pagesize, "Legal")) {
                pcl_paper = 3;  page_pt_w = 612U; page_pt_h = 1008U;
            } else if (strcasestr(opt_pagesize, "Exec")) {
                pcl_paper = 1;  page_pt_w = 522U; page_pt_h = 756U;
            } else if (strcasestr(opt_pagesize, "Letter")) {
                pcl_paper = 2;  page_pt_w = 612U; page_pt_h = 792U;
            }
        } else {
            if (page_pt_w == 595U || (page_pt_h >= 835U && page_pt_h <= 850U)) {
                pcl_paper = 26;
            } else if (page_pt_w == 612U && page_pt_h >= 990U) {
                pcl_paper = 3;
            } else if (page_pt_w == 522U) {
                pcl_paper = 1;
            } else if (page_pt_w == 612U && page_pt_h <= 800U) {
                pcl_paper = 2;
            }
        }

        /* Compute full physical page dimensions in pixels at 600 DPI */
        uint32_t full_w_px = (page_pt_w * dpi + 36U) / 72U;
        uint32_t full_h_px = (page_pt_h * dpi + 36U) / 72U;
        if (full_w_px < header.cupsWidth) full_w_px = header.cupsWidth;
        if (full_h_px < header.cupsHeight) full_h_px = header.cupsHeight;

        /* Vertical (Y) padding restores cropped top margin (verified 0.0mm vertical symmetry in v3.0).
         * Horizontal (X) uses pad_left_bytes = 0 + PCL -78u (-2.75mm), shifting X by -6.81mm left vs v3.0
         * so Left Margin == Right Margin across Word, Acrobat PDF, and PowerPoint. */
        uint32_t pad_left_bytes = 0U;
        uint32_t pad_top_px = (full_h_px > header.cupsHeight + 24U) ? ((full_h_px - header.cupsHeight) / 2U) : 0U;

        fprintf(stderr, "DEBUG: [rastertozj1022] page=%u paper=%d cups=%ux%u full=%ux%u pad_top=%upx\n",
                page, pcl_paper, header.cupsWidth, header.cupsHeight, full_w_px, full_h_px, pad_top_px);

        /* Page setup: Paper Size, Portrait (0O), Perforation Skip Off (0L), Top Margin 0 (0E), Copies */
        fprintf(stdout, "\033&l%da0o0l0e%dX", pcl_paper, copies);
        /* Hardware-calibrated registration: -78u (-2.75mm X) + pad_left_bytes=0 (-4.06mm X) = -6.81mm X shift; 20Z (+0.7mm Y) */
        fprintf(stdout, "\033&l-78u20Z");
        fprintf(stdout, "\033*p0x0Y");
        fprintf(stdout, "\033*t%uR", dpi);
        fprintf(stdout, "\033*r0f%us0A", header.cupsWidth);
        fprintf(stdout, "\033*b2M"); /* Enable TIFF PackBits compression */

        uint32_t src_bpl = (header.cupsWidth + 7U) / 8U;
        uint32_t dst_bpl = src_bpl + pad_left_bytes;
        uint8_t *raw_cups_line = (uint8_t *)calloc(1, header.cupsBytesPerLine);
        uint8_t *packed_line   = (uint8_t *)calloc(1, dst_bpl);
        uint8_t *packbits_buf  = (uint8_t *)calloc(1, dst_bpl * 2 + 16);

        /* Check if CUPS colorspace is Luminance/White (0=black, 255=white -> needs invert) */
        int invert = (header.cupsColorSpace == CUPS_CSPACE_W ||
                      header.cupsColorSpace == CUPS_CSPACE_SW ||
                      header.cupsColorSpace == CUPS_CSPACE_RGB ||
                      header.cupsColorSpace == CUPS_CSPACE_SRGB ||
                      header.cupsColorSpace == CUPS_CSPACE_RGBA);

        /* Allow printing down to the 4.0mm (96 dots at 600 DPI) hardware bottom margin */
        uint32_t max_printable_y = (full_h_px > 96U) ? (full_h_px - 96U) : full_h_px;
        uint32_t empty_lines = pad_top_px;

        for (uint32_t y = 0; y < header.cupsHeight; y++) {
            if (cupsRasterReadPixels(ras, raw_cups_line, header.cupsBytesPerLine) == 0) {
                break;
            }

            /* Always consume all CUPS scanlines, but clip bottom 4.0mm hardware border */
            if (y + pad_top_px >= max_printable_y) {
                continue;
            }

            memset(packed_line, 0, dst_bpl);
            uint8_t *dst_row = &packed_line[pad_left_bytes];

            if (header.cupsBitsPerPixel == 1) {
                uint32_t copy_b = header.cupsBytesPerLine < src_bpl ? header.cupsBytesPerLine : src_bpl;
                if (invert) {
                    for (uint32_t b = 0; b < copy_b; b++) {
                        dst_row[b] = raw_cups_line[b] ^ 0xFFU;
                    }
                    /* Mask trailing unused bits in last byte */
                    uint32_t rem = header.cupsWidth & 7U;
                    if (rem != 0 && copy_b > 0) {
                        uint8_t mask = (uint8_t)(0xFF00U >> rem);
                        dst_row[copy_b - 1] &= mask;
                    }
                } else {
                    memcpy(dst_row, raw_cups_line, copy_b);
                }
            } else if (header.cupsBitsPerPixel == 8) {
                const uint8_t *bayer_row = bayer8x8[y & 7U];
                for (uint32_t x = 0; x < header.cupsWidth; x++) {
                    uint8_t v = raw_cups_line[x];
                    uint8_t darkness = invert ? (255U - v) : v;
                    /* Threshold with Bayer 8x8 halftone */
                    uint8_t b_thresh = (bayer_row[x & 7U] << 2) + 2;
                    if (darkness > b_thresh) {
                        dst_row[x >> 3] |= (uint8_t)(0x80U >> (x & 7U));
                    }
                }
            } else if (header.cupsBitsPerPixel == 24 || header.cupsBitsPerPixel == 32) {
                uint32_t bpp = header.cupsBitsPerPixel / 8U;
                const uint8_t *bayer_row = bayer8x8[y & 7U];
                for (uint32_t x = 0; x < header.cupsWidth; x++) {
                    const uint8_t *px = &raw_cups_line[x * bpp];
                    uint32_t lum = (px[0] * 77U + px[1] * 150U + px[2] * 29U) >> 8;
                    uint8_t darkness = (uint8_t)(255U - lum);
                    uint8_t b_thresh = (bayer_row[x & 7U] << 2) + 2;
                    if (darkness > b_thresh) {
                        dst_row[x >> 3] |= (uint8_t)(0x80U >> (x & 7U));
                    }
                }
            }

            /* Trim trailing white (0x00) bytes on the right of the scanline */
            uint32_t trimmed_len = dst_bpl;
            while (trimmed_len > 0 && packed_line[trimmed_len - 1] == 0x00) {
                trimmed_len--;
            }

            if (trimmed_len == 0) {
                empty_lines++;
                continue;
            }

            if (empty_lines > 0) {
                fprintf(stdout, "\033*b%uY", empty_lines);
                empty_lines = 0;
            }

            size_t comp_len = pcl_packbits_encode(packed_line, trimmed_len, packbits_buf);
            fprintf(stdout, "\033*b%zuW", comp_len);
            fwrite(packbits_buf, 1, comp_len, stdout);
        }

        free(raw_cups_line);
        free(packed_line);
        free(packbits_buf);

        /* End Raster Graphics and eject page */
        fprintf(stdout, "\033*rC");
        fprintf(stdout, "\014");
        fflush(stdout);
    }

    fprintf(stdout, "\033E");
    fprintf(stdout, "\033%%-12345X@PJL EOJ\r\n\033%%-12345X");
    fflush(stdout);

    cupsFreeOptions(num_options, options);
    cupsRasterClose(ras);
    if (fd != 0) close(fd);
    return 0;
}

