
--- Solution
solution "vsqlite++"
   configurations { "Debug", "Release" }
 
   --- The library evipp definition
   project "vsqlite++-static"
      kind "StaticLib"
      language "C++"
      targetname "vsqlite++"
      files { "../include/**.hpp", "../src/sqlite/**.cpp" }
      includedirs { "../include" }
 
      configuration "Debug"
         defines { "DEBUG" }
         flags { "Symbols" }
 
      configuration "Release"
         defines { "NDEBUG" }
         flags { "Optimize" }    

   project "vsqlite++-shared"
      kind "SharedLib"
      language "C++"
      targetname "vsqlite++"
      files { "../include/**.hpp", "../src/sqlite/**.cpp" }
      includedirs { "../include" }
      links { "sqlite3" }
 
      configuration "Debug"
         defines { "DEBUG" }
         flags { "Symbols" }
 
      configuration "Release"
         defines { "NDEBUG" }
         flags { "Optimize" }
    
      configuration {"linux", "gmake"}
         linkoptions { "-Wl,--as-needed", "-Wl,-soname,libvsqlite++.so.0" }

    --- The unit test definition
    project "vsqlite_example"
        kind "ConsoleApp"
        language "C++"
        files { "../examples/**.cpp" }
        includedirs { "../include" }
        links { "vsqlite++-shared" }

        --- Configuration settings
        configuration "Debug"
            defines { "DEBUG" }
            flags { "Symbols" }
 
        configuration "Release"
            defines { "NDEBUG" }
            flags { "Optimize" }    

