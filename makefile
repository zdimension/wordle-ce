# ----------------------------
# Makefile Options
# ----------------------------

NAME = WORDLECE
ICON = icon.png
DESCRIPTION = "Wordle on your ez80"
COMPRESSED = NO
ARCHIVED = YES

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
