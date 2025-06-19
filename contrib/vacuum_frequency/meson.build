# vacuum_frequency extension
vacuum_frequency_sources = files(
  'vacuum_frequency.c',
)

vacuum_frequency = shared_module('vacuum_frequency',
  vacuum_frequency_sources,
  include_directories: [postgres_inc],
  link_with: [postgres],
  install: true,
  install_dir: dir_lib,
)

# Extension control file
install_data('vacuum_frequency.control',
  kwargs: contrib_data_args,
)

# SQL script
install_data('sql/vacuum_frequency.sql',
  kwargs: contrib_data_args,
) 