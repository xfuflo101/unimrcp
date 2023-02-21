MAINTAINERCLEANFILES     = Makefile.in

AM_CPPFLAGS              = -I$(top_srcdir)/libs/mrcp/include \
                           -I$(top_srcdir)/libs/mrcp/message/include \
                           -I$(top_srcdir)/libs/mrcp/control/include \
                           -I$(top_srcdir)/libs/mrcp/resources/include \
                           -I$(top_srcdir)/libs/apr-toolkit/include \
                           -Iinclude \
                           $(UNIMRCP_APR_INCLUDES)

noinst_PROGRAMS           = mrcp-mediatel

mrcp_mediatel_LDADD       = $(top_builddir)/libs/mrcp/libmrcp.la \
                            $(top_builddir)/libs/apr-toolkit/libaprtoolkit.la \
                            $(UNIMRCP_APR_LIBS)

include_HEADERS          = include/mrcp_mediatel.h

mrcp_mediatel_SOURCES    = src/main.cpp \
                           src/mrcp_mediatel.cpp
