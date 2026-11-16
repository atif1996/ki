function(ki_target_warnings target)
  if(MSVC)
    # C4251/C4275 flag standard-library members and bases of exported classes; they are false alarms when ki
    # and its users share a compiler and standard library, which a C++ library ABI already requires.
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4251 /wd4275
      $<$<BOOL:${KI_WARNINGS_AS_ERRORS}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow
      $<$<BOOL:${KI_WARNINGS_AS_ERRORS}>:-Werror>)
  endif()
endfunction()
