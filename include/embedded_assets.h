#ifndef EMBEDDED_ASSETS_H
#define EMBEDDED_ASSETS_H

/* Fallback embutido no .exe para rodar isolado (sem pasta assets ao lado). */
const char *embedded_map_lookup(const char *filename);
const unsigned char *embedded_icon_png_data(int *out_len);

#endif /* EMBEDDED_ASSETS_H */
