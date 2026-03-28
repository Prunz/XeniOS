project_root = "../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-kernel")
  uuid("ae185c4a-1c4f-4503-9892-328e549e871a")
  kind("StaticLib")
  language("C++")
  sysincludedirs({
    project_root.."/third_party/asio/include",
  })
  defines({
    "ASIO_STANDALONE",
    "ASIO_NO_DEPRECATED",
  })
  filter("platforms:Windows-*")
    defines({
      "_WIN32_WINNT=0x0A00",  -- Windows 10+ for Asio
      "_WINSOCK_DEPRECATED_NO_WARNINGS",  -- Suppress deprecated Winsock API warnings
    })
  filter({})
  filter("platforms:iOS-*")
    links({
      "aes_128",
      "fmt",
      "zlib-ng",
      "pugixml",
      "xenia-apu",
      "xenia-base",
      "xenia-cpu",
      "xenia-hid",
      "xenia-vfs",
    })
    linkoptions({ "-lcurl" })
  filter("platforms:Windows-*")
    links({
      "aes_128",
      "fmt",
      "libcurl",
      "zlib-ng",
      "pugixml",
      "xenia-apu",
      "xenia-base",
      "xenia-cpu",
      "xenia-hid",
      "xenia-vfs",
    })
  filter("platforms:iOS-*")
    sysincludedirs({
      project_root.."/third_party/rapidjson/include",
    })
    linkoptions({ "-lcurl" })
  filter("platforms:Windows-*")
    sysincludedirs({
      project_root.."/third_party/libcurl/include",
      project_root.."/third_party/rapidjson/include",
    })
  filter {}
  defines({
    "X86_FEATURES",
    "X86_HAVE_XSAVE_INTRIN",
    "X86_SSSE3",
    "X86_SSE42",
    "WITH_GZFILEOP",
  })
  if os.istarget("windows") then
    defines({
      "X86_SSE2",
      "X86_AVX2",
      "X86_AVX512",
      "X86_AVX512VNNI",
      "X86_PCLMULQDQ_CRC",
      "X86_VPCLMULQDQ_CRC",
    })
  end
  recursive_platform_files()
  files({
    "debug_visualizers.natvis",
  })
