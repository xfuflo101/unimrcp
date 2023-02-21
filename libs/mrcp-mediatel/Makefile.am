MAINTAINERCLEANFILES     = Makefile.in

AM_CPPFLAGS              = -I$(top_srcdir)/libs/mrcp/include \
                           -I$(top_srcdir)/libs/mrcp/message/include \
                           -I$(top_srcdir)/libs/mrcp/control/include \
                           -I$(top_srcdir)/libs/mrcp/resources/include \
                           -I$(top_srcdir)/libs/apr-toolkit/include \
                           $(UNIMRCP_APR_INCLUDES)

noinst_LTLIBRARIES       = libmrcp-mediatel.la

include_HEADERS          = include/mrcp_mediatel.h

libmrcp_la_SOURCES       = src/mrcp_mediatel.cpp
