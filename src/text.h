#pragma once

#include "noseguy.h"

typedef struct TextProvider TextProvider;

/* Create a provider from config. Returns NULL on allocation failure. */
TextProvider *text_provider_create(const TextConfig *cfg);

/* Return next text string. Never blocks. Never returns NULL. */
const char   *text_get_next(TextProvider *tp);

void          text_provider_destroy(TextProvider *tp);
