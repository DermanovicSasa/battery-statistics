cc = meson.get_compiler('c')

gtk_dep = dependency('gtk4', version: '>= 4.8')
adw_dep = dependency('libadwaita-1', version: '>= 1.4')
m_dep = cc.find_library('m', required: true)

sources = files(
  'main.c',
  'battery-model.c',
  'upower-service.c',
  'battery-chart.c',
  'battery-window.c',
)

executable(
  'battery-statistics',
  sources,
  dependencies: [gtk_dep, adw_dep, m_dep],
  install: true,
)
