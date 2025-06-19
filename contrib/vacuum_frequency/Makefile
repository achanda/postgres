MODULE_big = vacuum_frequency
OBJS = \
	$(WIN32RES) \
	vacuum_frequency.o
PGFILEDESC = "vacuum_frequency - Control VACUUM frequency with custom scheduling"

PG_CPPFLAGS = -I$(top_srcdir)/src/include
SHLIB_LINK = $(libpq)

EXTENSION = vacuum_frequency
DATA = sql/vacuum_frequency.sql
PGSQL_DATA = vacuum_frequency.control

REGRESS = vacuum_frequency

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/vacuum_frequency
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif 