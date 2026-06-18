gnome = import('gnome')
install_data(
  'org.example.BatteryStatistics.desktop',
  install_dir: get_option('datadir') / 'applications',
)
app_id = 'rs.someone.BatteryStatistics'
install_data(
  'icons/hicolor/symbolic/apps/' + app_id + '-symbolic.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/symbolic/apps',
)
gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)
