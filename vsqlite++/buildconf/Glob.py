from os.path import dirname
import glob

def Glob(env,pattern):
    if env['BUILD_TO']=='.':
        return glob.glob(pattern)
    path=env.Dir('.').srcnode().abspath +"/"
    l=len(path)
    def shorten(x):
        return x[l:]
    result=glob.glob(path + pattern)
    result=map(shorten,result)
    return result

import SCons.Environment
SCons.Environment.Base.Glob=Glob
