#ifndef textures_h
#define textures_h

#include <SDL2/SDL.h>

struct textures {
    SDL_Texture *t;

    SDL_Rect *blob_0;
    SDL_Rect *blob_1;
    SDL_Rect *enemy_0;
    SDL_Rect *enemy_1;
    SDL_Rect *tile_normal;
    SDL_Rect *tile_spikes;
};

struct textures *textures_load (SDL_Renderer *renderer);
void textures_unload (struct textures *pack);

#endif
