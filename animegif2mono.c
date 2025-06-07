#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <err.h>

#include <gif_lib.h>

#define GCE_TCF		0x01

#define BW_THRESHOLD	128
#define EDGE_THRESHOLD	200

#define CLIP(val, min, max)	\
    ((val) > (max) ? (max) : ((val) < (min) ? (min) : (val)))

#define DITHER_FS	0
#define DITHER_BAYER	1
#define DITHER_ATKINSON	2
#define DITHER_STUCKI	3
#define DITHER_BURKES	4
#define DITHER_SIERRA	5
#define DITHER_MAX	DITHER_SIERRA

static const char *progname = NULL;

/* 色視覚輝度推定近似式によるグレースケール化 */
static inline int
rgb_to_gray(int r, int g, int b)
{

    return (299 * r + 587 * g + 114 * b) / 1000;
}

/* コントラスト調整 (-100〜100) */
static int
adjust_contrast(int gray, int contrast)
{
    double factor = (259.0 * (contrast + 255.0)) / (255.0 * (259.0 - contrast));
    int new_val = (int)(factor * (gray - 128) + 128);

    return CLIP(new_val, 0, 255);
}

/* Floyd Steinberg モノクロディザ2値化処理 */
static void
fs_dither(uint8_t *gray, uint8_t *mono, int w, int h)
{
    int i, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int idx = y * w + x;
            int old_pixel = gray[idx];
            int new_pixel = old_pixel < BW_THRESHOLD ? 0 : 255;
            int err = old_pixel - new_pixel;
            int gval;

            gray[idx] = new_pixel;

            if (x + 1 < w) {
                gval = gray[y * w + (x + 1)] + (err * 70 + 5) / 160;
                gray[y * w + (x + 1)] = CLIP(gval, 0, 255);
            }
            if (y + 1 < h) {
                if (x > 0) {
                    gval = gray[(y + 1) * w + (x - 1)] + (err * 30 + 5) / 160;
                    gray[(y + 1) * w + (x - 1)] = CLIP(gval, 0, 255);
                }
                gval = gray[(y + 1) * w + x] + (err * 50 + 5) / 160;
                gray[(y + 1) * w + x] = CLIP(gval, 0, 255);
                if (x + 1 < w) {
                    gval = gray[(y + 1) * w + (x + 1)] + (err * 10 + 5) / 160;
                    gray[(y + 1) * w + (x + 1)] = CLIP(gval, 0, 255);
                }
            }
        }
    }
    for (i = 0; i < w * h; i++) {
        mono[i] = gray[i] >= BW_THRESHOLD ? 1 : 0;
    }
}

/* Bayer 4x4 モノクロディザ2値化処理 */ 
static void
bayer_dither(uint8_t *gray, uint8_t *mono, int w, int h)
{
    int i, x, y;
    static const int bayer4[4][4] = {
        {  15, 135,  45, 165 },
        { 195,  75, 225, 105 },
        {  60, 180,  30, 150 },
        { 240, 120, 210,  90 }
    };

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int threshold = bayer4[y % 4][x % 4];
            gray[y * w + x] = (gray[y * w + x] > threshold) ? 255 : 0;
        }
    }
    for (i = 0; i < w * h; i++) {
        mono[i] = gray[i] == 255 ? 1 : 0;
    }
}

/* Atkinson モノクロディザ2値化処理 */
static void
atkinson_dither(uint8_t *gray, uint8_t *mono, int w, int h)
{
    int i, x, y;

    for (y = 0; y < h - 2; y++) {
        for (x = 1; x < w - 2; x++) {
            int old, new, error, gval;

            i = y * w + x;
            old = gray[i];
            new = old < BW_THRESHOLD ? 0 : 255;
            error = (old - new) / 8;
            gray[i]          = new;
            if (x + 1 < w) {
                gval = gray[i + 1] + error;
                gray[i + 1] = CLIP(gval, 0, 255);
            }
            if (x + 2 < w) {
                gval = gray[i + 2] + error;
                gray[i + 2] = CLIP(gval, 0, 255);
            }
            if (y + 1 < h) {
                if (x - 1 >= 0) {
                    gval = gray[i + w - 1] + error;
                    gray[i + w - 1] = CLIP(gval, 0, 255);
                }
                gval = gray[i + w] + error; 
                gray[i + w] = CLIP(gval, 0, 255);
                if (x + 1 < w) {
                    gval = gray[i + w + 1] + error;
                    gray[i + w + 1] = CLIP(gval, 0, 255);
                }
            }
            if (y + 2 < h) {
                gval = gray[i + 2 * w] + error;
                gray[i + 2 * w] = CLIP(gval, 0, 255);
            }
        }
    }
    for (i = 0; i < w * h; i++) {
        mono[i] = gray[i] == 255 ? 1 : 0;
    }
}

/* Stucki モノクロディザ2値化処理 */
static void
stucki_dither(uint8_t *gray, uint8_t *mono, int w, int h)
{
    int i, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int old, new, error, gval;

            i = y * w + x;
            old = gray[i];
            new = old < 128 ? 0 : 255;
            error = old - new;
            gray[i] = new;

            if (x + 1 < w) {
                gval = gray[i + 1] + (error * 80 + 5) / 420;
                gray[i + 1] = CLIP(gval, 0, 255);
            }
            if (x + 2 < w) {
                gval = gray[i + 2] + (error * 40 + 5) / 420;
                gray[i + 2] = CLIP(gval, 0, 255);
            }
            if (y + 1 < h) {
                if (x - 2 >= 0) {
                    gval = gray[i + w - 2] + (error * 20 + 5) / 420;
                    gray[i + w - 2] = CLIP(gval, 0, 255);
                }
                if (x - 1 >= 0) {
                    gval = gray[i + w - 1] + (error * 40 + 5) / 420;
                    gray[i + w - 1] = CLIP(gval, 0, 255);
                }
                gval = gray[i + w] + (error * 80 + 5) / 420;
                gray[i + w] = CLIP(gval, 0, 255);
                if (x + 1 < w) {
                    gval = gray[i + w + 1] + (error * 40 + 5) / 420;
                    gray[i + w + 1] = CLIP(gval, 0, 255);
                }
                if (x + 2 < w) {
                    gval = gray[i + w + 2] + (error * 20 + 5) / 420;
                    gray[i + w + 2] = CLIP(gval, 0, 255);
                }
            }
            if (y + 2 < h) {
                if (x - 2 >= 0) {
                    gval = gray[i + w * 2 - 2] + (error * 10 + 5) / 420;
                    gray[i + w * 2 - 2] = CLIP(gval, 0, 255);
                }
                if (x - 1 >= 0) {
                    gval = gray[i + w * 2 - 1] + (error * 20 + 5) / 420;
                    gray[i + w * 2 - 1] = CLIP(gval, 0, 255);
                }
                gval = gray[i + w * 2] + (error * 40 + 5) / 420;
                gray[i + w * 2] = CLIP(gval, 0, 255);
                if (x + 1 < w) {
                    gval = gray[i + w * 2 + 1] + (error * 20 + 5) / 420;
                    gray[i + w * 2 + 1] = CLIP(gval, 0, 255);
                }
                if (x + 2 < w) {
                    gval = gray[i + w * 2 + 2] + (error * 10 + 5) / 420;
                    gray[i + w * 2 + 2] = CLIP(gval, 0, 255);
                }
            }
        }
    }
    for (i = 0; i < w * h; i++) {
        mono[i] = gray[i] == 255 ? 1 : 0;
    }
}

/* Burkes モノクロディザ2値化処理 */
static void
burkes_dither(uint8_t *gray, uint8_t *mono, int w, int h)
{
    int i, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int old, new, error, gval;

            i = y * w + x;
            old = gray[i];
            new = old < 128 ? 0 : 255;
            error = old - new;
            gray[i] = new;

            if (x + 1 < w) {
                gval = gray[i + 1] + (error * 80 + 5) / 320;
                gray[i + 1] = CLIP(gval, 0, 255);
            }
            if (x + 2 < w) {
                gval = gray[i + 2] + (error * 40 + 5) / 320;
                gray[i + 2] = CLIP(gval, 0, 255);
            }
            if (y + 1 < h) {
                if (x - 2 >= 0) {
                    gval = gray[i + w - 2] + (error * 20 + 5) / 320;
                    gray[i + w - 2] = CLIP(gval, 0, 255);
                }
                if (x - 1 >= 0) {
                    gval = gray[i + w - 1] + (error * 40 + 5) / 320;
                    gray[i + w - 1] = CLIP(gval, 0, 255);
                }
                gval = gray[i + w] + (error * 80 + 5) / 320;
                gray[i + w] = CLIP(gval, 0, 255);
                if (x + 1 < w) {
                    gval = gray[i + w + 1] + (error * 40 + 5) / 320;
                    gray[i + w + 1] = CLIP(gval, 0, 255);
                }
                if (x + 2 < w) {
                    gval = gray[i + w + 2] + (error * 20 + 5) / 320;
                    gray[i + w + 2] = CLIP(gval, 0, 255);
                }
            }
        }
    }
    for (i = 0; i < w * h; i++) {
        mono[i] = gray[i] == 255 ? 1 : 0;
    }
}

/* Sierra Lite モノクロディザ2値化処理 */
static void
sierra_lite_dither(uint8_t *gray, uint8_t *mono, int w, int h) {
    int i, x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int old, new, error, gval;

            i = y * w + x;
            old = gray[i];
            new = old < BW_THRESHOLD ? 0 : 255;
            error = old - new;
            gray[i] = new;

            if (x + 1 < w) {
                gval = gray[i + 1] + (error * 20 + 5) / 40;
                gray[i + 1] = CLIP(gval, 0, 255);
            }
            if (y + 1 < h) {
                if (x > 0) {
                    gval = gray[(y + 1) * w + x - 1] + (error * 10 + 5) / 40;
                    gray[(y + 1) * w + x - 1] = CLIP(gval, 0, 255);
                }
                gval = gray[(y + 1) * w + x] + (error * 10 + 5) / 40;
                gray[(y + 1) * w + x] = CLIP(gval, 0, 255);
            }
        }
    }
    for (i = 0; i < w * h; i++) {
        mono[i] = gray[i] == 255 ? 1 : 0;
    }
}

/* Sobel輪郭抽出 */
static void
edge_detect(uint8_t *gray, uint8_t *edge, int w, int h)
{
    int x, y, gx, gy;

    for (y = 1; y < h - 1; y++) {
        for (x = 1; x < w - 1; x++) {
            int idx = y * w + x;
            unsigned int mag;

            gx =
              -     gray[(y - 1) * w + (x - 1)]
              - 2 * gray[(y    ) * w + (x - 1)]
              -     gray[(y + 1) * w + (x - 1)]
              +     gray[(y - 1) * w + (x + 1)]
              + 2 * gray[(y    ) * w + (x + 1)]
              +     gray[(y + 1) * w + (x + 1)];
            gy =
                    gray[(y - 1) * w + (x - 1)]
              + 2 * gray[(y - 1) * w + (x    )]
              +     gray[(y - 1) * w + (x + 1)]
              -     gray[(y + 1) * w + (x - 1)]
              - 2 * gray[(y + 1) * w + (x    )]
              -     gray[(y + 1) * w + (x + 1)];
            mag = abs(gx) + abs(gy);
            if (mag > 255)
                mag = 255;
            edge[idx] = (uint8_t)mag;
        }
    }
    /* 画像最外周は輪郭検出対象外にする */
    for (x = 0; x < w; x++) {
        edge[x] = 0;
        edge[(h - 1) * w + x] = 0;
    }
    for (y = 0; y < h; y++) {
        edge[y * w] = 0;
        edge[y * w + (w - 1)] = 0;
    }
}

static void
usage(void)
{

    fprintf(stderr,
      "Usage: %s [-c contrast (-100..100)] [-d dither_method (0..%d)] [-e] "
      "input.gif output.gif\n",
      progname != NULL ? progname : "animegif2mono", DITHER_MAX);
    exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
    char *progpath;
    int opt;
    int contrast = 0;
    int dither_method = 0;
    int edge_mode = 0;
    const char *input_path, *output_path;
    int error;
    GifFileType *gif_in, *gif_out;
    const GifColorType mono_colors[2] = {
        {  0,   0,   0},
        {255, 255, 255}
    };
    ColorMapObject *mono_map;
    int sw, sh;
    int i, frame;
    uint8_t *canvas, *gray, *edge, *mono;

    progpath = strdup(argv[0]);
    progname = basename(progpath);

    while ((opt = getopt(argc, argv, "c:d:e")) != -1) {
        char *endptr;
        switch (opt) {
        case 'c':
            contrast = (int)strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || contrast < -100 || contrast > 100) {
                usage();
            }
            break;
        case 'd':
            dither_method = (int)strtol(optarg, &endptr, 10);
            if (*endptr != '\0' ||
              dither_method < 0 || dither_method > DITHER_MAX) {
                usage();
            }
            break;
        case 'e':
            edge_mode = 1;
            break;
        default:
            usage();
        }
    }

    if (optind + 2 != argc) {
        usage();
    }

    input_path = argv[optind];
    output_path = argv[optind + 1];

    /* 入力GIF画像オープン */
    gif_in = DGifOpenFileName(input_path, &error);
    if (gif_in == NULL) {
        errx(EXIT_FAILURE, "DGifOpenFileName failed: %s",
          GifErrorString(error));
    }

    /* 画像ファイルを内部表現データに取り込み */
    if (DGifSlurp(gif_in) == GIF_ERROR) {
        errx(EXIT_FAILURE, "DGifSlurp failed: %s",
          GifErrorString(gif_in->Error));
    }

    /* 出力GIF画像オープン */
    gif_out = EGifOpenFileName(output_path, 0, &error);
    if (gif_out == NULL) {
        errx(EXIT_FAILURE, "EGifOpenFileName failed: %s",
          GifErrorString(error));
    }

    /* 出力画像用モノクロカラーマップ */
    mono_map = GifMakeMapObject(2, mono_colors);

    /* 出力GIFの GIF Header 画像情報を出力（2値化以外入力GIF画像のまま） */
    sw = gif_in->SWidth;
    sh = gif_in->SHeight;
    if (EGifPutScreenDesc(gif_out, sw, sh, 0, 0, mono_map) == GIF_ERROR) {
        errx(EXIT_FAILURE, "EGifPutScreenDesc failed: %s",
          GifErrorString(gif_out->Error));
    }

    /* 出力GIFの Application Extension を出力（入力GIF画像のまま） */
    for (i = 0; i < gif_in->ExtensionBlockCount; i++) {
        ExtensionBlock *ext = &gif_in->ExtensionBlocks[i];
        if (ext->Function == APPLICATION_EXT_FUNC_CODE) {
            if (EGifPutExtension(gif_out, ext->Function, ext->ByteCount,
              ext->Bytes) == GIF_ERROR) {
                errx(EXIT_FAILURE, "EGifPutExtension (APPL) failed: %s",
                  GifErrorString(gif_out->Error));
            }
        }
    }

    /* 前フレーム保持分を含めたグレースケール画像データ */
    canvas = malloc(sw * sh);
    /* 各フレームのグレースケール化画像データ */
    gray = malloc(sw * sh);
    /* 画像輪郭抽出データ */
    edge = malloc(sw * sh);
    /* モノクロディザ2値化後の画像データ */
    mono = malloc(sw * sh);
    if (canvas == NULL || gray == NULL || edge == NULL || mono == NULL) {
        errx(EXIT_FAILURE, "malloc for images failed");
    }

    /* 先頭フレーム画像に透明色があった場合は白(255)として扱う */
    memset(canvas, 255, sw * sh);

    /* 各フレーム画像の処理 */
    for (frame = 0; frame < gif_in->ImageCount; frame++) {
        SavedImage *image = &gif_in->SavedImages[frame];
        GifImageDesc *d = &image->ImageDesc;
        GraphicsControlBlock gcb;
        int fw = d->Width;
        int fh = d->Height;
        int transparent_index = NO_TRANSPARENT_COLOR;
        int x, y;
        ColorMapObject *cm;

        /* 各フレームの透明色インデックスを抽出 */
        if (DGifSavedExtensionToGCB(gif_in, frame, &gcb) == GIF_ERROR) {
            errx(EXIT_FAILURE, "DGifSavedExtensionToGCB failed: %s",
              GifErrorString(gif_in->Error));
        }
        if (gcb.TransparentColor != NO_TRANSPARENT_COLOR) {
            transparent_index = gcb.TransparentColor;
        }

        /* フレーム別カラーマップがない場合はグローバルカラーマップを使用 */
        cm = d->ColorMap != NULL ? d->ColorMap : gif_in->SColorMap;

        /*
         * 入力画像の各フレームサイズがGIFスクリーンサイズより小さい場合は
         * 各フレームのデータの位置とサイズを見て画像データが存在する部分
         * のみ更新する（データがない部分は一つ前のフレームの値を保持）
         */
        for (y = 0; y < fh; y++) {
            for (x = 0; x < fw; x++) {
                int dst_x = d->Left + x;
                int dst_y = d->Top + y;
                int src_idx;
                int color_idx;

                /* GIFスクリーンサイズ外のデータはスキップ */
                if (dst_x >= sw || dst_y >= sh)
                    continue;

                src_idx = y * fw + x;
                color_idx = image->RasterBits[src_idx];
                if (color_idx != transparent_index) {
                    GifColorType *c;
                    int gval;
                    int dst_idx = dst_y * sw + dst_x;

                    /* RGB→グレースケール変換 */
                    c = &cm->Colors[color_idx];
                    gval = rgb_to_gray(c->Red, c->Green, c->Blue);

                    if (contrast != 0) {
                        /* コントラスト調整 */
                        gval = adjust_contrast(gval, contrast);
                    }
                    canvas[dst_idx] = gval;
                } else {
                    /* 透過色の場合は一つ前のフレームの値のまま保持 */
                }
            }
        }

        /*
         * 入力画像の各フレームサイズがGIFスクリーンサイズより小さい場合も
         * 出力画像の各フレームは全てGIFスクリーンサイズで出力する
         * （モノクロディザ2値化後は前フレームデータ保持の意味がない）
         */
        memcpy(gray, canvas, sw * sh);

        if (edge_mode) {
            /* 輪郭抽出 */
            edge_detect(gray, edge, sw, sh);

            /* 輪郭の強度がしきい値以上なら黒(0)で上書き */
            for (i = 0; i < sw * sh; i++) {
                if (edge[i] > EDGE_THRESHOLD) {
                    gray[i] = 0;
                }
            }
        }

        /* グレースケール画像→ディザ2値化 */
        switch (dither_method) {
        case DITHER_FS:
        default:
            fs_dither(gray, mono, sw, sh);
            break;
        case DITHER_BAYER:
            bayer_dither(gray, mono, sw, sh);
            break;
        case DITHER_ATKINSON:
            atkinson_dither(gray, mono, sw, sh);
            break;
        case DITHER_STUCKI:
            stucki_dither(gray, mono, sw, sh);
            break;
        case DITHER_BURKES:
            burkes_dither(gray, mono, sw, sh);
            break;
        case DITHER_SIERRA:
            sierra_lite_dither(gray, mono, sw, sh);
            break;
        }

        /* 各フレームの Graphic Control Extension を出力 */
        for (i = 0; i < image->ExtensionBlockCount; i++) {
            ExtensionBlock *ext = &image->ExtensionBlocks[i];
            if (ext->Function == GRAPHICS_EXT_FUNC_CODE) {
                if (ext->ByteCount == 4) {
                    /* 透過色は残らないので Transparent Color Flag クリア */
                    ext->Bytes[0] &= ~GCE_TCF;
                }
                if (EGifPutExtension(gif_out, ext->Function, ext->ByteCount,
                  ext->Bytes) == GIF_ERROR) {
                    errx(EXIT_FAILURE, "EGifPutExtension (GRAPH) failed: %s",
                      GifErrorString(gif_out->Error));
                }
            }
        }

        /* 各フレームの Image Block の画像情報を出力（サイズはGIF画像全体） */
        if (EGifPutImageDesc(gif_out, 0, 0, sw, sh, false, mono_map)
            == GIF_ERROR) {
            errx(EXIT_FAILURE, "EGifPutImageDesc failed: %s",
              GifErrorString(gif_out->Error));
        }

        /* 各フレームの Image Block の Image Data に2値化後イメージを出力 */
        for (y = 0; y < sh; y++) {
            if (EGifPutLine(gif_out, &mono[y * sw], sw) == GIF_ERROR) {
                errx(EXIT_FAILURE, "EGifPutLine failed: %s",
                  GifErrorString(gif_out->Error));
            }
        }
    }

    free(mono);
    free(edge);
    free(gray);
    free(canvas);
    EGifCloseFile(gif_out, &error);
    DGifCloseFile(gif_in, &error);
    GifFreeMapObject(mono_map);

    exit(EXIT_SUCCESS);
}
