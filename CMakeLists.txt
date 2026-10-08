set(PROSPECTOR_MODULE_DIR ${CMAKE_CURRENT_LIST_DIR})

zephyr_include_directories(${PROSPECTOR_MODULE_DIR}/include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
zephyr_library()

if(CONFIG_SHIELD_PROSPECTOR_ADAPTER)

        add_subdirectory(${PROSPECTOR_MODULE_DIR}/drivers/display)

        if(CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED)
                zephyr_library_sources(${PROSPECTOR_MODULE_DIR}/src/events/caps_word_state_changed.c)
                set_source_files_properties(
                        ${PROSPECTOR_MODULE_DIR}/src/behaviors/behavior_caps_word.c
                        TARGET_DIRECTORY app
                        PROPERTIES HEADER_FILE_ONLY ON)
                target_sources(app PRIVATE ${PROSPECTOR_MODULE_DIR}/src/behaviors/behavior_caps_word.c)
        endif()

        zephyr_library_sources(${PROSPECTOR_MODULE_DIR}/src/events/split_central_status_changed.c)
        zephyr_library_sources(${PROSPECTOR_MODULE_DIR}/src/split/bluetooth/central_status_changed_observer.c)

endif()
