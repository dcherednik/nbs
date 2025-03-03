# Generated by devtools/yamaker from nixpkgs 22.11.

LIBRARY()

LICENSE(
    X11 AND
    X11-XConsortium-Veillard
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(1.1.38)

ORIGINAL_SOURCE(https://gitlab.gnome.org/api/v4/projects/GNOME%2Flibxslt/repository/archive.tar.gz?sha=v1.1.38)

PEERDIR(
    contrib/libs/libxml
    contrib/libs/libxslt
)

ADDINCL(
    GLOBAL contrib/libs/libexslt
    contrib/libs/libexslt/libexslt
    contrib/libs/libxslt/libxslt
)

NO_COMPILER_WARNINGS()

NO_RUNTIME()

NO_UTIL()

CFLAGS(
    -DHAVE_CONFIG_H
)

IF (OS_WINDOWS)
    CFLAGS(
        GLOBAL -DLIBEXSLT_STATIC
    )
ENDIF()

SRCS(
    libexslt/common.c
    libexslt/crypto.c
    libexslt/date.c
    libexslt/dynamic.c
    libexslt/exslt.c
    libexslt/functions.c
    libexslt/math.c
    libexslt/saxon.c
    libexslt/sets.c
    libexslt/strings.c
)

END()
