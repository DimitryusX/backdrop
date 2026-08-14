#pragma once

#include <libintl.h>

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "backdrop"
#endif

#define _(String) gettext(String)
#define N_(String) (String)

namespace backdrop {

/** Bind gettext domain using OS locale (LANG / LANGUAGE / LC_MESSAGES). */
void init_i18n();

}  // namespace backdrop
