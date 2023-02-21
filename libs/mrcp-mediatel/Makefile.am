MAINTAINERCLEANFILES     = Makefile.in

AM_CPPFLAGS              = -I$(top_srcdir)/libs/mrcp/include \
                           -I$(top_srcdir)/libs/mrcp/message/include \
                           -I$(top_srcdir)/libs/mrcp/control/include \
                           -I$(top_srcdir)/libs/mrcp/resources/include \
                           -I$(top_srcdir)/libs/apr-toolkit/include \
                           $(UNIMRCP_APR_INCLUDES)

noinst_LTLIBRARIES       = libmrcp-mediatel.la

include_HEADERS          = include/mrcp_mediatel.h

libmrcp_la_SOURCES       = message/src/mrcp_start_line.c \
                           message/src/mrcp_header_accessor.c \
                           message/src/mrcp_generic_header.c \
                           message/src/mrcp_header.c \
                           message/src/mrcp_message.c \
                           control/src/mrcp_resource_factory.c \
                           control/src/mrcp_resource_loader.c \
                           control/src/mrcp_stream.c \
                           resources/src/mrcp_synth_header.c \
                           resources/src/mrcp_synth_resource.c \
                           resources/src/mrcp_recog_header.c \
                           resources/src/mrcp_recog_resource.c \
                           resources/src/mrcp_recorder_header.c \
                           resources/src/mrcp_recorder_resource.c \
                           resources/src/mrcp_verifier_header.c \
                           resources/src/mrcp_verifier_resource.c
