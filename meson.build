project('noseguy-wayland', 'c',
  version : '0.1.0',
  default_options : ['c_std=c11', 'warning_level=2'])

wayland_client = dependency('wayland-client')
cairo_dep      = dependency('cairo')
pangocairo_dep = dependency('pangocairo')
threads_dep    = dependency('threads')

wayland_scanner   = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
wp_dir            = wayland_protocols.get_variable('pkgdatadir')

layer_shell_xml = files('protocol/wlr-layer-shell-unstable-v1.xml')
xdg_shell_xml   = wp_dir / 'stable/xdg-shell/xdg-shell.xml'

layer_shell_h = custom_target('layer-shell-header',
  input  : layer_shell_xml,
  output : 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

layer_shell_c = custom_target('layer-shell-code',
  input  : layer_shell_xml,
  output : 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

xdg_shell_c = custom_target('xdg-shell-code',
  input  : xdg_shell_xml,
  output : 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

inc     = include_directories('src', 'data')
gen     = [layer_shell_h, layer_shell_c, xdg_shell_c]

src_core = files('src/anim.c', 'src/text.c')
src_all  = src_core + files('src/render.c', 'src/wayland.c', 'src/main.c')

executable('noseguy-wayland',
  src_all + gen,
  dependencies        : [wayland_client, cairo_dep, pangocairo_dep, threads_dep],
  include_directories : inc,
  install             : true)

# Unit tests — no Wayland, no cairo
test_anim_exe = executable('test-anim',
  ['tests/test_anim.c', 'src/anim.c'],
  include_directories : inc)
test('anim state machine', test_anim_exe)

test_text_exe = executable('test-text',
  ['tests/test_text.c', 'src/text.c'],
  dependencies        : [threads_dep],
  include_directories : inc)
test('text provider', test_text_exe)

# Optional plugin target
if get_option('plugin')
  m_dep = meson.get_compiler('c').find_library('m')
  shared_library('noseguy-plugin',
    src_core + files('src/render.c', 'src/plugin.c'),
    dependencies        : [cairo_dep, pangocairo_dep, threads_dep, m_dep],
    include_directories : inc,
    install             : true)
endif
