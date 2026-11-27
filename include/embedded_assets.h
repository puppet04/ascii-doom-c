#ifndef EMBEDDED_ASSETS_H
#define EMBEDDED_ASSETS_H

/* Copia embutida dos mapas e do icone no .exe caso rode sem a pasta assets/ do lado */
const char *embedded_map_lookup(const char *filename);
const unsigned char *embedded_icon_png_data(int *out_len);

#endif /* EMBEDDED_ASSETS_H */
