# contrib/custom_autovacuum/meson.build

custom_autovacuum_sources = files(
  'custom_autovacuum.c',
)

custom_autovacuum = shared_module('custom_autovacuum',
  custom_autovacuum_sources,
  kwargs: contrib_mod_args,
)
contrib_targets += custom_autovacuum

install_data(
  'custom_autovacuum.control',
  'custom_autovacuum--1.0.sql',
  kwargs: contrib_data_args,
)

tests += {
  'name': 'custom_autovacuum',
  'sd': meson.current_source_dir(),
  'bd': meson.current_build_dir(),
} 