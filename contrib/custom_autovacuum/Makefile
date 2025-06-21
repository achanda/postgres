# contrib/custom_autovacuum/Makefile

MODULE_big = custom_autovacuum
OBJS = \
	$(WIN32RES) \
	custom_autovacuum.o
PGFILEDESC = "custom_autovacuum - Custom autovacuum policy framework"

EXTENSION = custom_autovacuum
DATA = custom_autovacuum--1.0.sql

REGRESS = 

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/custom_autovacuum
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif 