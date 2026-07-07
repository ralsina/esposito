#ifndef LUCIDE_ICONS_H
#define LUCIDE_ICONS_H

/**
 * Lucide Icon Constants
 *
 * All icons use proper 3-byte UTF-8 encoding for E000-FFFF range codepoints.
 * Use these constants instead of raw escape sequences to avoid encoding errors.
 */

// Only icons actually embedded in the font bundles (see LUCIDE_CODEPOINTS
// in scripts/generate_vlw_fonts.py).

// Navigation
#define ICON_ARROW_BIG_UP "\xee\x87\xa4"
#define ICON_ARROW_BIG_DOWN "\xee\x87\xa1"
#define ICON_ARROW_BIG_RIGHT "\xee\x87\xa3"
#define ICON_ARROW_BIG_LEFT "\xee\x87\xa2"
#define ICON_ARROW_DOWN_TO_LINE "\xee\x91\x95"
#define ICON_CHEVRON_UP "\xee\x81\xb0"
#define ICON_CHEVRON_DOWN "\xee\x81\xad"
#define ICON_CHEVRON_LEFT "\xee\x81\xae"
#define ICON_CHEVRON_RIGHT "\xee\x81\xaf"
#define ICON_ARROW_UP "\xee\x81\x8a"
#define ICON_ARROW_DOWN "\xee\x81\x82"
#define ICON_ARROW_LEFT "\xee\x81\x88"
#define ICON_ARROW_RIGHT "\xee\x81\x89"
#define ICON_HOME "\xee\x83\xb5"
#define ICON_MENU "\xee\x84\x95"

// Files & Folders
#define ICON_FOLDER "\xee\x83\x97"
#define ICON_FOLDER_PLUS "\xee\x83\x99"
#define ICON_FILE_PLUS "\xee\x83\x89"
#define ICON_FILE_MINUS "\xee\x83\x86"
#define ICON_EXTERNAL_LINK "\xee\x82\xb9"
#define ICON_BOOK_OPEN "\xee\x81\x9f"

// Actions
#define ICON_SEARCH "\xee\x85\x91"
#define ICON_COPY "\xee\x82\x9e"
#define ICON_DOWNLOAD "\xee\x82\xb2"
#define ICON_UPLOAD "\xee\x86\x9e"
#define ICON_SAVE "\xee\x85\x8d"
#define ICON_EDIT_2 "\xee\x84\xaf"
#define ICON_TRASH "\xee\x86\x8d"
#define ICON_TRASH_2 "\xee\x86\x8e"

// Status
#define ICON_CHECK "\xee\x81\xac"
#define ICON_X "\xee\x86\xb2"
#define ICON_CHECK_CIRCLE "\xee\x81\xbc"
#define ICON_X_CIRCLE "\xee\x82\x84"

// System
#define ICON_SETTINGS "\xee\x85\x94"
#define ICON_POWER "\xee\x81\x99"
#define ICON_REFRESH_CW "\xee\x81\xa4"

// Media
#define ICON_PLAY "\xee\x84\xbc"
#define ICON_PAUSE "\xee\x84\xae"

#endif // LUCIDE_ICONS_H
