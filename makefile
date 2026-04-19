# ----------------------------
# Makefile Options
# ----------------------------

NAME = CERE
ICON = icon.png
DESCRIPTION = "CERE Chinese reader"
COMPRESSED = YES
ARCHIVED = YES

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
