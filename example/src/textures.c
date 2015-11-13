#include <assert.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "textures.h"

static const char *PNG_PATH = "images/textures.png";

struct textures *
textures_load(SDL_Renderer *renderer)
{
    struct textures *pack = malloc(sizeof(struct textures));
    assert(pack != NULL);

    SDL_Surface* raw = IMG_Load(PNG_PATH);
    if (raw == NULL) {
        fprintf(stderr, "textures: failed to load image %s: %s\n", PNG_PATH, IMG_GetError());
        exit(1);
    }

    pack->t = SDL_CreateTextureFromSurface(renderer, raw);
    if (pack->t == NULL) {
        fprintf(stderr, "textures: failed to create texture of image %s: %s\n", PNG_PATH, SDL_GetError());
        exit(1);
    }

    SDL_FreeSurface(raw);

    pack->blob_0 = malloc(sizeof(SDL_Rect));
    assert(pack->blob_0 != NULL);
    pack->blob_0->x = 32;
    pack->blob_0->y = 0;
    pack->blob_0->w = 16;
    pack->blob_0->h = 16;

    pack->blob_1 = malloc(sizeof(SDL_Rect));
    assert(pack->blob_1 != NULL);
    pack->blob_1->x = 32;
    pack->blob_1->y = 16;
    pack->blob_1->w = 16;
    pack->blob_1->h = 16;

    pack->enemy_0 = malloc(sizeof(SDL_Rect));
    assert(pack->enemy_0 != NULL);
    pack->enemy_0->x = 32;
    pack->enemy_0->y = 32;
    pack->enemy_0->w = 16;
    pack->enemy_0->h = 16;

    pack->enemy_1 = malloc(sizeof(SDL_Rect));
    assert(pack->enemy_1 != NULL);
    pack->enemy_1->x = 48;
    pack->enemy_1->y = 0;
    pack->enemy_1->w = 16;
    pack->enemy_1->h = 16;

    pack->tile_normal = malloc(sizeof(SDL_Rect));
    assert(pack->tile_normal != NULL);
    pack->tile_normal->x = 0;
    pack->tile_normal->y = 0;
    pack->tile_normal->w = 32;
    pack->tile_normal->h = 32;

    pack->tile_spikes = malloc(sizeof(SDL_Rect));
    assert(pack->tile_spikes != NULL);
    pack->tile_spikes->x = 0;
    pack->tile_spikes->y = 32;
    pack->tile_spikes->w = 32;
    pack->tile_spikes->h = 32;

    return pack;
}

void
textures_unload (struct textures *pack)
{
    free(pack->blob_0);
    free(pack->blob_1);
    free(pack->enemy_0);
    free(pack->enemy_1);
    free(pack->tile_normal);
    free(pack->tile_spikes);
    free(pack);
}
