group("third_party")
project("libcurl")
  uuid("1ba7e608-5752-457c-8df0-c006c6e8b7fe")
  kind("StaticLib")
  language("C")

  -- Windows: build from source
  filter("platforms:Windows-*")
    defines({
      "BUILDING_LIBCURL",
      "CURL_STATICLIB",
      "USE_SCHANNEL",
      "USE_WINDOWS_SSPI",
    })
    links({ "Wldap32", "crypt32", "ws2_32" })
    includedirs({
      "libcurl/lib",
      "libcurl/include",
    })
    files({
      "libcurl/lib/**.h",
      "libcurl/lib/**.c",
    })
    filter({"configurations:Release", "platforms:Windows-*"})
      buildoptions({ "/Os", "/O1" })

  -- iOS: use system curl from the SDK, no source compilation needed
  filter("platforms:iOS-*")
    kind("StaticLib")
    files({})  -- no source files, system curl is used
    linkoptions({ "-lcurl" })

  filter {}