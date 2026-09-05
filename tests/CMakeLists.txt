qt_add_executable(omakade_core_tests
  CoreTests.cpp
)

qt_add_executable(omakade_backup_recovery_tests BackupRecoveryTests.cpp)
target_link_libraries(omakade_backup_recovery_tests PRIVATE omakade_core Qt6::Test)
target_compile_definitions(omakade_backup_recovery_tests PRIVATE
  OMAKADE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME omakade_backup_recovery_tests COMMAND omakade_backup_recovery_tests)
set_tests_properties(omakade_backup_recovery_tests PROPERTIES TIMEOUT 60)

foreach(preference_case IN ITEMS gog-settings linked-preference)
  add_test(NAME omakade_${preference_case}_controls COMMAND omakade --${preference_case}-test)
  add_test(NAME omakade_${preference_case}_couch_controls COMMAND omakade --${preference_case}-test --couch)
  set_tests_properties(omakade_${preference_case}_controls omakade_${preference_case}_couch_controls PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()
foreach(preference_overlay IN ITEMS gog-folders linked-preference linked-preference-missing)
  add_test(NAME omakade_${preference_overlay}_desktop_render COMMAND omakade
    --render-overlay=${preference_overlay} --render-size=1000x850
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${preference_overlay}-desktop.png)
  add_test(NAME omakade_${preference_overlay}_couch_render COMMAND omakade --couch
    --render-overlay=${preference_overlay} --render-size=1920x1080
    --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/${preference_overlay}-couch.png)
  set_tests_properties(omakade_${preference_overlay}_desktop_render omakade_${preference_overlay}_couch_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()

add_test(NAME omakade_backup_editor COMMAND omakade --backup-editor-test)
add_test(NAME omakade_backup_editor_couch COMMAND omakade --backup-editor-test --couch)
add_test(NAME omakade_backup_editor_render COMMAND omakade --render-overlay=backup-editor
  --render-size=900x720 --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/backup-editor-desktop.png)
add_test(NAME omakade_backup_editor_couch_render COMMAND omakade --couch --render-overlay=backup-editor
  --render-size=1920x1080 --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/backup-editor-couch.png)
set_tests_properties(omakade_backup_editor omakade_backup_editor_couch
  omakade_backup_editor_render omakade_backup_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

foreach(restore_case IN ITEMS success retry undo close)
  add_test(NAME omakade_restore_startup_${restore_case}
    COMMAND omakade --restore-startup-test=${restore_case})
  set_tests_properties(omakade_restore_startup_${restore_case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 20)
endforeach()
add_test(NAME omakade_restore_startup_couch
  COMMAND omakade --restore-startup-test=undo --couch --render-size=1920x1080
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/restore-startup-couch.png)
add_test(NAME omakade_restore_startup_desktop
  COMMAND omakade --restore-startup-test=retry --render-size=900x620
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/restore-startup-desktop.png)
set_tests_properties(omakade_restore_startup_couch omakade_restore_startup_desktop PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 20)

target_link_libraries(omakade_core_tests PRIVATE
  omakade_core
  Qt6::Core
  Qt6::Gui
  Qt6::Test
)

add_test(NAME omakade_core_tests COMMAND omakade_core_tests)
set_tests_properties(omakade_core_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(NAME omakade_qml_smoke COMMAND omakade --smoke-test)
set_tests_properties(omakade_qml_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_controller_navigation
  COMMAND omakade --controller-navigation-test --uninstalled-layout-test
)
set_tests_properties(omakade_controller_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_controller_navigation_tiled
  COMMAND omakade --controller-navigation-test --owned-layout-test --uninstalled-layout-test
          --render-size=820x590
)
set_tests_properties(omakade_controller_navigation_tiled PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_controller_navigation_wide
  COMMAND omakade --controller-navigation-test --owned-layout-test --uninstalled-layout-test
          --render-size=1600x900
)

add_test(NAME omakade_couch_navigation
  COMMAND omakade --couch --couch-navigation-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-couch-navigation"
  TIMEOUT 30
)

add_test(NAME omakade_couch_thousand_game_startup
  COMMAND omakade --couch --stress-test --benchmark --benchmark-max-ms=500
)
set_tests_properties(omakade_couch_thousand_game_startup PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  RUN_SERIAL TRUE
  TIMEOUT 5
)

foreach(benchmark_case IN ITEMS text empty missing nonpositive)
  if(benchmark_case STREQUAL "text")
    set(benchmark_limit_argument --benchmark-max-ms=fast)
  elseif(benchmark_case STREQUAL "empty")
    set(benchmark_limit_argument --benchmark-max-ms=)
  elseif(benchmark_case STREQUAL "missing")
    set(benchmark_limit_argument --benchmark-max-ms)
  else()
    set(benchmark_limit_argument --benchmark-max-ms=-1)
  endif()
  add_test(NAME omakade_benchmark_rejects_${benchmark_case}_limit
    COMMAND omakade --benchmark ${benchmark_limit_argument}
  )
  set_tests_properties(omakade_benchmark_rejects_${benchmark_case}_limit PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    WILL_FAIL TRUE
    TIMEOUT 5
  )
endforeach()

add_test(NAME omakade_cli_rejects_missing_play_value COMMAND omakade --play)
add_test(NAME omakade_cli_rejects_missing_screenshot_path COMMAND omakade --render-screenshot)
add_test(NAME omakade_cli_rejects_invalid_render_size
  COMMAND omakade --smoke-test --render-size=wide
)
set_tests_properties(
  omakade_cli_rejects_missing_play_value
  omakade_cli_rejects_missing_screenshot_path
  omakade_cli_rejects_invalid_render_size
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    WILL_FAIL TRUE
    TIMEOUT 5
)

add_test(NAME omakade_couch_thousand_game_navigation
  COMMAND omakade --couch --stress-test --couch-navigation-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_thousand_game_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE=;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-thousand-navigation"
  TIMEOUT 10
)

add_test(NAME omakade_couch_owned_navigation
  COMMAND omakade --couch --couch-navigation-test --owned-layout-test
          --uninstalled-layout-test --render-size=1920x1080
)
set_tests_properties(omakade_couch_owned_navigation PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE=;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-owned-navigation"
  TIMEOUT 10
)
set_tests_properties(omakade_controller_navigation_wide PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_render_smoke
  COMMAND omakade --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-render.png
          --render-size=820x590
)

add_test(NAME omakade_couch_render
  COMMAND omakade --couch
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_reduced_motion_render
  COMMAND omakade --couch --reduced-motion
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-reduced-motion.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_reduced_motion_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_light_opaque_render
  COMMAND omakade --couch
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-light-opaque.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_light_opaque_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;XDG_STATE_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/light-state;XDG_CONFIG_HOME=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/empty-config;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30
)

foreach(couch_case IN ITEMS 720p 1440p ultrawide 4k)
  if(couch_case STREQUAL "720p")
    set(couch_size 1280x720)
  elseif(couch_case STREQUAL "1440p")
    set(couch_size 2560x1440)
  elseif(couch_case STREQUAL "ultrawide")
    set(couch_size 2560x1080)
  else()
    set(couch_size 3840x2160)
  endif()
  add_test(NAME omakade_couch_render_${couch_case}
    COMMAND omakade --couch
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-${couch_case}.png
            --render-size=${couch_size}
  )
  set_tests_properties(omakade_couch_render_${couch_case} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30
  )
endforeach()

foreach(grid_size IN ITEMS 720p 1080p 4k)
  if(grid_size STREQUAL "720p")
    set(grid_dimensions 1280x720)
  elseif(grid_size STREQUAL "1080p")
    set(grid_dimensions 1920x1080)
  else()
    set(grid_dimensions 3840x2160)
  endif()
  add_test(NAME omakade_couch_grid_${grid_size}_render
    COMMAND omakade --couch --owned-layout-test
            --render-overlay=couch-grid
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-grid-${grid_size}.png
            --render-size=${grid_dimensions}
  )
  set_tests_properties(omakade_couch_grid_${grid_size}_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30
  )
endforeach()

foreach(couch_overlay IN ITEMS couch-search couch-browse couch-settings-bottom)
  string(REPLACE "-" "_" couch_overlay_test ${couch_overlay})
  add_test(NAME omakade_${couch_overlay_test}_4k_render
    COMMAND omakade --couch --owned-layout-test
            --render-overlay=${couch_overlay}
            --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-${couch_overlay}-4k.png
            --render-size=3840x2160
  )
  set_tests_properties(omakade_${couch_overlay_test}_4k_render PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
    TIMEOUT 30
  )
endforeach()

add_test(NAME omakade_couch_detail_4k_render
  COMMAND omakade --couch --uninstalled-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-detail-4k.png
          --render-size=3840x2160
)
set_tests_properties(omakade_couch_detail_4k_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_render_200_percent
  COMMAND omakade --couch
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-200-percent.png
          --render-size=1280x720
)
set_tests_properties(omakade_couch_render_200_percent PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QT_SCALE_FACTOR=2"
  TIMEOUT 30
)

add_test(NAME omakade_couch_detail_render
  COMMAND omakade --couch --uninstalled-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-detail.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_detail_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_keyboard_render
  COMMAND omakade --couch
          --render-overlay=couch-search
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-keyboard.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_keyboard_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_browse_render
  COMMAND omakade --couch
          --render-overlay=couch-browse
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-browse.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_browse_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_text_entry_render
  COMMAND omakade --couch
          --render-overlay=couch-entry
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-text-entry.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_text_entry_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_settings_render
  COMMAND omakade --couch --owned-layout-test
          --render-overlay=couch-settings-top
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-settings.png
          --render-size=1920x1080
)
set_tests_properties(omakade_couch_settings_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_couch_settings_720p_render
  COMMAND omakade --couch --owned-layout-test
          --render-overlay=couch-settings-bottom
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-couch-settings-720p.png
          --render-size=1280x720
)
set_tests_properties(omakade_couch_settings_720p_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)
set_tests_properties(omakade_render_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_render_space_separated_options
  COMMAND omakade --render-screenshot ${CMAKE_CURRENT_BINARY_DIR}/omakade-render-spaced.png
          --render-size 820x590
)
set_tests_properties(omakade_render_space_separated_options PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_owned_layout_render
  COMMAND omakade --owned-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-owned-layout.png
          --render-size=820x590
)
set_tests_properties(omakade_owned_layout_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_uninstalled_detail_render
  COMMAND omakade --owned-layout-test --uninstalled-layout-test
          --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/omakade-uninstalled-detail.png
          --render-size=820x590
)
set_tests_properties(omakade_uninstalled_detail_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
  TIMEOUT 30
)

add_test(NAME omakade_manual_editor COMMAND omakade --manual-editor-test)
add_test(NAME omakade_manual_editor_couch COMMAND omakade --manual-editor-test --couch)
add_test(NAME omakade_manual_editor_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/manual-editor.png
  --render-overlay=manual-editor --render-size=820x590)
add_test(NAME omakade_manual_editor_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/manual-editor-couch.png
  --render-overlay=manual-editor --render-size=1920x1080)
set_tests_properties(omakade_manual_editor omakade_manual_editor_couch
  omakade_manual_editor_render omakade_manual_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_artwork_editor COMMAND omakade --artwork-editor-test)
add_test(NAME omakade_artwork_editor_couch COMMAND omakade --artwork-editor-test --couch)
add_test(NAME omakade_artwork_editor_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-editor.png
  --render-overlay=artwork-editor --render-size=820x590)
add_test(NAME omakade_artwork_editor_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/artwork-editor-couch.png
  --render-overlay=artwork-editor --render-size=1920x1080)
set_tests_properties(omakade_artwork_editor omakade_artwork_editor_couch
  omakade_artwork_editor_render omakade_artwork_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_random_selection COMMAND omakade --random-selection-test)
add_test(NAME omakade_random_selection_couch COMMAND omakade --random-selection-test --couch)
add_test(NAME omakade_random_selection_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/random-selection.png
  --render-overlay=random-selection --render-size=820x590)
set_tests_properties(omakade_random_selection omakade_random_selection_couch
  omakade_random_selection_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_saved_filters COMMAND omakade --saved-filter-test)
add_test(NAME omakade_saved_filters_couch COMMAND omakade --saved-filter-test --couch)
add_test(NAME omakade_saved_filters_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/saved-filters.png
  --render-overlay=saved-filters --render-size=820x590)
add_test(NAME omakade_saved_filters_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/saved-filters-couch.png
  --render-overlay=saved-filters --render-size=1920x1080)
set_tests_properties(omakade_saved_filters omakade_saved_filters_couch
  omakade_saved_filters_render omakade_saved_filters_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)

add_test(NAME omakade_bulk_editor COMMAND omakade --bulk-editor-test)
add_test(NAME omakade_bulk_editor_couch COMMAND omakade --bulk-editor-test --couch)
add_test(NAME omakade_bulk_editor_render COMMAND omakade
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/bulk-editor.png
  --render-overlay=bulk-editor --render-size=820x590)
add_test(NAME omakade_bulk_editor_couch_render COMMAND omakade --couch
  --render-screenshot=${CMAKE_CURRENT_BINARY_DIR}/bulk-editor-couch.png
  --render-overlay=bulk-editor --render-size=1920x1080)
set_tests_properties(omakade_bulk_editor omakade_bulk_editor_couch
  omakade_bulk_editor_render omakade_bulk_editor_couch_render PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;HYPRLAND_INSTANCE_SIGNATURE="
  TIMEOUT 30)
