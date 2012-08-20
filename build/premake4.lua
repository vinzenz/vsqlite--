
--- Solution
solution "vsqlite++"
   configurations { "Debug", "Release" }
 
   --- The library evipp definition
   project "vsqlite++"
      kind "StaticLib"
      language "C++"
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
        links { "vsqlite++", "sqlite3" }

        --- Configuration settings
        configuration "Debug"
            defines { "DEBUG" }
            flags { "Symbols" }
 
        configuration "Release"
            defines { "NDEBUG" }
            flags { "Optimize" }    

