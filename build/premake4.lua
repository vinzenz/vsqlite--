
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
 
      configuration "Debug"
         defines { "DEBUG" }
         flags { "Symbols" }
 
      configuration "Release"
         defines { "NDEBUG" }
         flags { "Optimize" }    

    --- The unit test definition
    project "vsqlite_example"
        kind "ConsoleApp"
        language "C++"
        files { "../examples/**.cpp" }
        includedirs { "../include" }
        links { "vsqlite++-shared", "sqlite3" }

        --- Configuration settings
        configuration "Debug"
            defines { "DEBUG" }
            flags { "Symbols" }
 
        configuration "Release"
            defines { "NDEBUG" }
            flags { "Optimize" }    

