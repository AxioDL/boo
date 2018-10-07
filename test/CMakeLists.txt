add_executable(booTest WIN32 main.cpp)
target_link_libraries(booTest boo logvisor xxhash ${BOO_SYS_LIBS})

if (COMMAND add_nro_target)
  set_target_properties(booTest PROPERTIES SUFFIX ".elf")
  add_nro_target(booTest booTest "Antidote/Jackoalan" "1.0.0")
endif()

if(COMMAND add_sanitizers)
  add_sanitizers(booTest)
endif()