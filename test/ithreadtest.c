#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "ithread.h"

#define GREYSCALE_IMAGE_DATA_STRUCTURE_ID (('G' << 24) + ('S' << 16) + ('c' << 8) + 'I')

typedef unsigned int uint;
typedef unsigned char byte;

typedef enum greyscale_image_format {
    GreyscaleImageFormatNormal = 0, GreyscaleImageFormatBlackAndWhiteRandom = 1, GreyscaleImageFormatUnusable = 2
} GreyscaleImageFormat;

typedef struct {
    uint struct_id;
    int width;
    int height;
    byte ** data;
} GreyscaleImage;

typedef struct {
    uint struct_id;
    int width;
    int height;
    GreyscaleImageFormat format;
    GreyscaleImage * image;
} GreyscaleImageJobData;

void successFunction(IWorkerThreadJob * iwj);
void failureFunction(IWorkerThreadJob * iwj);
void mainFunction(IWorkerThreadJob * iwj);
GreyscaleImageJobData * createGreycaleImageJobData();
void createGreyscaleImageFromJobData(GreyscaleImageJobData * jd);
void freeGreyscaleImage(GreyscaleImage * gsi);
bool saveGreyscaleImage(char * prefix, int thread_id, size_t job_id, GreyscaleImage * gsi);
bool freeGreyscaleImageJobData(GreyscaleImageJobData * jd);

int main(int argc, char ** argv)
{
    srand(time(NULL));
    IWorkerThreadController * iwtc = IWorkerThreadControllerCreate();
    if (!iwtc) exit(EXIT_FAILURE);

    for (size_t t = 0; t < 4; t++) {
        IWorkerThread * iwt = IWorkerThreadControllerAddWorkerThread(iwtc, mainFunction, successFunction, failureFunction, IThreadTimeoutNone);
        for (size_t j = 0; j < 8; j++) {
            GreyscaleImageJobData * jd = createGreycaleImageJobData();
            if (jd) {
                IWorkerThreadAddJob(iwt, jd);
            }
        }
        IWorkerThreadWaitForJobs(iwt, false);
    }

    if (IWorkerThreadControllerStart(iwtc)) {
        printf("Waiting for threads to finish generating greyscale images.\n");
        while (IWorkerThreadControllerIsRunning(iwtc)) {
            IThreadSleep(20);
        }
        printf("Closing down IWorkerThreadController.\n");
        IWorkerThreadControllerFree(iwtc);
    }
    exit(EXIT_SUCCESS);
}

void successFunction(IWorkerThreadJob * iwj)
{
    GreyscaleImageJobData * jd = (GreyscaleImageJobData *) IWorkerThreadJobGetData(iwj);
    IWorkerThread * iwt = (IWorkerThread *) IWorkerThreadJobGetParentThread(iwj);
    saveGreyscaleImage("Test Image", IWorkerThreadGetId(iwt), IWorkerThreadJobGetId(iwj), jd->image);
    freeGreyscaleImageJobData(jd);
}

void failureFunction(IWorkerThreadJob * iwj)
{
    GreyscaleImageJobData * jd = (GreyscaleImageJobData *) IWorkerThreadJobGetData(iwj);
    IWorkerThread * iwt = (IWorkerThread *) IWorkerThreadJobGetParentThread(iwj);
    printf("Failed to create and save greyscale image: thread id = %i, job id = %lu.\n",
        IWorkerThreadGetId(iwt),
        IWorkerThreadJobGetId(iwj));
    freeGreyscaleImageJobData(jd);
}

void mainFunction(IWorkerThreadJob * iwj) 
{
    if (!IWorkerThreadJobIsValid(iwj)) {
        printf("Invalid job data.\n");
        IWorkerThreadJobFailed(iwj,"Invalid job data.\n");
    } else {
        IWorkerThread * iwt = (IWorkerThread *) IWorkerThreadJobGetParentThread(iwj);
        printf("Creating greyscale image using: thread id = %i, job id = %lu.\n",
            IWorkerThreadGetId(iwt),
            IWorkerThreadJobGetId(iwj));

        GreyscaleImageJobData * jd = (GreyscaleImageJobData *) IWorkerThreadJobGetData(iwj);
        createGreyscaleImageFromJobData(jd);
    }
   
}

void freeGreyscaleImage(GreyscaleImage * gsi)
{
    if (!gsi || gsi->struct_id != GREYSCALE_IMAGE_DATA_STRUCTURE_ID) return;
    gsi->struct_id = 0;
    if (gsi->data) {
        for (int y = 0; y < gsi->height; y++) {
            if (gsi->data[y]) { 
                free(gsi->data[y]);
                gsi->data[y] = NULL;
            }
        }
        free(gsi->data);
        gsi->data = NULL;
    }
    gsi->width = gsi->height = 0;
    free(gsi);
}

void createGreyscaleImageFromJobData(GreyscaleImageJobData * jd)
{
    if (!jd || jd->struct_id != GREYSCALE_IMAGE_DATA_STRUCTURE_ID || jd->width == 0 || jd->height == 0 || jd->format == GreyscaleImageFormatUnusable) return;
    GreyscaleImage * gsi = (GreyscaleImage *) malloc(sizeof(GreyscaleImage));
    if (gsi) {
        gsi->struct_id = GREYSCALE_IMAGE_DATA_STRUCTURE_ID;
        gsi->width = jd->width;
        gsi->height = jd->height;
        gsi->data = (byte **) malloc(sizeof(byte *) * gsi->height);
        if (!gsi->data) goto failure;
        for (int y = 0; y < gsi->height; y++) {
            gsi->data[y] = (byte *) calloc(gsi->width, sizeof(byte));
            if (!gsi->data[y]) goto failure;
        }

        const int HALF_WIDTH_PIXELS = gsi->width / 2;
        const double LARGEST_SQRT_VALUE = sqrt((HALF_WIDTH_PIXELS * HALF_WIDTH_PIXELS) + (HALF_WIDTH_PIXELS * HALF_WIDTH_PIXELS));
        for (int y = 0; y < gsi->height; y++) {
            for (int x = 0; x < gsi->width; x++) {
                const double XPOS = x - HALF_WIDTH_PIXELS;
                const double YPOS = y - HALF_WIDTH_PIXELS;
                const double XPOS_SQ = XPOS * XPOS;
                const double YPOS_SQ = YPOS * YPOS;
                const double SQRT_XPOS_QS_AND_YPOS_SQ = LARGEST_SQRT_VALUE - sqrt(XPOS_SQ + YPOS_SQ);
                byte greyscale_value = (byte) (255.0 * (SQRT_XPOS_QS_AND_YPOS_SQ / LARGEST_SQRT_VALUE));
                switch (jd->format) {
                    case GreyscaleImageFormatBlackAndWhiteRandom : {
                        const byte RANDOM_VALUE = 1 + (rand() % 255);
                        greyscale_value = RANDOM_VALUE > greyscale_value ? 0 : 255;
                    } break;
                    default: {}
                }
                gsi->data[y][x] = greyscale_value;
            }
            // IThreadSleep(10);
        }
        jd->image = gsi;
    }

    return;

    failure:
        freeGreyscaleImage(gsi);
}

bool saveGreyscaleImage(char * prefix, int thread_id, size_t job_id, GreyscaleImage * gsi)
{
    if (!prefix || prefix[0] == 0 || thread_id < 0 || !gsi || gsi->struct_id != GREYSCALE_IMAGE_DATA_STRUCTURE_ID) return false;
    char filename[2048];
    size_t filename_chars_written = sprintf(filename, "%s-%i-%li.pgm", prefix, thread_id, job_id);
    filename[filename_chars_written] = 0;

    FILE * outfile = fopen(filename,"wb");
    if (!outfile) return false;

    fprintf(outfile, "P5 %i %i 255\n", gsi->width, gsi->height);
    for (int y = 0; y < gsi->height; y++) {
        fwrite(gsi->data[y], gsi->width, 1, outfile);
    }

    fclose(outfile);
    return true;
}

GreyscaleImageJobData * createGreycaleImageJobData()
{
    GreyscaleImageJobData * jd = (GreyscaleImageJobData *) malloc(sizeof(GreyscaleImageJobData));
    if (jd) {
        jd->struct_id = GREYSCALE_IMAGE_DATA_STRUCTURE_ID;
        const int DIMENSION_SIZE_PIXELS = 64 + (64 * (rand() % 16));
        jd->width = DIMENSION_SIZE_PIXELS;
        jd->height = DIMENSION_SIZE_PIXELS;
        jd->format = rand() % 2;
        jd->image = NULL;
    }
    return jd;
}

bool freeGreyscaleImageJobData(GreyscaleImageJobData * jd)
{
    if (!jd || jd->struct_id != GREYSCALE_IMAGE_DATA_STRUCTURE_ID) return false;
    jd->struct_id = 0;
    jd->width = jd->height = 0;
    jd->format = GreyscaleImageFormatUnusable;
    if (jd->image) {
        freeGreyscaleImage(jd->image);
        jd->image = NULL;
    }
    free(jd);
    return true;
}