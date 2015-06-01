find_all_sources(. hwdecrender_SRCS)

if(NOT TARGET_RPI)
  list(REMOVE_ITEM hwdecrender_SRCS ./MMALRenderer.cpp)
endif()

add_sources(${hwdecrender_SRCS})
