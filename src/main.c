#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <FreeImage.h>

#include "queue.h"
#include "heap.h"

#define MAX_SPEC_LINE_LEN 1024

// Define the type of a queue of inputs.
// See queue.h and OpenBSD's documentation for details.
SIMPLEQ_HEAD(inputshd, input);

/**
 * An [input] represents an image we are packing.
 * The [at] field is initially null but eventually contains the [posn] in the
 * packed image at which this image is placed.
 */
struct input {
    /**
     * [name] is the filename of the image without the file extension (which is
     * always a PNG for pngsquare). Specified in the .pngsquare file. Becomes
     * the name ofthe field in the textures structure, so it should be a valid
     * C identifier name.
     */
    char *name;

    FIBITMAP *bitmap; //!< [bitmap] is the actual image data.
    /**
     * [at] is initially null but is set to the [posn] in the packed image at
     * which this image is placed by the packing procedure.
     */
    struct posn *at;

    unsigned w; //!< [w] is the width of the image in pixels.
    unsigned h; //!< [h] is the height of the image in pixels.

    // A pointer to the next item in the input queue. See queue.h for details.
    SIMPLEQ_ENTRY(input) entries;
};

/**
 * A [spec] structure contains the parsed data from the .pngsquare file given
 * as the argument to pngsquare. See the README for information about
 * specification files.
 */
struct spec {
    char *name; //!< [name] is used to name the struct and functions in generated code.
    char *png; //!< [png] is the path to the packed PNG.
    char *c; //!< [c] is the path to the generated C code.
    char *h; //!< [h] is the path to the generated C header.
    char *hi; //!< [hi] is the include path that the generated C code should use to load the C header.
    char *from; //!< [from] is the path to the directory where the images to pack are stored.
    int unit; //!< [unit] is the side length of the pixel square to use in the heuristic. See README for details.

    struct inputshd inputs; //!< The queue of [input]s to process.
};

/**
 * A [posn] represents a coordinate (x, y).
 * We use it to represent the pixel square with coordinates (x, y), which means
 * that to get the position of the pixel square in pixels you need to multiply
 * by [spec->unit].
 */
struct posn {
    unsigned x;
    unsigned y;
};

/**
 * A [grid] represents a two-dimensional grid of pixel squares (each of size
 * [unit] from [spec]) used in determining where an image can be placed.
 * The coordinates in a grid refer to whole pixel squares.
 * Grids are created using [grid_alloc].
 * [grid_mark] marks a position on a grid, while [grid_marked] checks if a
 * position is marked.
 * The storage for [grid]s is allocated as needed by [grid_mark], and should be
 * freed with [grid_free].
 */
struct grid {
    unsigned s; //!< [s] is the current side length of the grid. Should be a power of two.
    bool **posns; //!< [posns] records the marked positions.
};

struct input *input_alloc();
void input_free(struct input *input);

/**
 * [input_cmp a b] returns 1 if the max side length of the [input] [a] is
 * greater than the max side length of [b], -1 if it is less, and 0 if they are
 * equal. It is intended for use with [qsort].
 */
int input_cmp(const void *a, const void *b);

/**
 * [posn_cmp] returns [true] if the coordinates of the [posn] [a] is
 * less than the coordinates for [b] and false otherwise.
 * First the maximums of the x and y values are compared; if equality occurs,
 * then the individual components are compared.
 * It is intended for use with [heap].
 */
bool posn_cmp(const void *a, const void *b);

struct spec *spec_alloc();
void spec_free(struct spec *spec);

void parse_spec(struct spec *spec, const char *path);

/**
 * [parse_directive dst key stream] checks to makes ure the next directive in
 * [stream] looks like "<key> <value>", then stores the value string into [dst].
 * [dst] must have been allocated with enough memory to store [key].
 * At most [MAX_SPEC_LINE_LEN] bytes are read.
 */
char *parse_directive(char *dst, const char *key, FILE *stream);

struct grid *grid_alloc();
bool grid_marked(struct grid *grid, unsigned x, unsigned y);
bool grid_mark(struct grid *grid, unsigned x, unsigned y);
void grid_free(struct grid *grid);

/**
 * [isvalidname c] returns [true] iff [c] is a valid C identifier name.
 */
bool isvalidname(const char *c);

int
main(int argc, char *argv[])
{
    struct spec *spec = NULL;
    struct input *input = NULL;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <spec>\n", argv[0]);
        return 1;
    }

    spec = spec_alloc();
    assert(spec != NULL);

    parse_spec(spec, argv[1]);

    FreeImage_Initialise(false);

    // Load the image data for each [input].

    int inputslen = 0;
    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        char *path = malloc(strlen(input->name) + strlen(spec->from) + 6); // appending {/, .png, \0}
        assert(path != NULL);

        sprintf(path, "%s/%s.png", spec->from, input->name);

        input->bitmap = FreeImage_Load(FIF_PNG, path, 0);
        free(path);
        if (input->bitmap == NULL) {
            fprintf(stderr, "failed to load image at %s\n", path);
            goto close;
        }

        input->w = FreeImage_GetWidth(input->bitmap);
        input->h = FreeImage_GetHeight(input->bitmap);

        inputslen++;
    }

    // [inputsarr] will store pointers to [input]s sorted in order of
    // decreasing maximum side length.
    struct input **inputsarr = malloc(inputslen * sizeof(struct input *));
    assert(inputsarr != NULL);

    int i = 0;
    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        inputsarr[i] = input;
        i++;
    }

    qsort(inputsarr, inputslen, sizeof(struct input *), input_cmp);

    // [grid] will record which positions in the packed image already contain
    // an image (or a part of one). The minimum position unit is a square of
    // pixels of side length [spec->unit].
    struct grid *grid = grid_alloc();
    assert(grid != NULL);

    // [frontier] is a min-heap w.r.t. position coordinates which we will use
    // to get the position we should next try to place an input image at.
    struct heap *frontier = heap_init(posn_cmp);

    // [start] is the top-left corner, where we will try to place the first image.
    struct posn *start = malloc(sizeof(struct posn));
    assert(start != NULL);

    start->x = 0;
    start->y = 0;

    assert(!heap_push(frontier, start));

    // Pack all of the input images.
    // For an overview of the heuristic, see the README.
    for (int i = 0; i < inputslen; i++) {
        struct input *input = inputsarr[i];
        // Get the width and height in terms of [spec->unit].
        int wu = ceil((double)input->w / spec->unit);
        int hu = ceil((double)input->h / spec->unit);

        // [posnqhd] will point to a queue of positions we have tried to insert
        // the current image at but which didn't work, due to not enough space
        // being available. At the end of the attempting-to-place loop, we add
        // these back to the heap.
        SIMPLEQ_HEAD(posnqhd, posnq) posnqhd = SIMPLEQ_HEAD_INITIALIZER(posnqhd);
        struct posnq {
            struct posn *v;
            SIMPLEQ_ENTRY(posnq) entries;
        };

        // Loop while we try to find somewhere to put this input.
        for (;;) {
            if (frontier->len == 0) {
                // This should not be possible (means we've somehow ran out of
                // places to try placing images at.
                assert(false);
            }

            // [top] is the position we will try to place this input image at.
            struct posn *top = heap_pop(frontier);
            assert(top != NULL);

            // If the position has been filled by someone, there's no use
            // keeping it in the heap.
            if (grid_marked(grid, top->x, top->y)) {
                free(top);
                continue;
            }

            // Check to make sure there is enough space available at this
            // position to place the image.
            bool failed = false;
            for (int y = top->y; !failed && y < top->y + hu; y++) {
                for (int x = top->x; x < top->x + wu; x++) {
                    if (grid_marked(grid, x, y)) {
                        failed = true;
                        break;
                    }
                }
            }
            // If we failed, add this position to the queue of positions to add
            // back to the heap after we're done.  We don't want to add it back
            // immediately, obviously, because then we'd end up in an infinite
            // loop...
            if (failed) {
                struct posnq *posnq = malloc(sizeof(struct posnq *));
                assert(posnq != NULL);
                posnq->v = top;
                SIMPLEQ_INSERT_TAIL(&posnqhd, posnq, entries);
                continue;
            }

            // We haven't failed--mark the positions now occupied by the input
            // image!
            for (int y = top->y; y < top->y + hu; y++) {
                for (int x = top->x; x < top->x + wu; x++) {
                    grid_mark(grid, x, y);
                }
            }

            input->at = top;

            // [a] and [b] are the new positions for subsequent images to try.
            // One is at the north-east corner of this image, and the other is
            // at the southwest corner.
            struct posn *a = malloc(sizeof(struct posn));
            assert(a != NULL);

            struct posn *b = malloc(sizeof(struct posn));
            assert(b != NULL);

            a->x = top->x + wu;
            a->y = top->y;

            b->x = top->x;
            b->y = top->y + hu;

            assert(!heap_push(frontier, a));
            assert(!heap_push(frontier, b));

            break;
        }

        // Add the positions that didn't have space for the image back to the
        // queue, because they might work for a smaller image.
        struct posnq *posnq, *tposnq;
        SIMPLEQ_FOREACH_SAFE(posnq, &posnqhd, entries, tposnq) {
            assert(!heap_push(frontier, posnq->v));
            free(posnq);
        }
    }

    // Free the remaining positions.
    for (int i = 0; i < frontier->len; i++) {
        free(frontier->data[i]);
    }

    // [wf] and [hf] will contain width and height of the packed image.
    int wf = 0;
    int hf = 0;
    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        int wp = input->at->x * spec->unit + input->w;
        int hp = input->at->y * spec->unit + input->h;

        if (wp > wf)
            wf = wp;

        if (hp > hf)
            hf = hp;
    }

    heap_free(frontier);
    grid_free(grid);

    FIBITMAP *output = FreeImage_Allocate(wf, hf, 32, 0, 0, 0);
    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        struct posn *at = input->at;
        assert(FreeImage_Paste(output, input->bitmap, at->x * spec->unit, at->y * spec->unit, 256));
    }

    if (!FreeImage_Save(FIF_PNG, output, spec->png, 0)) {
        fprintf(stderr, "FreeImage_Save: failed to save output image to %s\n", spec->png); 
        goto close;
    }

    FILE *hfh = fopen(spec->h, "w");
    if (hfh == NULL) {
        fprintf(stderr, "fopen: failed to open file at %s for writing: %s\n", spec->h, strerror(errno));
        goto close;
    }

    // XXX This is a little messy. :(

    fprintf(hfh, "#ifndef %s_h\n", spec->name);
    fprintf(hfh, "#define %s_h\n\n", spec->name);

    fprintf(hfh, "#include <SDL2/SDL.h>\n\n");

    fprintf(hfh, "struct %s {\n", spec->name);
    fprintf(hfh, "    SDL_Texture *t;\n\n");
    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        fprintf(hfh, "    SDL_Rect *%s;\n", input->name);
    }
    fprintf(hfh, "};\n\n");
    
    fprintf(hfh, "struct %s *%s_load(SDL_Renderer *renderer);\n", spec->name, spec->name);
    fprintf(hfh, "void %s_unload (struct %s *pack);\n\n", spec->name, spec->name);

    fprintf(hfh, "#endif\n");

    if (fclose(hfh)) {
        fprintf(stderr, "fclose: %s\n", strerror(errno));
    }

    FILE *cfh = fopen(spec->c, "w");
    if (cfh == NULL) {
        fprintf(stderr, "fopen: failed to open file at %s for writing: %s\n", spec->c, strerror(errno));
        goto close;
    }

    fprintf(cfh, "#include <assert.h>\n\n");
    fprintf(cfh, "#include <SDL2/SDL.h>\n#include <SDL2/SDL_image.h>\n\n");

    fprintf(cfh, "#include \"%s\"\n\n", spec->hi);

    fprintf(cfh, "static const char *PNG_PATH = \"%s\";\n\n", spec->png);

    fprintf(cfh, "struct %s *\n", spec->name);
    fprintf(cfh, "%s_load(SDL_Renderer *renderer)\n", spec->name);
    fprintf(cfh, "{\n");
    fprintf(cfh, "    struct %s *pack = malloc(sizeof(struct %s));\n", spec->name, spec->name);
    fprintf(cfh, "    assert(pack != NULL);\n\n");

    fprintf(cfh, "    SDL_Surface* raw = IMG_Load(PNG_PATH);\n");
    fprintf(cfh, "    if (raw == NULL) {\n");
    fprintf(cfh, "        fprintf(stderr, \"%s: failed to load image %%s: %%s\\n\", PNG_PATH, IMG_GetError());\n", spec->name);
    fprintf(cfh, "        exit(1);\n");
    fprintf(cfh, "    }\n\n");

    fprintf(cfh, "    pack->t = SDL_CreateTextureFromSurface(renderer, raw);\n");
    fprintf(cfh, "    if (pack->t == NULL) {\n");
    fprintf(cfh, "        fprintf(stderr, \"%s: failed to create texture of image %%s: %%s\\n\", PNG_PATH, SDL_GetError());\n", spec->name);
    fprintf(cfh, "        exit(1);\n");
    fprintf(cfh, "    }\n\n");

    fprintf(cfh, "    SDL_FreeSurface(raw);\n\n");

    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        fprintf(cfh, "    pack->%s = malloc(sizeof(SDL_Rect));\n", input->name);
        fprintf(cfh, "    assert(pack->%s != NULL);\n", input->name);
        fprintf(cfh, "    pack->%s->x = %d;\n", input->name, input->at->x * spec->unit);
        fprintf(cfh, "    pack->%s->y = %d;\n", input->name, input->at->y * spec->unit);
        fprintf(cfh, "    pack->%s->w = %d;\n", input->name, input->w);
        fprintf(cfh, "    pack->%s->h = %d;\n\n", input->name, input->h);
    }

    fprintf(cfh, "    return pack;\n");
    fprintf(cfh, "}\n\n");

    fprintf(cfh, "void\n");
    fprintf(cfh, "%s_unload (struct %s *pack)\n", spec->name, spec->name);
    fprintf(cfh, "{\n");
    
    SIMPLEQ_FOREACH(input, &spec->inputs, entries) {
        fprintf(cfh, "    free(pack->%s);\n", input->name);
    }

    fprintf(cfh, "    free(pack);\n");
    fprintf(cfh, "}\n");


    if (fclose(cfh)) {
        fprintf(stderr, "fclose: %s\n", strerror(errno));
    }
close:
    spec_free(spec);
    FreeImage_DeInitialise();
}

struct spec *
spec_alloc()
{
    struct spec *spec = malloc(sizeof(struct spec));
    if (spec == NULL) return NULL;

    spec->name = NULL;
    spec->png = NULL;
    spec->c = NULL;
    spec->h = NULL;
    spec->hi = NULL;
    spec->from = NULL;
    spec->unit = 0;
    SIMPLEQ_INIT(&spec->inputs);

    return spec;
}

void
spec_free(struct spec *spec)
{
    free(spec->name);
    free(spec->png);
    free(spec->c);
    free(spec->h);
    free(spec->hi);
    free(spec->from);

    while (!SIMPLEQ_EMPTY(&spec->inputs)) {
        struct input *input = SIMPLEQ_FIRST(&spec->inputs);
        SIMPLEQ_REMOVE_HEAD(&spec->inputs, entries);
        input_free(input);
    }

    free(spec);
}

struct input *
input_alloc()
{
    struct input *input = malloc(sizeof(struct input));
    if (input == NULL) return NULL;

    input->name = NULL;
    input->bitmap = NULL;
    input->at = NULL;
    input->w = 0;
    input->h = 0;

    return input;
}

void
input_free(struct input *input)
{
    free(input->name);
    free(input->at);

    if (input->bitmap != NULL) {
        FreeImage_Unload(input->bitmap);
    }

    free(input);
}

int
input_cmp(const void *a, const void *b)
{
    const struct input *i = *(const struct input **)a;
    const struct input *j = *(const struct input **)b;

    unsigned mi = i->w > i->h ? i->w : i->h;
    unsigned mj = j->w > j->h ? j->w : j->h;

    if (mi < mj)
        return 1;
    else if (mi > mj)
        return -1;
    else
        return 0;
}

bool
posn_cmp(const void *a, const void *b)
{
    const struct posn *i = a;
    const struct posn *j = b;

    unsigned mi = i->x > i->y ? i->x : i->y;
    unsigned mj = j->x > j->y ? j->x : j->y;

    if (mi == mj) {
        if (i->x < j->x || i->y < j->y)
            return true;
        else
            return false;
    }

    return mi < mj;
}

void
parse_spec(struct spec *spec, const char *path)
{
    FILE *stream = fopen(path, "r");
    if (stream == NULL) {
        fprintf(stderr, "parse_spec: fopen: %s\n", strerror(errno));
        exit(1);
    }

    bool failed = true;
    char *unitraw = NULL;

    // name <val>
    spec->name = malloc(MAX_SPEC_LINE_LEN);
    assert(spec->name != NULL);
    // png <val>
    spec->png = malloc(MAX_SPEC_LINE_LEN);
    assert(spec->png != NULL);
    // c <val>
    spec->c = malloc(MAX_SPEC_LINE_LEN);
    assert(spec->c != NULL);
    // h <val>
    spec->h = malloc(MAX_SPEC_LINE_LEN);
    assert(spec->h != NULL);
    // hi <val>
    spec->hi = malloc(MAX_SPEC_LINE_LEN);
    assert(spec->hi != NULL);
    // from <val>
    spec->from = malloc(MAX_SPEC_LINE_LEN);
    assert(spec->from != NULL);

    if (parse_directive(spec->name, "name", stream) == NULL || !isvalidname(spec->name)) {
        goto close;
    }
    if (parse_directive(spec->png, "png", stream) == NULL) {
        goto close;
    }
    if (parse_directive(spec->c, "c", stream) == NULL) {
        goto close;
    }
    if (parse_directive(spec->h, "h", stream) == NULL) {
        goto close;
    }
    if (parse_directive(spec->hi, "hi", stream) == NULL) {
        goto close;
    }
    if (parse_directive(spec->from, "from", stream) == NULL) {
        goto close;
    }

    unitraw = malloc(MAX_SPEC_LINE_LEN);
    if (parse_directive(unitraw, "unit", stream) == NULL) {
        goto close;
    }

    int unit = atoi(unitraw);
    if (!unit) {
        fprintf(stderr, "the unit directive must specify a positive integer\n");
        goto close;
    }
    spec->unit = unit;

    for (;;) {
        char *line = malloc(MAX_SPEC_LINE_LEN);
        assert(line != NULL);

        if (fgets(line, MAX_SPEC_LINE_LEN, stream) == NULL) {
            free(line);
            break;
        }

        size_t len = strlen(line);
        if (len < 2) {
            free(line);
            continue;
        }
        // trim trailing newline
        line[len - 1] = '\0';

        if (!isvalidname(line)) {
            free(line);
            fprintf(stderr, "the name '%s' must match [a-zA-Z][a-zA-Z0-9_].\n", line);
            goto close;
        }

        struct input *newest = input_alloc();
        assert(newest != NULL);

        newest->name = line;

        SIMPLEQ_INSERT_TAIL(&spec->inputs, newest, entries);
    }

    failed = false;

close:;
    free(unitraw);

    if (fclose(stream)) {
        fprintf(stderr, "parse_spec: fclose: %s\n", strerror(errno));
        failed = true;
    }

    if (failed) {
        spec_free(spec);
        exit(1);
    }
}

char *
parse_directive(char *dst, const char *key, FILE *stream)
{
    char *line = malloc(MAX_SPEC_LINE_LEN);
    assert(line != NULL);

    if (fgets(line, MAX_SPEC_LINE_LEN, stream) == NULL) {
        fprintf(stderr, "parse_directive: unexpected EOF or read error\n");
        goto fail;
    }

    size_t len = strlen(line);
    if (len == 0) {
        goto fail;
    }
    line[len - 1] = '\0';

    size_t keylen = strlen(key);
    if (len < keylen + 2 || strncmp(line, key, keylen) || line[keylen] != ' ') {
        fprintf(stderr, "parse_directive: expected '%s <data>', got '%s'\n", key, line);
        goto fail;
    }

    strncpy(dst, line + keylen + 1, len - keylen - 1);
    return dst;

fail:
    free(line);
    return NULL;
}

struct grid *
grid_alloc()
{
    struct grid *grid = malloc(sizeof(struct grid));
    assert(grid != NULL);

    grid->s = 1;

    grid->posns = malloc(sizeof(bool *));
    assert(grid->posns != NULL);

    grid->posns[0] = malloc(sizeof(bool));
    assert(grid->posns[0] != NULL);

    grid->posns[0][0] = false;

    return grid;
}

bool
grid_marked(struct grid *grid, unsigned x, unsigned y)
{
    if (x >= grid->s || y >= grid->s)
        return false;

    return grid->posns[y][x];
}

bool
grid_mark(struct grid *grid, unsigned x, unsigned y)
{
    bool resized = false;
    if (x >= grid->s || y >= grid->s) {
        int sn = 2 << (int)(ceil(log2(x > y ? x : y)));

        bool **posnsn = realloc(grid->posns, sn * sizeof(bool *));
        assert(posnsn != NULL);

        for (int y = 0; y < grid->s; y++) {
            bool *rown = realloc(posnsn[y], sn * sizeof(bool));
            assert(rown != NULL);
            memset(rown + grid->s, 0, sn - grid->s);
            posnsn[y] = rown;
        }

        for (int y = grid->s; y < sn; y++) {
            posnsn[y] = calloc(sn, sizeof(bool));
            assert(posnsn[y] != NULL);
        }

        grid->posns = posnsn;

        grid->s = sn;
        resized = true;
    }

    grid->posns[y][x] = true;

    return resized;
}

void
grid_free(struct grid *grid)
{
    for (int y = 0; y < grid->s; y++) {
        free(grid->posns[y]);
    }
    free(grid->posns);
}

bool
isvalidname(const char *c)
{
    size_t l = strlen(c);
    if (l == 0) return false;
    if (!isalpha(c[0])) return false;
    if (strlen(c) == 1) return true;

    for (int i = 0; i < l; i++)
        if (!(isalpha(c[i]) || isdigit(c[i]) || c[i] == '_')) return false;

    return true;
}
