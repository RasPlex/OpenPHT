set(CMAKE_SHARED_MODULE_SUFFIX ".vis")
set(CMAKE_SHARED_MODULE_PREFIX "")
if(NOT WIN32)
  if(NOT TARGET_RPI)
    add_definitions(-DHAS_SDL -DHAS_SDL_OPENGL)
  endif()
endif()

add_subdirectory(WaveForm)

if(NOT WIN32)

  if(TARGET_RPI)
    add_subdirectory(EGLHelpers)
  else()
    add_subdirectory(XBMCProjectM)
  endif()

  add_subdirectory(OpenGLSpectrum)
  
  if (NOT APPLE AND NOT TARGET_RPI)
    add_subdirectory(Goom)
  endif()
else()
  add_subdirectory(DirectXSpectrum)
endif()
